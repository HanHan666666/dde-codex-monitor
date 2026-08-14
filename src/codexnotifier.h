// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef CODEXNOTIFIER_H
#define CODEXNOTIFIER_H

#include <QObject>

/**
 * @brief 通过 org.freedesktop.Notifications 发送系统通知
 *
 * 支持"打开 Codex 桌面版"动作按钮（deepin 通知中心支持通知动作），
 * 点击通知或按钮即启动桌面版。未启用时所有通知静默忽略。
 */
class CodexNotifier : public QObject
{
    Q_OBJECT
public:
    explicit CodexNotifier(QObject *parent = nullptr);

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool enabled() const { return m_enabled; }

    /**
     * @brief 发送一条系统通知
     *
     * @param urgency 0=低 1=普通 2=紧急
     * @param openDesktop 为 true 时通知提供"打开桌面版"动作
     */
    void notify(const QString &summary, const QString &body,
                int urgency = 1, bool openDesktop = false);

private slots:
    void onActionInvoked(uint notificationId, const QString &action);

private:
    bool m_enabled = true;
    uint m_lastNotificationId = 0;
    bool m_lastHasOpenAction = false;
};

#endif // CODEXNOTIFIER_H
