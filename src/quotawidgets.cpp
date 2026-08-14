// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "quotawidgets.h"

#include <QPainter>
#include <QApplication>
#include <QFontMetrics>
#include <QDateTime>
#include <QMouseEvent>
#include <QtMath>

namespace {

// 语义颜色：在亮色和暗色主题下均可阅读
const QColor kGray(0x90, 0x93, 0x99);   // 暂无数据/读取失败
const QColor kGreen(0x2E, 0xB0, 0x6A);  // 正常
const QColor kOrange(0xF5, 0xA6, 0x23); // 接近上限
const QColor kRed(0xE6, 0x45, 0x45);    // 即将耗尽/已耗尽

} // namespace

QColor codexStatusColor(CodexClientState state, const QuotaState &quota)
{
    if (state != CodexClientState::Ready) {
        return kGray;
    }
    const QuotaWindow &worst = worstQuotaWindow(quota);
    if (!worst.valid) {
        return kGray;
    }
    if (worst.usedPercent >= 90) {
        return kRed;
    }
    if (worst.usedPercent >= 70) {
        return kOrange;
    }
    return kGreen;
}

const QuotaWindow &worstQuotaWindow(const QuotaState &quota)
{
    if (quota.secondary.valid
        && (!quota.primary.valid || quota.secondary.usedPercent > quota.primary.usedPercent)) {
        return quota.secondary;
    }
    return quota.primary;
}

QString formatWindowDuration(int minutes)
{
    if (minutes <= 0) {
        return QString();
    }
    if (minutes % 1440 == 0) {
        return QStringLiteral("%1 天").arg(minutes / 1440);
    }
    if (minutes % 60 == 0 && minutes < 1440) {
        return QStringLiteral("%1 小时").arg(minutes / 60);
    }
    if (minutes < 60) {
        return QStringLiteral("%1 分钟").arg(minutes);
    }
    return QStringLiteral("%1 分钟").arg(minutes);
}

QString formatRelativeReset(qint64 resetsAt)
{
    if (resetsAt <= 0) {
        return QStringLiteral("恢复时间未知");
    }
    const qint64 diff = resetsAt - QDateTime::currentSecsSinceEpoch();
    if (diff <= 0) {
        return QStringLiteral("已恢复");
    }
    if (diff < 3600) {
        return QStringLiteral("%1 分钟后恢复").arg(qMax<qint64>(1, diff / 60));
    }
    if (diff < 86400) {
        const qint64 hours = diff / 3600;
        const qint64 minutes = (diff % 3600) / 60;
        if (minutes == 0) {
            return QStringLiteral("%1 小时后恢复").arg(hours);
        }
        return QStringLiteral("%1 小时 %2 分后恢复").arg(hours).arg(minutes);
    }
    return QStringLiteral("%1 天后恢复").arg(diff / 86400);
}

void drawQuotaStatusIcon(QPainter &painter, const QRectF &rect,
                         CodexClientState state, const QuotaState &quota)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor color = codexStatusColor(state, quota);
    const QPointF center = rect.center();
    const qreal side = qMin(rect.width(), rect.height());
    const qreal radius = side * 0.34;
    const qreal penWidth = qMax(1.5, side * 0.10);

    // 灰色底环
    QPen basePen(kGray, penWidth);
    basePen.setCapStyle(Qt::RoundCap);
    painter.setPen(basePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius, radius);

    // 彩色圆弧表示已用百分比（最接近上限的窗口）
    const QuotaWindow &worst = worstQuotaWindow(quota);
    if (state == CodexClientState::Ready && worst.valid && worst.usedPercent > 0) {
        const qreal sweep = -qBound(0.0, worst.usedPercent, 100.0) / 100.0 * 360.0 * 16.0;
        QPen arcPen(color, penWidth, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(arcPen);
        painter.drawArc(QRectF(center.x() - radius, center.y() - radius, radius * 2, radius * 2),
                        90 * 16, qRound(sweep));
    }

    // 中间绘制简短的 C
    QFont font = painter.font();
    font.setPixelSize(qRound(side * 0.44));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(rect, Qt::AlignCenter, QStringLiteral("C"));

    painter.restore();
}

QuotaIconWidget::QuotaIconWidget(QWidget *parent)
    : QWidget(parent)
{
    m_singleClickTimer.setSingleShot(true);
    m_singleClickTimer.setInterval(QApplication::doubleClickInterval());
    connect(&m_singleClickTimer, &QTimer::timeout,
            this, &QuotaIconWidget::refreshRequested);
}

void QuotaIconWidget::setData(const QuotaState &quota, CodexClientState state)
{
    m_quota = quota;
    m_state = state;
    update();
}

void QuotaIconWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    drawQuotaStatusIcon(painter, QRectF(rect()), m_state, m_quota);
}

void QuotaIconWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 等待双击间隔：期间没有第二次点击才算单击刷新
        m_singleClickTimer.start();
    }
    QWidget::mousePressEvent(event);
}

void QuotaIconWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_singleClickTimer.stop();
        emit launchRequested();
    }
    // 默认实现会转发给 mousePressEvent，导致单击定时器被重启，这里直接接收
    event->accept();
}

QuotaPanelWidget::QuotaPanelWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
}

void QuotaPanelWidget::setData(const QuotaState &quota, CodexClientState state, const QString &message)
{
    m_quota = quota;
    m_state = state;
    m_message = message;
    update();
}

QString QuotaPanelWidget::usageText() const
{
    if (m_state != CodexClientState::Ready) {
        return QString();
    }
    const QuotaWindow &worst = worstQuotaWindow(m_quota);
    if (!worst.valid) {
        return QString();
    }
    // 明确显示"剩余"，避免歧义；数值 = 100 - 已用
    return QStringLiteral("剩余 %1%").arg(qRound(100.0 - worst.usedPercent));
}

QStringList QuotaPanelWidget::windowLines() const
{
    QStringList lines;
    if (m_state == CodexClientState::Ready) {
        const bool twoWindows = m_quota.primary.valid && m_quota.secondary.valid;
        if (m_quota.primary.valid) {
            const QString duration = formatWindowDuration(m_quota.primary.durationMinutes);
            if (twoWindows) {
                lines << QStringLiteral("%1：剩余 %2% · %3")
                             .arg(duration.isEmpty() ? QStringLiteral("窗口") : duration,
                                  QString::number(qRound(100.0 - m_quota.primary.usedPercent)),
                                  formatRelativeReset(m_quota.primary.resetsAt));
            } else {
                lines << QStringLiteral("%1 · %2")
                             .arg(duration.isEmpty() ? QStringLiteral("额度窗口") : duration + QStringLiteral("额度"),
                                  formatRelativeReset(m_quota.primary.resetsAt));
            }
        }
        if (m_quota.secondary.valid) {
            const QString duration = formatWindowDuration(m_quota.secondary.durationMinutes);
            lines << QStringLiteral("%1：剩余 %2% · %3")
                         .arg(duration.isEmpty() ? QStringLiteral("窗口") : duration,
                              QString::number(qRound(100.0 - m_quota.secondary.usedPercent)),
                              formatRelativeReset(m_quota.secondary.resetsAt));
        }
    } else {
        lines << m_message;
    }
    return lines;
}

void QuotaPanelWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 不绘制固定背景色，跟随系统亮色/暗色主题

    const QPalette palette = this->palette();
    const QColor textColor = palette.color(QPalette::WindowText);
    const QColor dimColor = palette.color(QPalette::PlaceholderText);
    const QColor infoColor = dimColor.isValid() ? dimColor : textColor;

    const qreal left = 16;
    const qreal right = 16;
    const qreal top = 8;
    const qreal bottom = 8;

    // 左侧状态图标
    const qreal iconSize = 24;
    const QRectF iconRect(left, (height() - iconSize) / 2.0, iconSize, iconSize);
    drawQuotaStatusIcon(painter, iconRect, m_state, m_quota);

    // 文本区域
    const qreal textX = left + iconSize + 12;
    const qreal textWidth = width() - textX - right;

    // 标题行：套餐类型 + 右侧剩余百分比
    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(10.0);
    painter.setFont(titleFont);
    const QFontMetrics titleMetrics(titleFont);

    QString title = QStringLiteral("Codex 额度");
    if (m_state == CodexClientState::Ready && !m_quota.planType.isEmpty()) {
        title = QStringLiteral("Codex ") + m_quota.planType;
    }
    const QString usage = usageText();
    const int usageWidth = usage.isEmpty() ? 0 : titleMetrics.horizontalAdvance(usage);

    // 窗口信息行
    QFont lineFont = font();
    lineFont.setPointSizeF(9.0);
    painter.setFont(lineFont);
    const QFontMetrics lineMetrics(lineFont);
    const QStringList lines = windowLines();

    const int titleHeight = titleMetrics.height();
    const int lineHeight = lineMetrics.height();
    const qreal spacing = 2;
    const qreal blockHeight = titleHeight + spacing * qMax(0, lines.size()) + lineHeight * lines.size();
    const qreal blockTop = top + qMax(0.0, (height() - top - bottom - blockHeight) / 2.0);

    // 标题
    painter.setPen(textColor);
    const qreal titleRight = textX + textWidth;
    const int titleMaxWidth = qMax(0, int(textWidth - (usage.isEmpty() ? 0 : usageWidth + 12)));
    const QString elidedTitle = titleMetrics.elidedText(title, Qt::ElideRight, titleMaxWidth);
    painter.setFont(titleFont);
    painter.drawText(QRectF(textX, blockTop, titleMaxWidth, titleHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);
    if (!usage.isEmpty()) {
        painter.drawText(QRectF(titleRight - usageWidth, blockTop, usageWidth, titleHeight),
                         Qt::AlignRight | Qt::AlignVCenter, usage);
    }

    // 窗口信息
    painter.setFont(lineFont);
    qreal y = blockTop + titleHeight + spacing;
    for (const QString &line : lines) {
        const QString elided = lineMetrics.elidedText(line, Qt::ElideRight, int(textWidth));
        painter.setPen(infoColor);
        painter.drawText(QRectF(textX, y, textWidth, lineHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, elided);
        y += lineHeight + spacing;
    }
}

void QuotaPanelWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit refreshRequested();
    }
    QWidget::mousePressEvent(event);
}
