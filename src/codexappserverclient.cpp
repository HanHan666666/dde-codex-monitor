// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "codexappserverclient.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>

// 环境变量：指定 Codex CLI 路径
static const QString kCodexBinEnv = QStringLiteral("DDE_CODEX_MONITOR_CODEX_BIN");

// 协议常量
static const int kReadRequestId = 2;
static const int kReadTimeoutMs = 10 * 1000;     // 请求 10 秒超时
static const int kPeriodicRefreshMs = 5 * 60 * 1000; // 每 5 分钟刷新
static const int kCountdownTickMs = 60 * 1000;   // 每分钟更新本地倒计时
static const int kRestartDelayMs = 30 * 1000;    // 进程异常退出后 30 秒重启
static const int kNoCliRetryMs = 60 * 1000;      // 未找到 CLI 时每分钟重试

CodexAppServerClient::CodexAppServerClient(QObject *parent)
    : QObject(parent)
{
    m_history.load();

    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    // 只解析 stdout；stderr 不是 JSONL，仅用于有限的错误日志

    connect(&m_process, &QProcess::started, this, &CodexAppServerClient::onProcessStarted);
    connect(&m_process, &QProcess::errorOccurred, this, &CodexAppServerClient::onProcessErrorOccurred);
    connect(&m_process, &QProcess::finished, this, &CodexAppServerClient::onProcessFinished);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CodexAppServerClient::onStdoutReady);

    m_readTimeout.setSingleShot(true);
    m_readTimeout.setInterval(kReadTimeoutMs);
    connect(&m_readTimeout, &QTimer::timeout, this, &CodexAppServerClient::onReadTimeout);

    m_periodicRefresh.setInterval(kPeriodicRefreshMs);
    connect(&m_periodicRefresh, &QTimer::timeout, this, &CodexAppServerClient::onPeriodicRefresh);

    m_countdown.setInterval(kCountdownTickMs);
    connect(&m_countdown, &QTimer::timeout, this, &CodexAppServerClient::onCountdownTick);

    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, [this]() {
        m_stopping = false;
        startProcess();
    });
}

CodexAppServerClient::~CodexAppServerClient()
{
    stopProcess();
}

void CodexAppServerClient::start()
{
    if (m_codexPath.isEmpty()) {
        m_codexPath = findCodexBinary();
    }
    if (m_codexPath.isEmpty()) {
        setState(CodexClientState::NoCli,
                 QStringLiteral("未找到 Codex CLI，可通过环境变量 %1 指定").arg(kCodexBinEnv));
        scheduleRestart(kNoCliRetryMs);
        return;
    }
    if (m_process.state() == QProcess::NotRunning) {
        startProcess();
    } else {
        refresh();
    }
}

void CodexAppServerClient::refreshManually()
{
    if (m_requestInFlight || m_process.state() != QProcess::Running) {
        refresh();
        return;
    }
    // 先进入"正在读取"状态，给用户点击反馈；响应到达后自动恢复
    setState(CodexClientState::Starting, QStringLiteral("正在刷新额度"));
    refresh();
}

void CodexAppServerClient::refresh()
{
    if (m_requestInFlight) {
        return; // 同一时间最多保留一个额度请求
    }
    if (m_process.state() != QProcess::Running) {
        // 进程未运行时（点击重试场景）重新启动
        m_stopping = false;
        m_restartTimer.stop();
        if (m_codexPath.isEmpty()) {
            m_codexPath = findCodexBinary();
        }
        if (m_codexPath.isEmpty()) {
            setState(CodexClientState::NoCli,
                     QStringLiteral("未找到 Codex CLI，可通过环境变量 %1 指定").arg(kCodexBinEnv));
            scheduleRestart(kNoCliRetryMs);
            return;
        }
        startProcess();
        return;
    }
    m_requestInFlight = true;
    m_readTimeout.start();

    QJsonObject req;
    req.insert(QStringLiteral("method"), QStringLiteral("account/rateLimits/read"));
    req.insert(QStringLiteral("id"), kReadRequestId);
    sendJson(req);
}

QString CodexAppServerClient::findCodexBinary() const
{
    // 1. 环境变量指定
    const QString envPath = qEnvironmentVariable(kCodexBinEnv.toUtf8().constData());
    if (!envPath.isEmpty()) {
        QFileInfo info(envPath);
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
        qWarning() << "[CodexMonitor] DDE_CODEX_MONITOR_CODEX_BIN 指定的文件不可执行:" << envPath;
    }

    // 2. PATH 中查找
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("codex"));
    if (!inPath.isEmpty()) {
        return inPath;
    }

    // 3. 常见用户目录
    const QString localBin = QDir::homePath() + QStringLiteral("/.local/bin/codex");
    {
        QFileInfo info(localBin);
        if (info.isFile() && info.isExecutable()) {
            return localBin;
        }
    }

    // 4. NVM 目录中最后修改时间最新的可执行文件
    QFileInfo newest;
    const QDir nvmVersions(QDir::homePath() + QStringLiteral("/.nvm/versions/node"));
    const QStringList versions = nvmVersions.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &version : versions) {
        QFileInfo candidate(nvmVersions.filePath(version + QStringLiteral("/bin/codex")));
        if (candidate.isFile() && candidate.isExecutable()) {
            if (!newest.exists() || candidate.lastModified() > newest.lastModified()) {
                newest = candidate;
            }
        }
    }
    if (newest.exists()) {
        return newest.absoluteFilePath();
    }

    return QString();
}

void CodexAppServerClient::startProcess()
{
    if (m_stopping || m_codexPath.isEmpty()) {
        return;
    }

    // NVM 中的 Codex 脚本使用 /usr/bin/env node，需要把 bin 目录放到 PATH 最前面
    const QFileInfo codexInfo(m_codexPath);
    const QString codexBinDir = codexInfo.absolutePath();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString path = env.value(QStringLiteral("PATH"));
    if (!path.split(QLatin1Char(':')).contains(codexBinDir)) {
        env.insert(QStringLiteral("PATH"), codexBinDir + QLatin1Char(':') + path);
    }
    m_process.setProcessEnvironment(env);

    m_process.setProgram(m_codexPath);
    m_process.setArguments({ QStringLiteral("app-server"), QStringLiteral("--listen"), QStringLiteral("stdio://") });

    m_buffer.clear();
    setState(CodexClientState::Starting, QStringLiteral("正在读取额度"));
    m_process.start();
    // 进程启动失败会通过 errorOccurred 通知
}

void CodexAppServerClient::stopProcess()
{
    m_stopping = true;
    m_readTimeout.stop();
    m_requestInFlight = false;
    if (m_process.state() == QProcess::Running) {
        m_process.terminate();
        if (!m_process.waitForFinished(3000)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
}

void CodexAppServerClient::onProcessStarted()
{
    m_periodicRefresh.start();
    m_countdown.start();
    // 初始化握手
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("dde_codex_monitor"));
    clientInfo.insert(QStringLiteral("title"), QStringLiteral("DDE Codex Monitor"));
    clientInfo.insert(QStringLiteral("version"), QStringLiteral("0.1.0"));

    QJsonObject params;
    params.insert(QStringLiteral("clientInfo"), clientInfo);

    QJsonObject req;
    req.insert(QStringLiteral("method"), QStringLiteral("initialize"));
    req.insert(QStringLiteral("id"), 1);
    req.insert(QStringLiteral("params"), params);
    sendJson(req);
}

void CodexAppServerClient::onProcessErrorOccurred(QProcess::ProcessError error)
{
    if (m_stopping) {
        return;
    }
    if (error == QProcess::FailedToStart) {
        handleProcessFailure(QStringLiteral("Codex CLI 启动失败"));
    }
    // 其他错误（崩溃、读写错误）由 finished 处理
}

void CodexAppServerClient::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus)
    if (m_stopping) {
        return;
    }
    m_readTimeout.stop();
    m_requestInFlight = false;
    m_periodicRefresh.stop();
    handleProcessFailure(QStringLiteral("Codex App Server 进程退出"));
}

void CodexAppServerClient::handleProcessFailure(const QString &reason)
{
    const bool hadData = m_quota.primary.valid || m_quota.secondary.valid;
    if (hadData) {
        // 已有数据时保留显示，仅降级提示；重试由 30 秒定时器驱动
        qWarning() << "[CodexMonitor]" << reason << ",retry in" << (kRestartDelayMs / 1000) << "s";
    } else {
        setState(CodexClientState::Failed, reason + QStringLiteral("，点击重试"));
    }
    scheduleRestart(kRestartDelayMs);
}

void CodexAppServerClient::scheduleRestart(int delayMs)
{
    m_restartTimer.start(delayMs);
}

void CodexAppServerClient::sendJson(const QJsonObject &obj)
{
    const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    if (m_process.state() == QProcess::Running) {
        m_process.write(line);
    }
}

void CodexAppServerClient::onStdoutReady()
{
    // readyReadStandardOutput 得到的数据不保证是一条完整 JSON
    m_buffer += m_process.readAllStandardOutput();
    int newlineIdx;
    while ((newlineIdx = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newlineIdx);
        m_buffer.remove(0, newlineIdx + 1);
        if (line.trimmed().isEmpty()) {
            continue; // 空行直接忽略
        }
        handleLine(line.trimmed());
    }
}

void CodexAppServerClient::handleLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[CodexMonitor] 忽略无法解析的 JSON 行:" << parseError.errorString();
        return;
    }
    const QJsonObject msg = doc.object();
    if (msg.contains(QStringLiteral("method"))) {
        handleNotification(msg);
    } else if (msg.contains(QStringLiteral("id"))) {
        handleResponse(msg);
    }
}

void CodexAppServerClient::handleResponse(const QJsonObject &msg)
{
    const int id = msg.value(QStringLiteral("id")).toInt(-1);

    if (id == 1) {
        // initialize 成功（失败也继续尝试，额度接口会给出明确错误）
        QJsonObject initialized;
        initialized.insert(QStringLiteral("method"), QStringLiteral("initialized"));
        initialized.insert(QStringLiteral("params"), QJsonObject());
        sendJson(initialized);
        refresh();
        return;
    }

    if (id == kReadRequestId) {
        m_readTimeout.stop();
        m_requestInFlight = false;

        if (msg.contains(QStringLiteral("error"))) {
            const QJsonObject error = msg.value(QStringLiteral("error")).toObject();
            const QString message = error.value(QStringLiteral("message")).toString();
            qWarning() << "[CodexMonitor] 读取额度失败:" << message;
            if (message.contains(QStringLiteral("authentication required"))
                || message.contains(QStringLiteral("not logged in"))
                || message.contains(QStringLiteral("登录"))) {
                setState(CodexClientState::NotLoggedIn, QStringLiteral("Codex 尚未登录，请先登录 Codex CLI"));
            } else {
                setState(CodexClientState::Failed, QStringLiteral("读取额度失败：") + message);
            }
            return;
        }

        parseRateLimitObject(msg.value(QStringLiteral("result")).toObject());
    }
}

void CodexAppServerClient::handleNotification(const QJsonObject &msg)
{
    const QString method = msg.value(QStringLiteral("method")).toString();
    if (method == QStringLiteral("account/rateLimits/updated")) {
        // 额度变化通知：params.rateLimits 与读取响应的 rateLimit 对象同构
        const QJsonObject params = msg.value(QStringLiteral("params")).toObject();
        const QJsonObject rateLimits = params.value(QStringLiteral("rateLimits")).toObject();
        if (!rateLimits.isEmpty()) {
            parseRateLimitObject(rateLimits);
        }
        return;
    }
    // remoteControl/status/changed 等其它通知：第一版忽略
}

void CodexAppServerClient::parseRateLimitObject(const QJsonObject &result)
{
    // 解析优先级：1. rateLimitsByLimitId.codex；2. 顶层 rateLimits
    QJsonObject limitObj;
    const QJsonObject byLimitId = result.value(QStringLiteral("rateLimitsByLimitId")).toObject();
    limitObj = byLimitId.value(QStringLiteral("codex")).toObject();
    if (limitObj.isEmpty()) {
        limitObj = result.value(QStringLiteral("rateLimits")).toObject();
    }
    if (limitObj.isEmpty()) {
        setState(CodexClientState::NoQuota, QStringLiteral("当前账号没有可用的 Codex 套餐额度"));
        return;
    }

    QuotaState quota;
    quota.planType = limitObj.value(QStringLiteral("planType")).toString();
    if (!quota.planType.isEmpty()) {
        quota.planType[0] = quota.planType[0].toUpper(); // "plus" -> "Plus"
    }
    quota.primary = parseWindow(limitObj.value(QStringLiteral("primary")));
    quota.secondary = parseWindow(limitObj.value(QStringLiteral("secondary")));

    if (!quota.primary.valid && !quota.secondary.valid) {
        setState(CodexClientState::NoQuota, QStringLiteral("当前账号没有可用的 Codex 套餐额度"));
        return;
    }

    // 采样 -> 趋势与消耗速度估算
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (quota.primary.valid) {
        m_history.record(quota.primary.durationMinutes, now, quota.primary.usedPercent);
        quota.primary.burnPerHour = m_history.burnRatePerHour(quota.primary.durationMinutes, now);
        quota.primary.estimatedMinutesLeft = m_history.estimateMinutesLeft(
                quota.primary.durationMinutes, quota.primary.usedPercent,
                quota.primary.resetsAt, now);
        quota.primary.lastSampleAt = now;
    }
    if (quota.secondary.valid) {
        m_history.record(quota.secondary.durationMinutes, now, quota.secondary.usedPercent);
        quota.secondary.burnPerHour = m_history.burnRatePerHour(quota.secondary.durationMinutes, now);
        quota.secondary.estimatedMinutesLeft = m_history.estimateMinutesLeft(
                quota.secondary.durationMinutes, quota.secondary.usedPercent,
                quota.secondary.resetsAt, now);
        quota.secondary.lastSampleAt = now;
    }
    m_history.save();

    publishQuota(quota);
}

void CodexAppServerClient::publishQuota(const QuotaState &quota)
{
    m_quota = quota;
    m_quota.codexActive = m_codexActive;
    setState(CodexClientState::Ready);
    emit quotaUpdated(m_quota);
}

QuotaWindow CodexAppServerClient::parseWindow(const QJsonValue &value) const
{
    QuotaWindow window;
    if (!value.isObject()) {
        return window; // null 或缺失 -> 无效窗口
    }
    const QJsonObject obj = value.toObject();
    const QJsonValue used = obj.value(QStringLiteral("usedPercent"));
    const QJsonValue duration = obj.value(QStringLiteral("windowDurationMins"));
    const QJsonValue resets = obj.value(QStringLiteral("resetsAt"));
    if (!used.isDouble() || !duration.isDouble() || !resets.isDouble()) {
        return window; // 字段缺失或类型错误 -> 无效窗口，不崩溃
    }
    window.usedPercent = qBound(0.0, used.toDouble(), 100.0); // 百分比限制在 0-100
    window.durationMinutes = qMax(0, duration.toInt());
    window.resetsAt = resets.toVariant().toLongLong();
    window.valid = true;
    return window;
}

void CodexAppServerClient::setState(CodexClientState state, const QString &message)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state, message);
}

void CodexAppServerClient::onReadTimeout()
{
    // 请求 10 秒没有响应，进入读取失败状态
    m_requestInFlight = false;
    if (m_process.state() == QProcess::Running) {
        m_process.terminate();
        if (!m_process.waitForFinished(2000)) {
            m_process.kill();
        }
    }
    handleProcessFailure(QStringLiteral("额度请求超时"));
}

void CodexAppServerClient::onPeriodicRefresh()
{
    refresh();
}

void CodexAppServerClient::onCountdownTick()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool expired = (m_quota.primary.valid && m_quota.primary.resetsAt > 0 && now >= m_quota.primary.resetsAt)
                      || (m_quota.secondary.valid && m_quota.secondary.resetsAt > 0 && now >= m_quota.secondary.resetsAt);
    if (expired) {
        refresh();
        return;
    }
    // 每分钟扫描一次本机会话并广播快照：倒计时本地走字、消耗预测实时外推都靠这次心跳
    m_codexActive = scanCodexSessions();
    if (m_quota.primary.valid || m_quota.secondary.valid) {
        m_quota.codexActive = m_codexActive;
        emit quotaUpdated(m_quota);
    }
}

bool CodexAppServerClient::scanCodexSessions() const
{
    // 判定"正在使用"：存在交互式 Codex 会话进程（TUI / codex exec / IDE 插件会话）。
    // 排除：本插件自己的 app-server 子进程、桌面版内置进程、以及空闲的
    // app-server 守护（它们常驻但不代表在消耗额度）。
    const QDir procDir(QStringLiteral("/proc"));
    const QFileInfoList entries = procDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    const qint64 ownPid = m_process.processId();
    for (const QFileInfo &entry : entries) {
        bool ok = false;
        const qint64 pid = QString(entry.fileName()).toLongLong(&ok);
        if (!ok || pid <= 0 || pid == ownPid) {
            continue;
        }
        QFile cmdlineFile(entry.absoluteFilePath() + QStringLiteral("/cmdline"));
        if (!cmdlineFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        QStringList args;
        const QList<QByteArray> rawArgs = cmdlineFile.readAll().split('\0');
        for (const QByteArray &raw : rawArgs) {
            if (!raw.isEmpty()) {
                args.append(QString::fromUtf8(raw));
            }
        }
        if (args.isEmpty()) {
            continue;
        }
        const QString cmd = args.join(QLatin1Char(' '));
        if (cmd.contains(QStringLiteral("app-server"))
            || cmd.contains(QStringLiteral("/usr/lib/chatgpt/"))) {
            continue; // app-server 守护与桌面版内置进程不算"正在使用"
        }
        for (const QString &arg : args) {
            const QString base = arg.section(QLatin1Char('/'), -1);
            if (base == QStringLiteral("codex")) {
                return true; // codex TUI / codex exec / 其它子命令
            }
        }
    }
    return false;
}
