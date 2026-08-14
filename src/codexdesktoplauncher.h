// SPDX-FileCopyrightText: 2025 DDE Codex Monitor contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef CODEXDESKTOPLAUNCHER_H
#define CODEXDESKTOPLAUNCHER_H

#include <QString>

/**
 * @brief 查找并启动 Codex 桌面版（内含 Codex 的 ChatGPT 桌面应用）
 *
 * 官方 Linux 桌面版安装后提供 /usr/share/applications/chatgpt.desktop 与
 * /usr/bin/chatgpt（https://learn.chatgpt.com/docs/linux/linux-app）。
 */
namespace CodexDesktop {

/**
 * @brief 在 XDG applications 目录中查找桌面入口文件，找不到返回空串
 */
QString findDesktopEntry();

/**
 * @brief 解析桌面入口的 Exec/Path 并脱离启动正在运行的进程
 *
 * @param error 可选，失败时填充用户可读的原因
 */
bool launch(QString *error = nullptr);

} // namespace CodexDesktop

#endif // CODEXDESKTOPLAUNCHER_H
