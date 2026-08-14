// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef CODEXMONITORPLUGIN_H
#define CODEXMONITORPLUGIN_H

#include <QObject>
#include <QScopedPointer>
#include <QTimer>

#include <pluginsiteminterface_v2.h>

#include "codexappserverclient.h"

class QLabel;
class CodexAppServerClient;
class CodexNotifier;
class QuotaIconWidget;
class QuotaPanelWidget;
class QuotaDetailWidget;

/**
 * @brief DDE 任务栏快捷面板插件：Codex 额度监控
 *
 * 提供 Dock 常驻状态图标（codex-monitor）、快捷面板整行额度卡片
 * （Dock::QUICK_ITEM_KEY）、详情子页面、系统通知与右键菜单。
 */
class CodexMonitorPlugin : public QObject, public PluginsItemInterfaceV2
{
    Q_OBJECT
    Q_INTERFACES(PluginsItemInterfaceV2)
    Q_PLUGIN_METADATA(IID ModuleInterface_iid_V2 FILE "codex-monitor.json")

public:
    explicit CodexMonitorPlugin(QObject *parent = nullptr);
    ~CodexMonitorPlugin() override;

    const QString pluginName() const override;
    const QString pluginDisplayName() const override;

    void init(PluginProxyInterface *proxyInter) override;

    QWidget *itemWidget(const QString &itemKey) override;
    QWidget *itemTipsWidget(const QString &itemKey) override;
    QWidget *itemPopupApplet(const QString &itemKey) override;
    const QString itemContextMenu(const QString &itemKey) override;
    void invokedMenuItem(const QString &itemKey, const QString &menuId, const bool checked) override;

    void setMessageCallback(MessageCallbackFunc callback) override;
    QString message(const QString &msg) override;

    Dock::PluginFlags flags() const override;

private slots:
    void onPrepareForSleep(bool aboutToSuspend);

private:
    QString tipsText() const;
    void updateTipsText();
    void openCodexDesktop();
    void showDetail();
    void updateDetailData();
    void onQuotaUpdated(const QuotaState &quota);
    void evaluateTransitions(const QuotaState &quota);
    void sendItemActiveState(bool active);
    QString shareText() const;
    bool notifyEnabled() const;
    void setNotifyEnabled(bool enabled);

    QScopedPointer<QuotaIconWidget> m_iconWidget;
    QScopedPointer<QuotaPanelWidget> m_panelWidget;
    QScopedPointer<QuotaDetailWidget> m_detailWidget;
    QScopedPointer<CodexNotifier> m_notifier;
    QScopedPointer<CodexAppServerClient> m_client;
    QLabel *m_tipsLabel = nullptr; // 由框架展示的悬停提示
    QString m_message;             // 最近一次状态提示（读取失败等原因）
    QString m_tipNote;             // 双击打开桌面版失败时的临时提示
    QTimer m_tipNoteTimer;         // 临时提示自动清除
    QuotaState m_prevQuota;        // 上一次额度快照（通知触发沿检测）
    bool m_hasPrevQuota = false;
    int m_lastActiveState = -1;    // -1 未发送过
    MessageCallbackFunc m_messageCallback = nullptr;
};

#endif // CODEXMONITORPLUGIN_H
