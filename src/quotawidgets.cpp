// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "quotawidgets.h"

#include <QPainter>
#include <QPainterPath>
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

bool quotaWindowExhausted(const QuotaWindow &window)
{
    return window.valid && window.usedPercent >= 99.5;
}

// 实时外推的时间上限：数据长期未刷新时避免预测无限跑飞
static constexpr qint64 kMaxExtrapolateSec = 15 * 60;

LiveQuotaView liveQuotaView(const QuotaWindow &window, bool codexActive, qint64 nowSec)
{
    LiveQuotaView view;
    if (!window.valid) {
        return view;
    }
    double used = window.usedPercent;
    if (codexActive && window.burnPerHour > 0 && window.lastSampleAt > 0
        && nowSec > window.lastSampleAt) {
        const qint64 elapsed = qMin<qint64>(nowSec - window.lastSampleAt, kMaxExtrapolateSec);
        used = qMin(100.0, used + window.burnPerHour * double(elapsed) / 3600.0);
        view.advanced = true;
    }
    view.remainingPercent = 100.0 - used;
    view.depleted = used >= 99.5;
    if (window.burnPerHour > 0) {
        const qint64 minutes = qRound(view.remainingPercent / window.burnPerHour * 60.0);
        // 外推结果即时展示（哪怕很短）；静态估算沿用 >=5 分钟的展示门槛
        if (view.advanced || minutes >= 5) {
            view.minutesLeft = qMax<qint64>(0, minutes);
        }
    } else if (!view.advanced) {
        view.minutesLeft = window.estimatedMinutesLeft;
    }
    return view;
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

QString formatShortDuration(qint64 minutes)
{
    if (minutes < 0) {
        return QString();
    }
    if (minutes < 60) {
        return QStringLiteral("%1 分钟").arg(minutes);
    }
    if (minutes < 1440) {
        return QStringLiteral("%1 小时").arg(minutes / 60.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 天").arg(minutes / 1440.0, 0, 'f', 1);
}

QString pluginStateText(CodexClientState state)
{
    switch (state) {
    case CodexClientState::NoCli:
        return QStringLiteral("未找到 Codex CLI");
    case CodexClientState::NotLoggedIn:
        return QStringLiteral("Codex 尚未登录");
    case CodexClientState::NoQuota:
        return QStringLiteral("当前账号没有可用额度");
    case CodexClientState::Failed:
        return QStringLiteral("读取失败");
    case CodexClientState::Ready:
        return QStringLiteral("已连接");
    case CodexClientState::Starting:
    default:
        return QStringLiteral("正在读取");
    }
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

    // 彩色圆弧表示已用百分比（最接近上限的窗口；使用中时按速率实时外推）
    const QuotaWindow &worst = worstQuotaWindow(quota);
    double worstUsed = worst.valid ? worst.usedPercent : 0.0;
    bool exhausted = quotaWindowExhausted(worst);
    if (state == CodexClientState::Ready && worst.valid && quota.codexActive) {
        const LiveQuotaView live = liveQuotaView(worst, true, QDateTime::currentSecsSinceEpoch());
        worstUsed = 100.0 - live.remainingPercent;
        exhausted = live.depleted;
    }
    if (state == CodexClientState::Ready && worst.valid && worstUsed > 0) {
        const qreal sweep = -qBound(0.0, worstUsed, 100.0) / 100.0 * 360.0 * 16.0;
        QPen arcPen(color, penWidth, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(arcPen);
        painter.drawArc(QRectF(center.x() - radius, center.y() - radius, radius * 2, radius * 2),
                        90 * 16, qRound(sweep));
    }

    // 中间绘制简短的 C；耗尽时改为 ! 一眼区分"没额度"与"没数据"
    QFont font = painter.font();
    font.setPixelSize(qRound(side * 0.44));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(rect, Qt::AlignCenter,
                    exhausted ? QStringLiteral("!") : QStringLiteral("C"));

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
    const LiveQuotaView live = liveQuotaView(worst, m_quota.codexActive,
                                             QDateTime::currentSecsSinceEpoch());
    if (live.depleted) {
        return QStringLiteral("已耗尽");
    }
    // 明确显示"剩余"，避免歧义；使用中时按速率实时外推
    return QStringLiteral("剩余 %1%").arg(qRound(live.remainingPercent));
}

// "剩余 X% [· 还能用约 Y] · 恢复倒计时"（使用中时按速率实时外推）
static QString windowDetailText(const QuotaWindow &window, bool codexActive)
{
    const LiveQuotaView live = liveQuotaView(window, codexActive,
                                             QDateTime::currentSecsSinceEpoch());
    QStringList parts;
    if (live.depleted) {
        parts << QStringLiteral("已耗尽");
    } else {
        parts << QStringLiteral("剩余 %1%").arg(qRound(live.remainingPercent));
    }
    if (live.minutesLeft == 0) {
        parts << QStringLiteral("即将耗尽");
    } else if (live.minutesLeft > 0) {
        parts << QStringLiteral("还能用约 %1").arg(formatShortDuration(live.minutesLeft));
    }
    parts << formatRelativeReset(window.resetsAt);
    return parts.join(QStringLiteral(" · "));
}

QStringList QuotaPanelWidget::windowLines() const
{
    QStringList lines;
    if (m_state == CodexClientState::Ready) {
        const bool twoWindows = m_quota.primary.valid && m_quota.secondary.valid;
        if (m_quota.primary.valid) {
            const QString duration = formatWindowDuration(m_quota.primary.durationMinutes);
            if (twoWindows) {
                lines << QStringLiteral("%1：%2")
                             .arg(duration.isEmpty() ? QStringLiteral("窗口") : duration,
                                  windowDetailText(m_quota.primary, m_quota.codexActive));
            } else {
                // 单窗口：右侧大数字已是该窗口的剩余百分比，行内不重复
                const LiveQuotaView live = liveQuotaView(m_quota.primary, m_quota.codexActive,
                                                         QDateTime::currentSecsSinceEpoch());
                QStringList parts;
                if (live.minutesLeft == 0) {
                    parts << QStringLiteral("即将耗尽");
                } else if (live.minutesLeft > 0) {
                    parts << QStringLiteral("还能用约 %1").arg(formatShortDuration(live.minutesLeft));
                }
                parts << formatRelativeReset(m_quota.primary.resetsAt);
                lines << QStringLiteral("%1 · %2")
                             .arg(duration.isEmpty() ? QStringLiteral("额度窗口") : duration + QStringLiteral("额度"),
                                  parts.join(QStringLiteral(" · ")));
            }
        }
        if (m_quota.secondary.valid) {
            const QString duration = formatWindowDuration(m_quota.secondary.durationMinutes);
            lines << QStringLiteral("%1：%2")
                         .arg(duration.isEmpty() ? QStringLiteral("窗口") : duration,
                              windowDetailText(m_quota.secondary, m_quota.codexActive));
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
    const qreal bottom = 10; // 底部留出进度条空间

    // 左侧状态图标
    const qreal iconSize = 24;
    const QRectF iconRect(left, (height() - iconSize) / 2.0, iconSize, iconSize);
    drawQuotaStatusIcon(painter, iconRect, m_state, m_quota);

    // 文本区域（右侧为详情箭头预留空间）
    const qreal textX = left + iconSize + 12;
    const qreal textWidth = width() - textX - right - QuotaPanelWidget::detailTriggerWidth();

    // 标题行：套餐类型 + 右侧剩余百分比
    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(10.0);
    QFontMetrics titleMetrics(titleFont);

    // 窗口信息行
    QFont lineFont = font();
    lineFont.setPointSizeF(9.0);
    QFontMetrics lineMetrics(lineFont);
    const QStringList lines = windowLines();

    // 双窗口或字体偏高时文本可能超出可用高度，先压行距、再逐级缩小
    // 行/标题字号（下限 7/8.5pt），实在放不下由上方裁剪兜底，避免与进度条重叠
    qreal spacing = 2;
    const qreal availHeight = height() - top - bottom;
    while (lines.size() > 0
           && titleMetrics.height() + (lineMetrics.height() + spacing) * lines.size() > availHeight
           && (spacing > 0 || lineFont.pointSizeF() > 7.0 || titleFont.pointSizeF() > 8.5)) {
        if (spacing > 0) {
            spacing -= 0.5;
        } else if (lineFont.pointSizeF() > 7.0) {
            lineFont.setPointSizeF(lineFont.pointSizeF() - 0.5);
            lineMetrics = QFontMetrics(lineFont);
        } else {
            titleFont.setPointSizeF(titleFont.pointSizeF() - 0.25);
            titleMetrics = QFontMetrics(titleFont);
        }
    }

    QString title = QStringLiteral("Codex 额度");
    if (m_state == CodexClientState::Ready && !m_quota.planType.isEmpty()) {
        title = QStringLiteral("Codex ") + m_quota.planType;
    }
    const QString usage = usageText();
    const int usageWidth = usage.isEmpty() ? 0 : titleMetrics.horizontalAdvance(usage);

    const int titleHeight = titleMetrics.height();
    const int lineHeight = lineMetrics.height();
    const qreal blockHeight = titleHeight + spacing * qMax(0, lines.size()) + lineHeight * lines.size();
    const qreal blockTop = top + qMax(0.0, (height() - top - bottom - blockHeight) / 2.0);

    // 文本绘制裁剪到进度条上方：字号压到下限仍放不下时宁可截断也不与进度条重叠
    painter.save();
    painter.setClipRect(QRect(0, 0, width(), height() - 8));

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
    painter.restore();

    // 底部剩余额度进度条：填充比例与"剩余"文案一致，颜色同状态图标
    const qreal barHeight = 3;
    const QRectF trackRect(left, height() - 8, width() - left - right, barHeight);
    QColor trackColor = infoColor;
    trackColor.setAlphaF(0.4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(trackRect, barHeight / 2, barHeight / 2);
    const QuotaWindow &worstBar = worstQuotaWindow(m_quota);
    if (m_state == CodexClientState::Ready && worstBar.valid) {
        const LiveQuotaView liveBar = liveQuotaView(worstBar, m_quota.codexActive,
                                                    QDateTime::currentSecsSinceEpoch());
        const double remaining = qBound(0.0, liveBar.remainingPercent, 100.0);
        const qreal fillWidth = trackRect.width() * remaining / 100.0;
        if (fillWidth >= 1) {
            painter.setBrush(codexStatusColor(m_state, m_quota));
            painter.drawRoundedRect(QRectF(trackRect.left(), trackRect.y(), fillWidth, barHeight),
                                    barHeight / 2, barHeight / 2);
        }
    }

    // 右侧详情入口箭头
    QFont arrowFont = font();
    arrowFont.setBold(true);
    arrowFont.setPointSizeF(14.0);
    painter.setFont(arrowFont);
    painter.setPen(infoColor);
    painter.drawText(QRectF(width() - QuotaPanelWidget::detailTriggerWidth(), 0,
                            QuotaPanelWidget::detailTriggerWidth(), height()),
                     Qt::AlignCenter, QStringLiteral("›"));
}

void QuotaPanelWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (event->pos().x() >= width() - QuotaPanelWidget::detailTriggerWidth() - 8) {
            emit detailRequested();
        } else {
            emit refreshRequested();
        }
    }
    QWidget::mousePressEvent(event);
}

// ---------- QuotaDetailWidget ----------

namespace {
// 趋势曲线配色：与状态色无关的稳定区分色，亮暗主题均可读
const QColor kSeriesPrimary(0x4F, 0x8F, 0xF7); // 主窗口：蓝
const QColor kSeriesSecondary(0x9B, 0x7B, 0xF5); // 次窗口：紫
}

QuotaDetailWidget::QuotaDetailWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    // 创建即固定 310x400：loader 用 QML 表面尺寸强推 setFixedSize，
    // 控件自己提交的初始尺寸是嵌入链路第一环，必须一开始就正确。
    // loader 后续覆盖为其他尺寸时由 paintEvent 自适应布局兜底。
    setFixedSize(310, 400);
}

void QuotaDetailWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // 装机后从 journal 确认 dock 实际推给子页的尺寸（验证 loader 强推假设）
    qWarning() << "[dde-codex-monitor] detail resized:" << size()
               << "sizeHint:" << sizeHint();
}

void QuotaDetailWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    qWarning() << "[dde-codex-monitor] detail shown, size:" << size();
}

void QuotaDetailWidget::setData(const QuotaState &quota, CodexClientState state,
                                const QString &message,
                                const QVector<QuotaSample> &primarySeries,
                                const QVector<QuotaSample> &secondarySeries)
{
    m_quota = quota;
    m_state = state;
    m_message = message;
    m_primarySeries = primarySeries;
    m_secondarySeries = secondarySeries;
    update();
}

QString QuotaDetailWidget::windowSummary(const QuotaWindow &window) const
{
    const LiveQuotaView live = liveQuotaView(window, m_quota.codexActive,
                                             QDateTime::currentSecsSinceEpoch());
    QStringList parts;
    if (live.depleted) {
        parts << QStringLiteral("已耗尽");
    } else {
        parts << QStringLiteral("剩余 %1%").arg(qRound(live.remainingPercent));
    }
    if (live.minutesLeft == 0) {
        parts << QStringLiteral("即将耗尽");
    } else if (live.minutesLeft > 0) {
        parts << QStringLiteral("还能用约 %1").arg(formatShortDuration(live.minutesLeft));
    }
    parts << formatRelativeReset(window.resetsAt);
    return parts.join(QStringLiteral(" · "));
}

void QuotaDetailWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette palette = this->palette();
    const QColor textColor = palette.color(QPalette::WindowText);
    QColor dimColor = palette.color(QPalette::PlaceholderText);
    if (!dimColor.isValid()) {
        dimColor = textColor;
    }
    const QColor statusColor = codexStatusColor(m_state, m_quota);

    // 头部：套餐 + 剩余大数字
    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(13.0);
    painter.setFont(titleFont);
    painter.setPen(textColor);
    QString title = QStringLiteral("Codex 额度");
    if (m_state == CodexClientState::Ready && !m_quota.planType.isEmpty()) {
        title = QStringLiteral("Codex ") + m_quota.planType;
    }
    painter.drawText(QRectF(16, 12, 180, 26), Qt::AlignLeft | Qt::AlignVCenter, title);

    QFont usageFont = titleFont;
    usageFont.setPointSizeF(16.0);
    painter.setFont(usageFont);
    painter.setPen(m_state == CodexClientState::Ready ? statusColor : dimColor);
    QString usage;
    if (m_state == CodexClientState::Ready) {
        const QuotaWindow &worst = worstQuotaWindow(m_quota);
        const LiveQuotaView live = liveQuotaView(worst, m_quota.codexActive,
                                                 QDateTime::currentSecsSinceEpoch());
        usage = live.depleted ? QStringLiteral("已耗尽")
                              : QStringLiteral("剩余 %1%").arg(qRound(live.remainingPercent));
    } else {
        usage = pluginStateText(m_state);
    }
    painter.drawText(QRectF(width() - 16 - 140, 12, 140, 26), Qt::AlignRight | Qt::AlignVCenter, usage);

    // 高度自适应布局：loader 可能 setFixedSize 强推任意尺寸，从上往下依次排
    // 头部/窗口行/趋势图/图例/脚注，空间不足时逐级裁剪，避免任一档被裁字
    const qreal bottomMargin = 12;
    // 窗口明细行（环形图 + 说明），每行 50；头部占 12..38，留 8 间隔
    qreal y = 46;
    const auto paintWindowRow = [&](const QuotaWindow &window) {
        const QRectF ringRect(16, y + 6, 36, 36);
        drawQuotaStatusIcon(painter, ringRect, m_state, m_quota);
        QFont nameFont = font();
        nameFont.setBold(true);
        nameFont.setPointSizeF(10.5);
        painter.setFont(nameFont);
        painter.setPen(textColor);
        const QString name = formatWindowDuration(window.durationMinutes)
                .isEmpty() ? QStringLiteral("额度窗口")
                           : formatWindowDuration(window.durationMinutes) + QStringLiteral("额度");
        painter.drawText(QRectF(64, y, width() - 64 - 16 - 56, 18), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(nameFont).elidedText(name, Qt::ElideRight,
                                                           width() - 64 - 16 - 56));
        if (m_quota.codexActive) {
            // 使用中标记：绿点 + 文案（预测按当前速度实时外推的依据）
            QFont tagFont = font();
            tagFont.setPointSizeF(8.5);
            tagFont.setBold(true);
            painter.setFont(tagFont);
            const QString tag = QStringLiteral("使用中");
            const int tw = painter.fontMetrics().horizontalAdvance(tag);
            painter.setPen(Qt::NoPen);
            painter.setBrush(kGreen);
            painter.drawEllipse(QPointF(width() - 16 - tw - 10, y + 9), 2.5, 2.5);
            painter.setPen(kGreen);
            painter.drawText(QRectF(width() - 16 - tw, y, tw + 2, 18),
                             Qt::AlignLeft | Qt::AlignVCenter, tag);
        }
        QFont detailFont = font();
        detailFont.setPointSizeF(9.0);
        painter.setFont(detailFont);
        painter.setPen(dimColor);
        painter.drawText(QRectF(64, y + 18, width() - 64 - 16, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(detailFont).elidedText(windowSummary(window),
                                                             Qt::ElideRight,
                                                             width() - 64 - 16));
        y += 50;
    };
    if (m_state == CodexClientState::Ready) {
        if (m_quota.primary.valid) {
            paintWindowRow(m_quota.primary);
        }
        if (m_quota.secondary.valid) {
            paintWindowRow(m_quota.secondary);
        }
    } else {
        QFont msgFont = font();
        msgFont.setPointSizeF(9.5);
        painter.setFont(msgFont);
        painter.setPen(dimColor);
        painter.drawText(QRectF(16, y, width() - 32, 34),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                         m_message.isEmpty() ? pluginStateText(m_state) : m_message);
        y += 42;
    }

    // 趋势图：标题 20 + 间隙 6 + 图表 + 尾部(时间刻度)18 + 图例 22 + 脚注 14，
    // 全部能塞进 height() 才画；图表最小 90 高，不够就整体折叠（图例随图省略）
    const qreal chartTitleHeight = 20;
    const qreal chartTitleGap = 6;
    const qreal chartTailHeight = 18; // 图表下方 -48h/-24h/现在 刻度 + 与图例的间隔
    const qreal legendHeight = 22;
    const qreal footnoteHeight = 14;
    const qreal minChartHeight = 90;
    const qreal chartHeight = height() - bottomMargin - y - chartTitleHeight
            - chartTitleGap - chartTailHeight - legendHeight - footnoteHeight;

    if (chartHeight >= minChartHeight) {
        QFont chartTitleFont = font();
        chartTitleFont.setBold(true);
        chartTitleFont.setPointSizeF(10.0);
        painter.setFont(chartTitleFont);
        painter.setPen(textColor);
        painter.drawText(QRectF(16, y, width() - 32, chartTitleHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("最近 48 小时已用趋势"));
        y += chartTitleHeight + chartTitleGap;
        // 左侧 18px 留给纵轴刻度槽
        const QRectF chartRect(34, y, width() - 34 - 16, chartHeight);
        paintChart(painter, chartRect);
        // 图例比横轴时间标签再低一档，避免与 -48h/-24h/现在 同一水平带
        y = chartRect.bottom() + chartTailHeight;

        // 图例
        QFont legendFont = font();
        legendFont.setPointSizeF(8.5);
        painter.setFont(legendFont);
        qreal lx = 16;
        const auto paintLegend = [&](const QColor &color, const QString &label) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(lx + 4, y + 8), 3.5, 3.5);
            painter.setPen(dimColor);
            painter.drawText(QRectF(lx + 12, y, 70, 16), Qt::AlignLeft | Qt::AlignVCenter, label);
            lx += 12 + 8 + painter.fontMetrics().horizontalAdvance(label) + 14;
        };
        if (!m_primarySeries.isEmpty()) {
            paintLegend(kSeriesPrimary,
                        m_quota.primary.valid && !formatWindowDuration(m_quota.primary.durationMinutes).isEmpty()
                            ? formatWindowDuration(m_quota.primary.durationMinutes)
                            : QStringLiteral("主窗口"));
        }
        if (!m_secondarySeries.isEmpty()) {
            paintLegend(kSeriesSecondary,
                        m_quota.secondary.valid && !formatWindowDuration(m_quota.secondary.durationMinutes).isEmpty()
                            ? formatWindowDuration(m_quota.secondary.durationMinutes)
                            : QStringLiteral("次窗口"));
        }
        y += legendHeight;
    } else if (y + chartTitleHeight <= height() - bottomMargin) {
        // 高度不够画图：标题位放折叠提示，图例随图省略
        QFont chartTitleFont = font();
        chartTitleFont.setPointSizeF(9.0);
        painter.setFont(chartTitleFont);
        painter.setPen(dimColor);
        painter.drawText(QRectF(16, y, width() - 32, chartTitleHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("高度不足，趋势图已折叠"));
        y += chartTitleHeight;
    }

    // 底注（没空间就省略）
    if (y + footnoteHeight <= height() - bottomMargin) {
        QFont footFont = font();
        footFont.setPointSizeF(8.0);
        painter.setFont(footFont);
        painter.setPen(dimColor);
        painter.drawText(QRectF(16, y, width() - 32, footnoteHeight), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("数据来自本机 Codex App Server，仅本地保存 48 小时"));
    }
}

void QuotaDetailWidget::paintChart(QPainter &painter, const QRectF &rect)
{
    const QPalette palette = this->palette();
    QColor gridColor = palette.color(QPalette::PlaceholderText);
    if (!gridColor.isValid()) {
        gridColor = palette.color(QPalette::WindowText);
    }
    gridColor.setAlphaF(0.3);

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 t0 = now - QuotaHistory::kRetainSeconds;
    const auto xOf = [&](qint64 t) {
        return rect.left() + double(t - t0) / double(QuotaHistory::kRetainSeconds) * rect.width();
    };
    const auto yOf = [&](double used) {
        return rect.bottom() - qBound(0.0, used, 100.0) / 100.0 * rect.height();
    };

    // 横向网格：0/25/50/75/100
    painter.setPen(QPen(gridColor, 1));
    QFont labelFont = font();
    labelFont.setPointSizeF(7.5);
    for (int pct : { 0, 25, 50, 75, 100 }) {
        const double yy = yOf(pct);
        painter.drawLine(QPointF(rect.left(), yy), QPointF(rect.right(), yy));
    }
    // 纵轴刻度槽（图表左侧外沿），与曲线、横轴标签互不干扰
    const auto yAxisLabel = [&](int pct, const QColor &color, bool bold = false) {
        QFont f = font();
        f.setPointSizeF(7.5);
        f.setBold(bold);
        painter.setFont(f);
        painter.setPen(color);
        painter.drawText(QRectF(0, yOf(pct) - 8, rect.left() - 4, 16),
                         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1%").arg(pct));
    };
    yAxisLabel(100, gridColor);
    yAxisLabel(50, gridColor);
    yAxisLabel(0, gridColor);

    // 90% 预警线（刻度同样放左侧槽，红色醒目）
    QPen warnPen(kRed, 1, Qt::DashLine);
    painter.setPen(warnPen);
    const double y90 = yOf(90);
    painter.drawLine(QPointF(rect.left(), y90), QPointF(rect.right(), y90));
    yAxisLabel(90, kRed, true);

    // 时间刻度：-48h / -24h / 现在
    painter.setPen(gridColor);
    painter.drawText(QRectF(rect.left(), rect.bottom() + 1, 60, 9),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("-48h"));
    painter.drawText(QRectF(rect.center().x() - 30, rect.bottom() + 1, 60, 9),
                     Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("-24h"));
    painter.drawText(QRectF(rect.right() - 60, rect.bottom() + 1, 60, 9),
                     Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("现在"));

    const auto paintSeries = [&](const QVector<QuotaSample> &series, const QColor &color) {
        if (series.isEmpty()) {
            return;
        }
        // 只绘制保留窗口内的点
        QVector<QPointF> points;
        for (const QuotaSample &s : series) {
            if (s.timestamp < t0) {
                continue;
            }
            points.append(QPointF(xOf(s.timestamp), yOf(s.usedPercent)));
        }
        if (points.isEmpty()) {
            return;
        }
        if (points.size() == 1) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(points.first(), 2.5, 2.5);
            return;
        }
        QPen linePen(color, 1.8);
        linePen.setCapStyle(Qt::RoundCap);
        linePen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(linePen);
        painter.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(points.first());
        for (int i = 1; i < points.size(); ++i) {
            path.lineTo(points.at(i));
        }
        painter.drawPath(path);
    };
    paintSeries(m_primarySeries, kSeriesPrimary);
    paintSeries(m_secondarySeries, kSeriesSecondary);

    if (m_primarySeries.isEmpty() && m_secondarySeries.isEmpty()) {
        painter.setPen(gridColor);
        painter.drawText(rect, Qt::AlignCenter, QStringLiteral("暂无历史数据"));
    }
}
