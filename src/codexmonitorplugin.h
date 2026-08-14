// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef CODEXMONITORPLUGIN_H
#define CODEXMONITORPLUGIN_H

#include <QObject>
#include <QScopedPointer>
#include <QTimer>

#include <pluginsiteminterface_v2.h>

class QLabel;
class CodexAppServerClient;
class QuotaIconWidget;
class QuotaPanelWidget;

/**
 * @brief DDE 任务栏快捷面板插件：Codex 额度监控
 *
 * 提供 Dock 常驻状态图标（codex-monitor）和快捷面板整行额度卡片
 * （Dock::QUICK_ITEM_KEY）。
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

    Dock::PluginFlags flags() const override;

private:
    QString tipsText() const;
    void updateTipsText();
    void openCodexDesktop();

    QScopedPointer<QuotaIconWidget> m_iconWidget;
    QScopedPointer<QuotaPanelWidget> m_panelWidget;
    QScopedPointer<CodexAppServerClient> m_client;
    QLabel *m_tipsLabel = nullptr; // 由框架展示的悬停提示
    QString m_message;             // 最近一次状态提示（读取失败等原因）
    QString m_tipNote;             // 双击打开桌面版失败时的临时提示
    QTimer m_tipNoteTimer;         // 临时提示自动清除
};

#endif // CODEXMONITORPLUGIN_H
