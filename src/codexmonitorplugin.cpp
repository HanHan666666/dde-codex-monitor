// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "codexmonitorplugin.h"

#include "codexappserverclient.h"
#include "codexdesktoplauncher.h"
#include "codexnotifier.h"
#include "quotawidgets.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDBusConnection>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPalette>

// 恢复/预警通知的触发阈值
static const double kWarnOrangePercent = 70.0;
static const double kWarnRedPercent = 90.0;
static const double kRecoveredBelowPercent = 80.0;

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
    m_notifier.reset(new CodexNotifier(this));
    m_notifier->setEnabled(m_proxyInter->getValue(this, QStringLiteral("notifyEnabled"), true).toBool());
    m_iconWidget.reset(new QuotaIconWidget);
    m_panelWidget.reset(new QuotaPanelWidget);
    m_detailWidget.reset(new QuotaDetailWidget);

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
                updateDetailData();
                m_proxyInter->itemUpdate(this, QStringLiteral("codex-monitor"));
            });
    connect(m_client.data(), &CodexAppServerClient::quotaUpdated,
            this, &CodexMonitorPlugin::onQuotaUpdated);

    // 点击卡片或托盘图标：手动刷新（带"正在读取"反馈）
    connect(m_panelWidget.data(), &QuotaPanelWidget::refreshRequested,
            m_client.data(), &CodexAppServerClient::refreshManually);
    connect(m_iconWidget.data(), &QuotaIconWidget::refreshRequested,
            m_client.data(), &CodexAppServerClient::refreshManually);
    // 双击托盘图标：打开 Codex 桌面版
    connect(m_iconWidget.data(), &QuotaIconWidget::launchRequested,
            this, &CodexMonitorPlugin::openCodexDesktop);
    // 点击卡片右侧箭头：打开详情子页面
    connect(m_panelWidget.data(), &QuotaPanelWidget::detailRequested,
            this, &CodexMonitorPlugin::showDetail);

    m_tipNoteTimer.setSingleShot(true);
    m_tipNoteTimer.setInterval(5 * 1000);
    connect(&m_tipNoteTimer, &QTimer::timeout, this, [this]() {
        m_tipNote.clear();
        updateTipsText();
    });

    // 挂起恢复后立即刷新一次，避免等 5 分钟周期
    QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"),
            QStringLiteral("PrepareForSleep"),
            this, SLOT(onPrepareForSleep(bool)));

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

QWidget *CodexMonitorPlugin::itemPopupApplet(const QString &itemKey)
{
    if (itemKey == QStringLiteral("codex-monitor")
        || itemKey == Dock::QUICK_ITEM_KEY) {
        return m_detailWidget.data();
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
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (quota.primary.valid) {
            const LiveQuotaView live = liveQuotaView(quota.primary, quota.codexActive, now);
            parts << (live.depleted ? QStringLiteral("已耗尽")
                                     : QStringLiteral("剩余 %1%").arg(qRound(live.remainingPercent)))
                  << formatRelativeReset(quota.primary.resetsAt);
        }
        if (quota.secondary.valid) {
            const QString duration = formatWindowDuration(quota.secondary.durationMinutes);
            const LiveQuotaView live = liveQuotaView(quota.secondary, quota.codexActive, now);
            const QString pct = live.depleted ? QStringLiteral("已耗尽")
                                              : QStringLiteral("剩余 %1%").arg(qRound(live.remainingPercent));
            parts << (duration.isEmpty() ? pct : QStringLiteral("%1 %2").arg(duration, pct))
                  << formatRelativeReset(quota.secondary.resetsAt);
        }
        if (parts.isEmpty()) {
            parts << pluginDisplayName();
        }
        if (quota.codexActive) {
            parts << QStringLiteral("使用中");
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

void CodexMonitorPlugin::showDetail()
{
    updateDetailData();
    m_proxyInter->requestSetAppletVisible(this, QStringLiteral("codex-monitor"), true);
}

void CodexMonitorPlugin::updateDetailData()
{
    if (!m_detailWidget || !m_client) {
        return;
    }
    const QuotaState &quota = m_client->quota();
    const CodexClientState state = m_client->state();
    const QuotaHistory &history = m_client->history();
    const QVector<QuotaSample> primary = quota.primary.valid
            ? history.series(quota.primary.durationMinutes) : QVector<QuotaSample>();
    const QVector<QuotaSample> secondary = quota.secondary.valid
            ? history.series(quota.secondary.durationMinutes) : QVector<QuotaSample>();
    m_detailWidget->setData(quota, state, m_message, primary, secondary);
}

void CodexMonitorPlugin::onQuotaUpdated(const QuotaState &quota)
{
    m_iconWidget->setData(quota, m_client->state());
    m_panelWidget->setData(quota, m_client->state(), m_message);
    updateTipsText();
    updateDetailData();
    m_proxyInter->itemUpdate(this, QStringLiteral("codex-monitor"));
    evaluateTransitions(quota);
}

void CodexMonitorPlugin::evaluateTransitions(const QuotaState &quota)
{
    const QuotaWindow &worst = worstQuotaWindow(quota);
    if (!worst.valid) {
        m_prevQuota = quota;
        m_hasPrevQuota = true;
        return;
    }

    if (m_hasPrevQuota) {
        const QuotaWindow &prevWorst = worstQuotaWindow(m_prevQuota);
        const QString windowName = formatWindowDuration(worst.durationMinutes).isEmpty()
                ? QStringLiteral("额度") : formatWindowDuration(worst.durationMinutes);
        if (quotaWindowExhausted(prevWorst) && worst.usedPercent <= kRecoveredBelowPercent) {
            // 恢复：上一刻已耗尽，现在明显回落
            m_notifier->notify(QStringLiteral("Codex 额度已恢复"),
                               QStringLiteral("%1额度现在剩余 %2%。")
                                   .arg(windowName, QString::number(qRound(100.0 - worst.usedPercent))),
                               1, true);
        } else if (prevWorst.usedPercent < kWarnRedPercent
                   && worst.usedPercent >= kWarnRedPercent) {
            m_notifier->notify(QStringLiteral("Codex 额度即将耗尽"),
                               QStringLiteral("%1额度已用 %2%，%3。")
                                   .arg(windowName, QString::number(qRound(worst.usedPercent)),
                                        formatRelativeReset(worst.resetsAt)),
                               2, false);
        } else if (prevWorst.usedPercent < kWarnOrangePercent
                   && worst.usedPercent >= kWarnOrangePercent) {
            m_notifier->notify(QStringLiteral("Codex 额度接近上限"),
                               QStringLiteral("%1额度已用 %2%，注意安排用量。")
                                   .arg(windowName, QString::number(qRound(worst.usedPercent))),
                               1, false);
        }
    }
    m_prevQuota = quota;
    m_hasPrevQuota = true;

    // 耗尽状态变化时通知任务栏（激活态高亮）
    const bool exhausted = quotaWindowExhausted(worst);
    if (int(exhausted) != m_lastActiveState) {
        m_lastActiveState = int(exhausted);
        sendItemActiveState(exhausted);
    }
}

void CodexMonitorPlugin::sendItemActiveState(bool active)
{
    if (!m_messageCallback) {
        return;
    }
    QJsonObject data;
    data.insert(QStringLiteral("itemActiveState"), active);
    QJsonObject msg;
    msg.insert(QStringLiteral("msgType"), QStringLiteral("itemActiveState"));
    msg.insert(QStringLiteral("data"), data);
    m_messageCallback(this, QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

QString CodexMonitorPlugin::shareText() const
{
    QStringList parts;
    const QuotaState &quota = m_client->quota();
    if (m_client->state() == CodexClientState::Ready) {
        if (!quota.planType.isEmpty()) {
            parts << QStringLiteral("Codex ") + quota.planType;
        }
        const auto describe = [&parts](const QuotaWindow &window) {
            if (!window.valid) {
                return;
            }
            const QString duration = formatWindowDuration(window.durationMinutes);
            QStringList item;
            item << (duration.isEmpty() ? QStringLiteral("额度") : duration)
                 << QStringLiteral("剩余 %1%").arg(qRound(100.0 - window.usedPercent))
                 << formatRelativeReset(window.resetsAt);
            parts << item.join(QStringLiteral(" "));
        };
        describe(quota.primary);
        describe(quota.secondary);
    } else {
        parts << pluginDisplayName() << pluginStateText(m_client->state());
    }
    return parts.join(QStringLiteral(" · "));
}

bool CodexMonitorPlugin::notifyEnabled() const
{
    return m_notifier->enabled();
}

void CodexMonitorPlugin::setNotifyEnabled(bool enabled)
{
    m_notifier->setEnabled(enabled);
    m_proxyInter->saveValue(this, QStringLiteral("notifyEnabled"), enabled);
}

const QString CodexMonitorPlugin::itemContextMenu(const QString &itemKey)
{
    Q_UNUSED(itemKey)
    QList<QVariant> items;

    QMap<QString, QVariant> detail;
    detail[QStringLiteral("itemId")] = QStringLiteral("detail");
    detail[QStringLiteral("itemText")] = QStringLiteral("查看详情");
    detail[QStringLiteral("isCheckable")] = false;
    detail[QStringLiteral("isActive")] = true;
    items.push_back(detail);

    QMap<QString, QVariant> refresh;
    refresh[QStringLiteral("itemId")] = QStringLiteral("refresh");
    refresh[QStringLiteral("itemText")] = QStringLiteral("立即刷新");
    refresh[QStringLiteral("isCheckable")] = false;
    refresh[QStringLiteral("isActive")] = true;
    items.push_back(refresh);

    QMap<QString, QVariant> open;
    open[QStringLiteral("itemId")] = QStringLiteral("open");
    open[QStringLiteral("itemText")] = QStringLiteral("打开 Codex 桌面版");
    open[QStringLiteral("isCheckable")] = false;
    open[QStringLiteral("isActive")] = true;
    items.push_back(open);

    QMap<QString, QVariant> copy;
    copy[QStringLiteral("itemId")] = QStringLiteral("copy");
    copy[QStringLiteral("itemText")] = QStringLiteral("复制额度信息");
    copy[QStringLiteral("isCheckable")] = false;
    copy[QStringLiteral("isActive")] = true;
    items.push_back(copy);

    QMap<QString, QVariant> notify;
    notify[QStringLiteral("itemId")] = QStringLiteral("notify");
    notify[QStringLiteral("itemText")] = QStringLiteral("额度通知");
    notify[QStringLiteral("isCheckable")] = true;
    notify[QStringLiteral("isActive")] = true;
    notify[QStringLiteral("checked")] = notifyEnabled();
    items.push_back(notify);

    QMap<QString, QVariant> menu;
    menu[QStringLiteral("items")] = items;
    menu[QStringLiteral("checkableMenu")] = false;
    menu[QStringLiteral("singleCheck")] = false;
    return QJsonDocument::fromVariant(menu).toJson();
}

void CodexMonitorPlugin::invokedMenuItem(const QString &itemKey,
                                         const QString &menuId, const bool checked)
{
    Q_UNUSED(itemKey)
    if (menuId == QStringLiteral("detail")) {
        showDetail();
    } else if (menuId == QStringLiteral("refresh")) {
        m_client->refreshManually();
    } else if (menuId == QStringLiteral("open")) {
        openCodexDesktop();
    } else if (menuId == QStringLiteral("copy")) {
        QApplication::clipboard()->setText(shareText());
    } else if (menuId == QStringLiteral("notify")) {
        setNotifyEnabled(checked);
    }
}

void CodexMonitorPlugin::setMessageCallback(MessageCallbackFunc callback)
{
    m_messageCallback = callback;
    // 回调就绪后上报一次当前激活态
    if (m_lastActiveState >= 0) {
        sendItemActiveState(m_lastActiveState == 1);
    }
}

QString CodexMonitorPlugin::message(const QString &msg)
{
    const QJsonObject msgObj = QJsonDocument::fromJson(msg.toUtf8()).object();
    const QString msgType = msgObj.value(QStringLiteral("msgType")).toString();

    QJsonObject reply;
    if (msgType == QStringLiteral("getSupportFlag")) {
        QJsonObject data;
        data.insert(QStringLiteral("supportFlag"), true);
        reply.insert(QStringLiteral("msgType"), msgType);
        reply.insert(QStringLiteral("data"), data);
        return QJsonDocument(reply).toJson(QJsonDocument::Compact);
    }
    if (msgType == QStringLiteral("whetherWantToBeLoaded")) {
        QJsonObject data;
        data.insert(QStringLiteral("whetherWantToBeLoaded"), true);
        reply.insert(QStringLiteral("msgType"), msgType);
        reply.insert(QStringLiteral("data"), data);
        return QJsonDocument(reply).toJson(QJsonDocument::Compact);
    }
    if (msgType == QStringLiteral("pluginProperty")) {
        QJsonObject data;
        data.insert(QStringLiteral("needChameleon"), false);
        reply.insert(QStringLiteral("msgType"), msgType);
        reply.insert(QStringLiteral("data"), data);
        return QJsonDocument(reply).toJson(QJsonDocument::Compact);
    }
    return QStringLiteral("{}");
}

void CodexMonitorPlugin::onPrepareForSleep(bool aboutToSuspend)
{
    if (!aboutToSuspend) {
        m_client->refresh(); // 唤醒后立即刷新一次
    }
}

Dock::PluginFlags CodexMonitorPlugin::flags() const
{
    // 快捷面板整行插件；第一版不添加 Attribute_CanSetting / Attribute_Normal
    return Dock::Type_Quick | Dock::Quick_Panel_Full;
}
