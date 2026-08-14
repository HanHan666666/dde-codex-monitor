// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "codexnotifier.h"

#include "codexdesktoplauncher.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

CodexNotifier::CodexNotifier(QObject *parent)
    : QObject(parent)
{
    // 监听通知动作回调（点击通知或按钮）
    QDBusConnection::sessionBus().connect(
            QString(), QStringLiteral("/org/freedesktop/Notifications"),
            QStringLiteral("org.freedesktop.Notifications"),
            QStringLiteral("ActionInvoked"),
            this, SLOT(onActionInvoked(uint,QString)));
}

void CodexNotifier::notify(const QString &summary, const QString &body,
                           int urgency, bool openDesktop)
{
    if (!m_enabled) {
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.Notifications"),
            QStringLiteral("/org/freedesktop/Notifications"),
            QStringLiteral("org.freedesktop.Notifications"),
            QStringLiteral("Notify"));
    // actions: [key, label, ...]；default 为点击通知本体
    QStringList actions;
    if (openDesktop) {
        actions << QStringLiteral("default") << QStringLiteral("打开 Codex 桌面版")
                << QStringLiteral("open") << QStringLiteral("打开 Codex 桌面版");
    }
    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"), urgency);
    // desktop-entry 决定通知归属的应用图标；没有我们自己的桌面入口，置空用通用图标
    msg.setArguments(QVariantList{
        QStringLiteral("DDE Codex Monitor"),
        QVariant::fromValue(m_lastNotificationId), // 替换上一条，避免刷屏
        QString(),
        summary,
        body,
        actions,
        hints,
        QVariant(8000) // 8 秒超时
    });
    // 异步发送，不阻塞 Dock 主线程；回复中的通知 id 用于动作匹配
    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<uint> reply = *w;
        if (reply.isValid()) {
            m_lastNotificationId = reply.value();
        }
        w->deleteLater();
    });
    m_lastHasOpenAction = openDesktop;
}

void CodexNotifier::onActionInvoked(uint notificationId, const QString &action)
{
    if (notificationId != m_lastNotificationId || !m_lastHasOpenAction) {
        return;
    }
    if (action == QStringLiteral("default") || action == QStringLiteral("open")) {
        QString error;
        if (!CodexDesktop::launch(&error)) {
            qWarning() << "[dde-codex-monitor] 通知动作打开桌面版失败:" << error;
        }
    }
}
