// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "codexmonitorplugin.h"

#include "codexappserverclient.h"
#include "codexdesktoplauncher.h"
#include "quotawidgets.h"

#include <QLabel>
#include <QPalette>

CodexMonitorPlugin::CodexMonitorPlugin(QObject *parent)
    : QObject(parent)
{
}

CodexMonitorPlugin::~CodexMonitorPlugin() = default;

const QString CodexMonitorPlugin::pluginName() const
{
    return QStringLiteral("codex-monitor");
}

const QString CodexMonitorPlugin::pluginDisplayName() const
{
    return QStringLiteral("Codex 额度");
}

void CodexMonitorPlugin::init(PluginProxyInterface *proxyInter)
{
    m_proxyInter = proxyInter; // 由 Loader 管理，不负责释放

    m_client.reset(new CodexAppServerClient(this));
    m_iconWidget.reset(new QuotaIconWidget);
    m_panelWidget.reset(new QuotaPanelWidget);

    // 快捷面板只固定高度，不锁定宽度
    m_panelWidget->setFixedHeight(Dock::QUICK_ITEM_HEIGHT);
    // Dock 状态图标固定 24x24
    m_iconWidget->setFixedSize(Dock::QUICK_PANEL_ICON_SIZE);

    // 悬停提示
    m_tipsLabel = new QLabel;
    m_tipsLabel->setContentsMargins(8, 6, 8, 6);
    m_tipsLabel->setWordWrap(false);
    m_tipsLabel->setForegroundRole(QPalette::WindowText);

    connect(m_client.data(), &CodexAppServerClient::stateChanged,
            this, [this](CodexClientState state, const QString &message) {
                m_message = message;
                m_iconWidget->setData(m_client->quota(), state);
                m_panelWidget->setData(m_client->quota(), state, message);
                updateTipsText();
                m_proxyInter->itemUpdate(this, QStringLiteral("codex-monitor"));
            });
    connect(m_client.data(), &CodexAppServerClient::quotaUpdated,
            this, [this](const QuotaState &quota) {
                m_iconWidget->setData(quota, m_client->state());
                m_panelWidget->setData(quota, m_client->state(), m_message);
                updateTipsText();
                m_proxyInter->itemUpdate(this, QStringLiteral("codex-monitor"));
            });

    // 点击卡片或托盘图标：手动刷新（带"正在读取"反馈）
    connect(m_panelWidget.data(), &QuotaPanelWidget::refreshRequested,
            m_client.data(), &CodexAppServerClient::refreshManually);
    connect(m_iconWidget.data(), &QuotaIconWidget::refreshRequested,
            m_client.data(), &CodexAppServerClient::refreshManually);
    // 双击托盘图标：打开 Codex 桌面版
    connect(m_iconWidget.data(), &QuotaIconWidget::launchRequested,
            this, &CodexMonitorPlugin::openCodexDesktop);

    m_tipNoteTimer.setSingleShot(true);
    m_tipNoteTimer.setInterval(5 * 1000);
    connect(&m_tipNoteTimer, &QTimer::timeout, this, [this]() {
        m_tipNote.clear();
        updateTipsText();
    });

    m_proxyInter->itemAdded(this, QStringLiteral("codex-monitor"));
    m_client->start();
}

QWidget *CodexMonitorPlugin::itemWidget(const QString &itemKey)
{
    if (itemKey == QStringLiteral("codex-monitor")) {
        return m_iconWidget.data();
    }
    if (itemKey == Dock::QUICK_ITEM_KEY) {
        return m_panelWidget.data();
    }
    return nullptr;
}

QString CodexMonitorPlugin::tipsText() const
{
    if (!m_client) {
        return QString();
    }
    if (!m_tipNote.isEmpty()) {
        return m_tipNote;
    }
    const QuotaState &quota = m_client->quota();
    switch (m_client->state()) {
    case CodexClientState::Ready: {
        QStringList parts;
        if (!quota.planType.isEmpty()) {
            parts << QStringLiteral("Codex ") + quota.planType;
        }
        if (quota.primary.valid) {
            parts << QStringLiteral("已用 %1%").arg(qRound(quota.primary.usedPercent))
                  << formatRelativeReset(quota.primary.resetsAt);
        }
        if (quota.secondary.valid) {
            const QString duration = formatWindowDuration(quota.secondary.durationMinutes);
            parts << (duration.isEmpty() ? QStringLiteral("已用 %1%").arg(qRound(quota.secondary.usedPercent))
                                         : QStringLiteral("%1 已用 %2%").arg(duration, QString::number(qRound(quota.secondary.usedPercent))))
                  << formatRelativeReset(quota.secondary.resetsAt);
        }
        if (parts.isEmpty()) {
            parts << pluginDisplayName();
        }
        parts << QStringLiteral("单击刷新 · 双击打开桌面版");
        return parts.join(QStringLiteral(" · "));
    }
    case CodexClientState::NoCli:
        return QStringLiteral("Codex 额度：未找到 Codex CLI");
    case CodexClientState::NotLoggedIn:
        return QStringLiteral("Codex 额度：Codex 尚未登录");
    case CodexClientState::NoQuota:
        return QStringLiteral("Codex 额度：当前账号没有可用额度");
    case CodexClientState::Failed:
        return QStringLiteral("Codex 额度：读取失败，单击重试");
    case CodexClientState::Starting:
    default:
        return QStringLiteral("Codex 额度：正在读取");
    }
}

void CodexMonitorPlugin::updateTipsText()
{
    if (m_tipsLabel) {
        m_tipsLabel->setText(tipsText());
    }
}

QWidget *CodexMonitorPlugin::itemTipsWidget(const QString &itemKey)
{
    if (itemKey == QStringLiteral("codex-monitor")) {
        return m_tipsLabel;
    }
    return nullptr;
}

void CodexMonitorPlugin::openCodexDesktop()
{
    QString error;
    if (CodexDesktop::launch(&error)) {
        return;
    }
    qWarning() << "[dde-codex-monitor] 打开 Codex 桌面版失败:" << error;
    m_tipNote = error;
    updateTipsText();
    m_tipNoteTimer.start();
}

Dock::PluginFlags CodexMonitorPlugin::flags() const
{
    // 快捷面板整行插件；第一版不添加 Attribute_CanSetting / Attribute_Normal
    return Dock::Type_Quick | Dock::Quick_Panel_Full;
}
