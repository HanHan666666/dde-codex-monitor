// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef CODEXCLIPBOARD_H
#define CODEXCLIPBOARD_H

#include <QByteArray>
#include <QObject>

class QSocketNotifier;
struct xcb_connection_t;

/**
 * @brief 通过独立 XCB 连接直接写 X11 CLIPBOARD 剪贴板
 *
 * 托盘插件进程跑在 dde-shell 内嵌的 Wayland 合成器上，该合成器不实现
 * wl_data_device，因此 QClipboard::setText() 在插件进程里是静默空操作。
 * 桌面会话（包括 treeland 的 Xwayland）始终有 X11 服务器，所以这里用
 * 独立的 XCB 连接成为 CLIPBOARD selection 属主，并在插件存活期间持续
 * 服务粘贴方的 SelectionRequest。
 */
class CodexClipboard : public QObject
{
    Q_OBJECT
public:
    explicit CodexClipboard(QObject *parent = nullptr);
    ~CodexClipboard() override;

    /**
     * @brief 把文本写入 X11 CLIPBOARD；返回是否成功取得 selection 属主
     */
    bool setText(const QString &text);

    bool isAvailable() const { return m_conn != nullptr; }

private:
    void handleXcbEvents();
    void handleSelectionRequest(void *event);
    quint32 internAtom(const char *name);

    xcb_connection_t *m_conn = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    quint32 m_window = 0;
    QByteArray m_utf8;
    QByteArray m_latin1;

    quint32 m_atomClipboard = 0;
    quint32 m_atomTargets = 0;
    quint32 m_atomUtf8String = 0;
    quint32 m_atomString = 0;
    quint32 m_atomText = 0;
    quint32 m_atomPlain = 0;
    quint32 m_atomPlainUtf8 = 0;
    quint32 m_atomTimestamp = 0;
};

#endif // CODEXCLIPBOARD_H
