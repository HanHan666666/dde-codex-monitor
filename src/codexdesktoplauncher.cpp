// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "codexdesktoplauncher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace CodexDesktop {

// 环境变量：指定 Codex 桌面版 desktop 文件（完整路径或文件名）
static const char kDesktopEntryEnv[] = "DDE_CODEX_MONITOR_DESKTOP_ENTRY";

// 候选 desktop 文件名；官方 Linux 包安装的是 chatgpt.desktop
static const QStringList kDesktopEntryNames = {
    QStringLiteral("chatgpt.desktop"),
    QStringLiteral("codex.desktop"),
};

QString findDesktopEntry()
{
    // 显式指定的文件名优先，之后仍按 XDG 查找顺序兜底
    QStringList names = kDesktopEntryNames;
    const QString override = qEnvironmentVariable(kDesktopEntryEnv);
    if (!override.isEmpty()) {
        names.prepend(override);
    }
    for (const QString &name : std::as_const(names)) {
        if (name.startsWith(QLatin1Char('/')) && QFileInfo::exists(name)) {
            return name;
        }
        const QString found = QStandardPaths::locate(
                QStandardPaths::GenericDataLocation,
                QStringLiteral("applications/") + name);
        if (!found.isEmpty()) {
            return found;
        }
    }
    return QString();
}

// 读取 [Desktop Entry] 段的 Exec / Path
static bool readDesktopEntry(const QString &path, QString *exec, QString *workDir)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    bool inMainSection = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            if (inMainSection) {
                break; // 主段结束
            }
            inMainSection = (line == QStringLiteral("[Desktop Entry]"));
            continue;
        }
        if (!inMainSection) {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString key = line.left(eq);
        if (key == QLatin1String("Exec")) {
            *exec = line.mid(eq + 1);
        } else if (key == QLatin1String("Path")) {
            *workDir = line.mid(eq + 1);
        }
    }
    return !exec->isEmpty();
}

// 按 desktop entry 规范反转义（\s 空格、\n、\t、\\ 等）
static void unescape(QString *token)
{
    QString out;
    out.reserve(token->size());
    for (int i = 0; i < token->size(); ++i) {
        const QChar ch = token->at(i);
        if (ch == QLatin1Char('\\') && i + 1 < token->size()) {
            const QChar next = token->at(++i);
            if (next == QLatin1Char('s')) {
                out += QLatin1Char(' ');
            } else if (next == QLatin1Char('n')) {
                out += QLatin1Char('\n');
            } else if (next == QLatin1Char('t')) {
                out += QLatin1Char('\t');
            } else if (next == QLatin1Char('r')) {
                out += QLatin1Char('\r');
            } else {
                out += next; // \\ \" \` \$ 等按字面输出
            }
        } else {
            out += ch;
        }
    }
    *token = out;
}

// 字段代码（%f %U %i …）不参与执行（chatgpt.desktop 的 Exec=chatgpt %U）
static bool isFieldCode(const QString &token)
{
    return token.startsWith(QLatin1Char('%')) && token.size() >= 2
            && QStringLiteral("fFuUdDnNickvm").contains(token.at(1));
}

// Exec 行分词（支持单双引号与 \s 等转义），并丢弃字段代码
static QStringList parseExecArgs(const QString &execLine)
{
    QStringList args;
    QString current;
    bool hasToken = false;
    bool inSingle = false;
    bool inDouble = false;
    auto finishToken = [&args, &current, &hasToken]() {
        if (!hasToken) {
            return;
        }
        unescape(&current);
        if (!isFieldCode(current)) {
            args.append(current);
        }
        current.clear();
        hasToken = false;
    };
    const int size = execLine.size();
    for (int i = 0; i < size; ++i) {
        const QChar ch = execLine.at(i);
        if (ch == QLatin1Char('\\') && i + 1 < size && !inSingle && !inDouble) {
            hasToken = true;
            current += ch;
            current += execLine.at(++i);
        } else if (inSingle) {
            if (ch == QLatin1Char('\'')) {
                inSingle = false;
            } else {
                hasToken = true;
                current += ch;
            }
        } else if (inDouble) {
            if (ch == QLatin1Char('"')) {
                inDouble = false;
            } else {
                hasToken = true;
                current += ch;
            }
        } else if (ch == QLatin1Char('\'')) {
            inSingle = true;
            hasToken = true;
        } else if (ch == QLatin1Char('"')) {
            inDouble = true;
            hasToken = true;
        } else if (ch.isSpace()) {
            finishToken();
        } else {
            hasToken = true;
            current += ch;
        }
    }
    finishToken();
    return args;
}

// 裸命令名解析为绝对路径；dde-shell 的 PATH 可能缺少 ~/.local/bin，单独补查
static QString resolveExecutable(const QString &program)
{
    if (program.contains(QLatin1Char('/'))) {
        return program;
    }
    QString resolved = QStandardPaths::findExecutable(program);
    if (resolved.isEmpty()) {
        const QString local = QDir::homePath()
                + QStringLiteral("/.local/bin/") + program;
        if (QFileInfo(local).isExecutable()) {
            resolved = local;
        }
    }
    return resolved;
}

bool launch(QString *error)
{
    const QString entry = findDesktopEntry();
    if (entry.isEmpty()) {
        if (error) {
            *error = QStringLiteral("未找到 Codex 桌面版（chatgpt.desktop）");
        }
        return false;
    }

    QString exec;
    QString workDir;
    if (!readDesktopEntry(entry, &exec, &workDir)) {
        if (error) {
            *error = QStringLiteral("Codex 桌面版入口缺少 Exec：%1").arg(entry);
        }
        return false;
    }

    QStringList args = parseExecArgs(exec);
    if (args.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Codex 桌面版 Exec 无法解析：%1").arg(exec);
        }
        return false;
    }

    const QString program = resolveExecutable(args.takeFirst());
    if (program.isEmpty() || !QFileInfo(program).isExecutable()) {
        if (error) {
            *error = QStringLiteral("未找到 Codex 桌面版可执行文件");
        }
        return false;
    }

    if (!QProcess::startDetached(program, args, workDir)) {
        if (error) {
            *error = QStringLiteral("Codex 桌面版启动失败：%1").arg(program);
        }
        return false;
    }
    return true;
}

} // namespace CodexDesktop
