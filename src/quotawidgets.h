// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef QUOTAWIDGETS_H
#define QUOTAWIDGETS_H

#include "codexappserverclient.h"

#include <QTimer>
#include <QWidget>

/**
 * @brief 根据状态和额度计算状态颜色（绿/橙/红/灰）
 */
QColor codexStatusColor(CodexClientState state, const QuotaState &quota);

/**
 * @brief 已用比例最高的窗口（决定图标状态）；没有有效窗口时返回 primary
 */
const QuotaWindow &worstQuotaWindow(const QuotaState &quota);

/**
 * @brief 判断窗口是否已耗尽（已用比例打满）
 */
bool quotaWindowExhausted(const QuotaWindow &window);

/**
 * @brief "实时剩余"视图
 *
 * 检测到 Codex 会话正在运行时，按最近消耗速度从最后一次采样外推
 * （外推跨度上限 15 分钟），解决"盯着看的时候数据还没刷新"的滞后；
 * 未在使用时退回静态数值（用户停止使用时冻结是可接受的）。
 */
struct LiveQuotaView {
    double remainingPercent = -1; // <0 无效
    qint64 minutesLeft = -1;      // <0 未知，不展示
    bool advanced = false;        // 是否做了实时外推
    bool depleted = false;        // 外推后已耗尽
};
LiveQuotaView liveQuotaView(const QuotaWindow &window, bool codexActive, qint64 nowSec);

/**
 * @brief 格式化额度周期：N 分钟 / N 小时 / N 天
 */
QString formatWindowDuration(int minutes);

/**
 * @brief 简短时长格式：N 分钟 / N.N 小时 / N.N 天（用于"还能用约 X"）
 */
QString formatShortDuration(qint64 minutes);

/**
 * @brief 客户端状态的简短文案（详情页/非 Ready 状态展示用）
 */
QString pluginStateText(CodexClientState state);

/**
 * @brief 格式化恢复时间（相对时间，本地倒计时）
 */
QString formatRelativeReset(qint64 resetsAt);

/**
 * @brief 绘制额度状态图标（灰色底环 + 彩色已用圆弧 + 字母 C）
 */
void drawQuotaStatusIcon(QPainter &painter, const QRectF &rect,
                         CodexClientState state, const QuotaState &quota);

/**
 * @brief Dock 常驻状态图标（24x24，QPainter 绘制，不依赖 SVG/DCI 资源）
 */
class QuotaIconWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QuotaIconWidget(QWidget *parent = nullptr);

    void setData(const QuotaState &quota, CodexClientState state);

    QSize sizeHint() const override { return QSize(24, 24); }

signals:
    void refreshRequested();
    void launchRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QuotaState m_quota;
    CodexClientState m_state = CodexClientState::Starting;
    // 单击延迟到双击间隔结束后生效，双击则取消刷新改为打开桌面版
    QTimer m_singleClickTimer;
};

/**
 * @brief 快捷面板整行额度卡片（固定高度，不锁定宽度）
 *
 * 点击卡片主体手动刷新，点击右侧箭头区域打开详情页。
 */
class QuotaPanelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QuotaPanelWidget(QWidget *parent = nullptr);

    void setData(const QuotaState &quota, CodexClientState state, const QString &message);

    QSize sizeHint() const override { return QSize(310, 60); }

    static int detailTriggerWidth() { return 36; } // 右侧箭头可点击区宽度

signals:
    void refreshRequested();
    void detailRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QStringList windowLines() const;
    QString usageText() const;

    QuotaState m_quota;
    CodexClientState m_state = CodexClientState::Starting;
    QString m_message;
};

/**
 * @brief 快捷面板详情子页面：双窗口环形图 + 48 小时已用趋势曲线
 */
class QuotaDetailWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QuotaDetailWidget(QWidget *parent = nullptr);

    void setData(const QuotaState &quota, CodexClientState state, const QString &message,
                 const QVector<QuotaSample> &primarySeries,
                 const QVector<QuotaSample> &secondarySeries);

    QSize sizeHint() const override { return QSize(310, 400); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void paintChart(QPainter &painter, const QRectF &rect);
    QString windowSummary(const QuotaWindow &window) const;

    QuotaState m_quota;
    CodexClientState m_state = CodexClientState::Starting;
    QString m_message;
    QVector<QuotaSample> m_primarySeries;
    QVector<QuotaSample> m_secondarySeries;
};

#endif // QUOTAWIDGETS_H
