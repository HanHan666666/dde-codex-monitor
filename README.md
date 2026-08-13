# dde-codex-monitor — DDE Dock 的 Codex 额度监控插件

[![License: LGPL-3.0](https://img.shields.io/badge/License-LGPL--3.0--or--later-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-deepin%2FUOS%20v25-green.svg)]()
[![Dock API](https://img.shields.io/badge/Dock%20API-2.0.0-orange.svg)]()

在 **deepin / UOS v25** 任务栏上直接查看你的 **OpenAI Codex** 订阅额度：
不用打开 Codex CLI 或网页，托盘图标一眼看到用量，快捷面板展示套餐、已用百分比和恢复倒计时。

> 通过 Codex App Server 的 stdio 模式（`codex app-server --listen stdio://`）读取额度，
> 复用现有登录状态，不读取 `~/.codex/auth.json`，不抓取网页，不调用私有 HTTP 接口。

![快捷面板 - 亮色](docs/screenshots/quick-panel-light.png)
![快捷面板 - 暗色](docs/screenshots/quick-panel-dark.png)

*真实运行效果（deepin 25）：*

![真实快捷面板](docs/screenshots/live-quick-panel.png)
![真实托盘图标](docs/screenshots/live-tray-icon.png)

## ✨ 功能特性

- **托盘状态图标**：灰色底环 + 彩色圆弧表示已用比例，中间字母 `C`；
  绿色=正常，橙色=接近上限（≥70%），红色=即将耗尽（≥90%），灰色=无数据/读取失败。
- **快捷面板额度卡片**：套餐类型（如 Plus）、已用百分比、额度周期（5 小时 / 7 天，以服务端为准）、恢复倒计时。
- **双额度窗口**：同时存在 5 小时与 7 天窗口时逐行展示，以最接近上限的窗口决定图标状态。
- **自动刷新**：启动读取一次、每 5 分钟刷新、收到服务端额度变化通知立即更新、倒计时本地更新不发网络请求。
- **手动刷新**：点击托盘图标或额度卡片立即重新读取（图标短暂变灰提示）。
- **异常状态提示**：区分"正在读取 / 未找到 Codex CLI / 未登录 / 读取失败 / 无可用额度"。
- **亮暗主题自适应**：不使用固定背景色，文本颜色跟随系统调色板。

![图标状态](docs/screenshots/tray-icons.png)

## 📋 环境要求

- 系统：deepin / UOS v25（DDE 任务栏 v2，dde-tray-loader ≥ 2.0）
- 编译：CMake ≥ 3.16、C++17、Qt6（Core / Gui / Widgets）
- 依赖开发包：`dde-tray-loader-dev`、`qt6-base-dev`
- 运行时：本机已安装 [Codex CLI](https://github.com/openai/codex) 并用 ChatGPT 账号登录

## 🔨 构建与安装

```bash
sudo apt install build-essential cmake qt6-base-dev dde-tray-loader-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build --prefix /usr
# 或打包安装：
dpkg-deb --build --root-owner-group <deb目录>
sudo dpkg -i dde-codex-monitor.deb
```

安装后重启任务栏（或重新登录）生效：

```bash
systemctl --user restart dde-shell@DDE.service
```

> 构建无需系统级 DDE 头文件：CI 与本地构建均从
> [dde-tray-loader](https://github.com/linuxdeepin/dde-tray-loader) 拉取接口头文件，
> 通过 `-DDDE_TRAY_LOADER_INCLUDE_OVERRIDE=<头文件目录>` 指定（见 .github/workflows/build.yml）。

## 📌 注册到快捷面板

插件托盘图标随插件加载自动出现；**快捷面板卡片**需要在
`quickPlugins` 配置中注册 `codex-monitor`（该配置项为只读，普通用户不可写）。
在支持写入的系统上：

```bash
dde-dconfig get -a org.deepin.ds.dock -r org.deepin.ds.dock.tray -k quickPlugins
```

保留原有列表并追加 `"codex-monitor"` 后写回。若 `dde-dconfig set` 提示无权限
（本机实测），可写入系统覆盖文件（`dsg.config.override` 格式）：

```bash
sudo mkdir -p /etc/dsg/configs/overrides/org.deepin.ds.dock/org.deepin.ds.dock.tray
```

```json
{
  "magic": "dsg.config.override",
  "version": "1.0",
  "contents": {
    "quickPlugins": {
      "value": ["network", "bluetooth", "...原有插件...", "codex-monitor"],
      "serial": 0
    }
  }
}
```

写入后重启 `dde-dconfig-daemon.service` 与 dde-shell 生效。

## 🔎 Codex CLI 查找顺序

1. 环境变量 `DDE_CODEX_MONITOR_CODEX_BIN` 指定路径
2. `PATH` 中的 `codex`
3. `~/.local/bin/codex`
4. `~/.nvm/versions/node/*/bin/codex`（取最新）

若找到的是 NVM 中的 Codex，插件会把其 `bin` 目录前置到子进程 `PATH`，
解决 `/usr/bin/env node` 找不到 Node.js 的问题。

## 📁 项目结构

```text
.
├── CMakeLists.txt
├── src
│   ├── codex-monitor.json          # 插件元数据 {"api": "2.0.0"}
│   ├── codexmonitorplugin.*        # PluginsItemInterfaceV2 插件主体
│   ├── codexappserverclient.*      # QProcess + JSONL：App Server 客户端
│   └── quotawidgets.*              # 托盘图标 / 快捷面板卡片（QPainter 绘制）
└── docs/screenshots
```

## 📄 设计文档

需求与验收标准见 [DESIGN.md](DESIGN.md)（第一版范围：无独立窗口、无设置页、无后台服务、
无数据库、无趋势图，异常不得导致任务栏崩溃）。

## ⚖️ 许可证

[LGPL-3.0-or-later](LICENSE)
