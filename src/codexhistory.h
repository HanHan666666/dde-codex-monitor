// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef CODEXHISTORY_H
#define CODEXHISTORY_H

#include <QMap>
#include <QString>
#include <QVector>

/**
 * @brief 单个额度采样点
 */
struct QuotaSample {
    qint64 timestamp = 0;  // Unix 秒
    double usedPercent = 0; // 已用百分比，0-100
};

/**
 * @brief 额度历史与消耗速度估算
 *
 * 按额度窗口时长（分钟）分组保存采样点，持久化到
 * $XDG_DATA_HOME/dde-codex-monitor/history.json（默认 ~/.local/share/…），
 * 仅保留最近 48 小时。检测到窗口滚动（已用比例骤降）时清空该窗口旧样本，
 * 保证估算始终基于同一窗口周期。
 */
class QuotaHistory
{
public:
    static constexpr qint64 kRetainSeconds = 48 * 3600;    // 保留 48 小时
    static constexpr qint64 kMinSampleGapSec = 60;         // 同窗口采样最小间隔
    static constexpr int kMinEstimateSamples = 3;          // 估算最少样本数
    static constexpr qint64 kMinEstimateSpanSec = 10 * 60; // 估算需要至少 10 分钟跨度
    static constexpr qint64 kEstimateWindowSec = 2 * 3600; // 估算只使用最近 2 小时样本

    /**
     * @brief 记录一次采样；窗口滚动时返回 false 表示序列被重置
     */
    bool record(int durationMinutes, qint64 timestamp, double usedPercent);

    QVector<QuotaSample> series(int durationMinutes) const;

    /**
     * @brief 最近消耗速度（%/小时），优先取最近 30 分钟窗口，样本不足退回 2 小时
     *
     * 无法得到有意义的正斜率时返回 0。
     */
    double burnRatePerHour(int durationMinutes, qint64 nowSec) const;

    /**
     * @brief 用最近样本的线性回归估算"距耗尽还有多少分钟"
     *
     * 样本不足、消耗速度接近零、或估算结果晚于窗口重置时间时
     * 返回 -1（表示未知，不应展示）。
     */
    qint64 estimateMinutesLeft(int durationMinutes, double usedPercent,
                               qint64 resetsAt, qint64 nowSec) const;

    QString filePath() const;
    bool load();
    bool save() const;

private:
    void prune(qint64 nowSec);

    // durationMinutes -> 采样点（按时间升序）
    QMap<int, QVector<QuotaSample>> m_series;
};

#endif // CODEXHISTORY_H
