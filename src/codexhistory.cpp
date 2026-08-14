// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "codexhistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

// 低于该消耗速度（%/秒，约 0.18 %/小时）视为没有在消耗
static const double kMinSlopePerSec = 5e-5;

bool QuotaHistory::record(int durationMinutes, qint64 timestamp, double usedPercent)
{
    if (durationMinutes <= 0 || timestamp <= 0) {
        return true;
    }
    QVector<QuotaSample> &samples = m_series[durationMinutes];
    bool rolled = false;
    if (!samples.isEmpty()) {
        const QuotaSample &last = samples.last();
        if (usedPercent < last.usedPercent - 30.0) {
            // 已用比例骤降：窗口已滚动，旧样本属于上一周期
            samples.clear();
            rolled = true;
        } else if (timestamp - last.timestamp < kMinSampleGapSec) {
            // 间隔过近（服务端推送密集时）：用最新值替换
            samples.last() = QuotaSample{ timestamp, usedPercent };
            return rolled;
        }
    }
    samples.append(QuotaSample{ timestamp, usedPercent });
    prune(timestamp);
    return rolled;
}

QVector<QuotaSample> QuotaHistory::series(int durationMinutes) const
{
    return m_series.value(durationMinutes);
}

// 对最近 windowSec 内样本做最小二乘，返回 %/秒 的正斜率；不可用时返回 0
static double slopeOfSeries(const QVector<QuotaSample> &all, qint64 nowSec, qint64 windowSec)
{
    QVector<QuotaSample> recent;
    for (int i = all.size() - 1; i >= 0; --i) {
        if (nowSec - all[i].timestamp > windowSec) {
            break;
        }
        recent.prepend(all[i]);
    }
    if (recent.size() < QuotaHistory::kMinEstimateSamples) {
        return 0;
    }
    const qint64 span = recent.last().timestamp - recent.first().timestamp;
    if (span < QuotaHistory::kMinEstimateSpanSec) {
        return 0;
    }
    double sumT = 0, sumU = 0, sumTT = 0, sumTU = 0;
    const int n = recent.size();
    for (const QuotaSample &s : recent) {
        sumT += double(s.timestamp);
        sumU += s.usedPercent;
        sumTT += double(s.timestamp) * double(s.timestamp);
        sumTU += double(s.timestamp) * s.usedPercent;
    }
    const double denom = n * sumTT - sumT * sumT;
    if (std::abs(denom) < 1e-9) {
        return 0;
    }
    const double slope = (n * sumTU - sumT * sumU) / denom;
    return slope > kMinSlopePerSec ? slope : 0;
}

double QuotaHistory::burnRatePerHour(int durationMinutes, qint64 nowSec) const
{
    // 优先最近 30 分钟（爆发期更能反映"当前速度"），样本不足退回 2 小时
    const QVector<QuotaSample> &all = m_series.value(durationMinutes);
    double slope = slopeOfSeries(all, nowSec, 30 * 60);
    if (slope <= 0) {
        slope = slopeOfSeries(all, nowSec, kEstimateWindowSec);
    }
    return slope > 0 ? slope * 3600.0 : 0.0;
}

qint64 QuotaHistory::estimateMinutesLeft(int durationMinutes, double usedPercent,
                                         qint64 resetsAt, qint64 nowSec) const
{
    const double burnPerHour = burnRatePerHour(durationMinutes, nowSec);
    if (burnPerHour <= 0) {
        return -1; // 没有可观测的消耗
    }
    const double secondsLeft = (100.0 - usedPercent) / burnPerHour * 3600.0;
    const qint64 minutesLeft = qRound(secondsLeft / 60.0);
    if (minutesLeft < 5) {
        return -1; // 太短没有指导意义
    }
    // 估算耗尽时间晚于窗口重置：额度会先重置，直接看重置倒计时即可
    if (resetsAt > nowSec && secondsLeft > double(resetsAt - nowSec)) {
        return -1;
    }
    return minutesLeft;
}

QString QuotaHistory::filePath() const
{
    // XDG：采样历史属于运行时数据，放 $XDG_DATA_HOME（默认 ~/.local/share）下的
    // 插件自有目录。不用 AppConfigLocation——宿主进程是 dde-shell，那会写进宿主目录。
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.local/share");
    }
    return base + QStringLiteral("/dde-codex-monitor/history.json");
}

bool QuotaHistory::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() != 1) {
        return false;
    }
    m_series.clear();
    const QJsonArray windows = root.value(QStringLiteral("windows")).toArray();
    for (const QJsonValue &w : windows) {
        const QJsonObject obj = w.toObject();
        const int duration = obj.value(QStringLiteral("durationMinutes")).toInt();
        if (duration <= 0) {
            continue;
        }
        QVector<QuotaSample> samples;
        const QJsonArray arr = obj.value(QStringLiteral("samples")).toArray();
        samples.reserve(arr.size());
        for (const QJsonValue &v : arr) {
            const QJsonArray pair = v.toArray();
            if (pair.size() == 2) {
                samples.append(QuotaSample{ pair.at(0).toVariant().toLongLong(),
                                            pair.at(1).toDouble() });
            }
        }
        if (!samples.isEmpty()) {
            m_series.insert(duration, samples);
        }
    }
    prune(QDateTime::currentSecsSinceEpoch());
    return true;
}

bool QuotaHistory::save() const
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    QJsonArray windows;
    for (auto it = m_series.constBegin(); it != m_series.constEnd(); ++it) {
        QJsonObject obj;
        obj.insert(QStringLiteral("durationMinutes"), it.key());
        QJsonArray arr;
        for (const QuotaSample &s : it.value()) {
            arr.append(QJsonArray{ double(s.timestamp), s.usedPercent });
        }
        obj.insert(QStringLiteral("samples"), arr);
        windows.append(obj);
    }
    root.insert(QStringLiteral("windows"), windows);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

void QuotaHistory::prune(qint64 nowSec)
{
    for (auto it = m_series.begin(); it != m_series.end(); ++it) {
        QVector<QuotaSample> &samples = it.value();
        while (!samples.isEmpty() && nowSec - samples.first().timestamp > kRetainSeconds) {
            samples.removeFirst();
        }
    }
}
