// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef CODEXAPPSERVERCLIENT_H
#define CODEXAPPSERVERCLIENT_H

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QByteArray>
#include <QJsonObject>

/**
 * @brief 当前额度数据的客户端状态
 */
enum class CodexClientState {
    Starting,    // 正在读取额度
    NoCli,       // 未找到 Codex CLI
    NotLoggedIn, // Codex 尚未登录
    Failed,      // 网络异常或读取失败
    NoQuota,     // 已连接但账号没有可用额度
    Ready,       // 已取得额度数据
};

/**
 * @brief 单个额度窗口
 */
struct QuotaWindow {
    bool valid = false;
    double usedPercent = 0;  // 已用百分比，0-100
    int durationMinutes = 0; // 额度周期（分钟）
    qint64 resetsAt = 0;     // 恢复时间（Unix 秒级时间戳）
};

/**
 * @brief 当前账号额度状态
 */
struct QuotaState {
    QString planType;        // 套餐类型，如 "Plus"
    QuotaWindow primary;
    QuotaWindow secondary;
};

/**
 * @brief Codex App Server 客户端
 *
 * 单独负责：查找 Codex CLI、启动/关闭 app-server 子进程、JSON-RPC 初始化、
 * 请求并解析额度、监听额度变化通知、请求超时/进程退出处理与有限重试。
 * UI 和插件类不直接处理 JSON 或进程通信。
 */
class CodexAppServerClient : public QObject
{
    Q_OBJECT
public:
    explicit CodexAppServerClient(QObject *parent = nullptr);
    ~CodexAppServerClient() override;

    void start();   // 启动并读取一次额度
    void refresh(); // 立即请求一次额度（已有请求在途时忽略）
    void refreshManually(); // 用户点击触发的刷新：先显示"正在读取"再请求

    CodexClientState state() const { return m_state; }
    QuotaState quota() const { return m_quota; }

signals:
    void stateChanged(CodexClientState state, const QString &message);
    void quotaUpdated(const QuotaState &quota);

private slots:
    void onProcessStarted();
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onStdoutReady();
    void onReadTimeout();
    void onPeriodicRefresh();
    void onCountdownTick();

private:
    void startProcess();
    void stopProcess();
    QString findCodexBinary() const;
    void sendJson(const QJsonObject &obj);
    void handleLine(const QByteArray &line);
    void handleResponse(const QJsonObject &msg);
    void handleNotification(const QJsonObject &msg);
    void parseRateLimitObject(const QJsonObject &obj);
    QuotaWindow parseWindow(const QJsonValue &value) const;
    void setState(CodexClientState state, const QString &message = QString());
    void scheduleRestart(int delayMs);
    void handleProcessFailure(const QString &reason);

    QProcess m_process;
    QByteArray m_buffer;          // stdout 未完成行缓冲
    QTimer m_readTimeout;         // 请求 10 秒超时
    QTimer m_periodicRefresh;     // 每 5 分钟刷新
    QTimer m_countdown;           // 每分钟本地倒计时
    QTimer m_restartTimer;        // 进程异常退出后 30 秒重启
    QString m_codexPath;
    bool m_requestInFlight = false;
    bool m_stopping = false;
    CodexClientState m_state = CodexClientState::Starting;
    QuotaState m_quota;
};

#endif // CODEXAPPSERVERCLIENT_H
