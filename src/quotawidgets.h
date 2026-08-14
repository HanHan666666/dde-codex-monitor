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
 * @brief 格式化额度周期：N 分钟 / N 小时 / N 天
 */
QString formatWindowDuration(int minutes);

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
 */
class QuotaPanelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QuotaPanelWidget(QWidget *parent = nullptr);

    void setData(const QuotaState &quota, CodexClientState state, const QString &message);

    QSize sizeHint() const override { return QSize(310, 60); }

signals:
    void refreshRequested();

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

#endif // QUOTAWIDGETS_H
