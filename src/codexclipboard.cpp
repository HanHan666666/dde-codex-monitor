// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "codexclipboard.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSocketNotifier>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include <cstdlib>
#include <cstring>

namespace {
// 插件进程环境变量被 dde-shell 清理过，连 X 前补上 HOME/XAUTHORITY，
// 否则 Xau 找不到认证 cookie。QDir::homePath() 走 passwd 不依赖环境变量。
void ensureAuthEnv()
{
    if (qEnvironmentVariableIsEmpty("HOME")) {
        qputenv("HOME", QDir::homePath().toUtf8());
    }
    if (qEnvironmentVariableIsEmpty("XAUTHORITY")) {
        const QString xauth = QDir::homePath() + QStringLiteral("/.Xauthority");
        if (QFileInfo::exists(xauth)) {
            qputenv("XAUTHORITY", xauth.toUtf8());
        }
    }
}
} // namespace

CodexClipboard::CodexClipboard(QObject *parent)
    : QObject(parent)
{
    ensureAuthEnv();

    // 会话显示号优先读环境，读不到时回退 deepin 固定的 :0 / :1
    QStringList displays;
    const QString envDisplay = qEnvironmentVariable("DISPLAY");
    if (!envDisplay.isEmpty()) {
        displays << envDisplay;
    }
    displays << QStringLiteral(":0") << QStringLiteral(":1");

    for (const QString &display : displays) {
        xcb_connection_t *conn = xcb_connect(display.toUtf8().constData(), nullptr);
        if (!conn || xcb_connection_has_error(conn)) {
            if (conn) {
                xcb_disconnect(conn);
            }
            continue;
        }
        m_conn = conn;
        break;
    }
    if (!m_conn) {
        qWarning() << "[dde-codex-monitor] clipboard: X11 connection failed,"
                   << "QClipboard fallback will be used";
        return;
    }

    // 建一个 1x1 窗口用于 selection 属主与事件投递
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_conn)).data;
    m_window = xcb_generate_id(m_conn);
    xcb_create_window(m_conn, XCB_COPY_FROM_PARENT, m_window, screen->root, 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
    xcb_flush(m_conn);

    m_atomClipboard = internAtom("CLIPBOARD");
    m_atomTargets = internAtom("TARGETS");
    m_atomUtf8String = internAtom("UTF8_STRING");
    m_atomString = XCB_ATOM_STRING;
    m_atomText = internAtom("TEXT");
    m_atomPlain = internAtom("text/plain");
    m_atomPlainUtf8 = internAtom("text/plain;charset=utf-8");
    m_atomTimestamp = internAtom("TIMESTAMP");

    m_notifier = new QSocketNotifier(xcb_get_file_descriptor(m_conn),
                                     QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated,
            this, &CodexClipboard::handleXcbEvents);
}

CodexClipboard::~CodexClipboard()
{
    if (m_conn) {
        // 释放属主并断开（连接随进程退出一起释放，这里保险起见显式清理）
        xcb_set_selection_owner(m_conn, XCB_WINDOW_NONE, m_atomClipboard, XCB_CURRENT_TIME);
        xcb_flush(m_conn);
        xcb_disconnect(m_conn);
    }
}

quint32 CodexClipboard::internAtom(const char *name)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(m_conn, 0,
                                                      static_cast<uint16_t>(std::strlen(name)),
                                                      name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(m_conn, cookie, nullptr);
    const quint32 atom = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

bool CodexClipboard::setText(const QString &text)
{
    if (!m_conn) {
        return false;
    }

    m_utf8 = text.toUtf8();
    // STRING/TEXT 目标是 Latin-1：中文等非 Latin-1 字符近似为 '?'，
    // 现代应用走 UTF8_STRING 不受影响
    m_latin1 = QByteArray();
    for (const QChar &ch : text) {
        m_latin1.append(ch.unicode() < 256 ? char(ch.unicode()) : '?');
    }

    xcb_set_selection_owner(m_conn, m_window, m_atomClipboard, XCB_CURRENT_TIME);
    xcb_flush(m_conn);

    // 同步确认属主确实拿到了
    xcb_get_selection_owner_cookie_t cookie = xcb_get_selection_owner(m_conn, m_atomClipboard);
    xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(m_conn, cookie, nullptr);
    const bool owned = reply && reply->owner == m_window;
    free(reply);
    if (!owned) {
        qWarning() << "[dde-codex-monitor] clipboard: failed to own CLIPBOARD selection";
    }
    return owned;
}

void CodexClipboard::handleXcbEvents()
{
    if (!m_conn) {
        return;
    }
    while (xcb_generic_event_t *event = xcb_poll_for_event(m_conn)) {
        const uint8_t type = event->response_type & 0x7f;
        if (type == XCB_SELECTION_REQUEST) {
            handleSelectionRequest(event);
        }
        // response_type == 0 是 X 错误事件：请求方窗口消失等竞态，忽略即可
        free(event);
    }
}

void CodexClipboard::handleSelectionRequest(void *event)
{
    auto *req = reinterpret_cast<xcb_selection_request_event_t *>(event);

    xcb_selection_notify_event_t notify{};
    notify.response_type = XCB_SELECTION_NOTIFY;
    notify.requestor = req->requestor;
    notify.selection = req->selection;
    notify.target = req->target;
    notify.property = XCB_ATOM_NONE;
    notify.time = req->time;

    // 请求方没指定属性时把数据放到以 target 命名的属性上（惯例）
    const quint32 property = req->property != XCB_ATOM_NONE ? req->property : req->target;

    if (req->target == m_atomTargets) {
        const quint32 atoms[] = {
            m_atomTargets, m_atomUtf8String, m_atomString, m_atomText,
            m_atomPlain, m_atomPlainUtf8, m_atomTimestamp,
        };
        xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, req->requestor, property,
                            XCB_ATOM_ATOM, 32, sizeof(atoms) / sizeof(atoms[0]), atoms);
        notify.property = property;
    } else if (req->target == m_atomTimestamp) {
        const quint32 time = XCB_CURRENT_TIME;
        xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, req->requestor, property,
                            m_atomTimestamp, 32, 1, &time);
        notify.property = property;
    } else if (req->target == m_atomUtf8String || req->target == m_atomPlain
               || req->target == m_atomPlainUtf8) {
        xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, req->requestor, property,
                            req->target, 8, static_cast<uint32_t>(m_utf8.size()),
                            m_utf8.constData());
        notify.property = property;
    } else if (req->target == m_atomString || req->target == m_atomText) {
        xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, req->requestor, property,
                            req->target, 8, static_cast<uint32_t>(m_latin1.size()),
                            m_latin1.constData());
        notify.property = property;
    }

    xcb_send_event(m_conn, 0, req->requestor, XCB_EVENT_MASK_NO_EVENT,
                   reinterpret_cast<const char *>(&notify));
    xcb_flush(m_conn);
}
