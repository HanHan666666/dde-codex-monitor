# DDE Codex 额度监控面板需求

## 1. 项目目标

开发一个适用于 deepin/UOS v25 的 DDE 任务栏快捷面板插件，用于查看当前用户的 Codex 套餐额度。

用户无需打开 Codex 或网页，即可从任务栏看到额度使用情况和恢复时间。

第一版以简单、稳定、能用为目标，不搭建复杂架构。

> **第二版扩展（已实现）**：在第一版基础上增加——系统通知（恢复/阈值预警）、
> 右键菜单、消耗速度预测与"使用中"实时外推、详情子页面与 48 小时趋势图、
> 耗尽图标态、挂起唤醒后立即刷新。目标环境仅 Deepin/DDE：启动应用优先经
> `dde-am`，配置经 Dock 插件机制（`saveValue`），历史数据存
> `$XDG_DATA_HOME/dde-codex-monitor/history.json`（仅本地、48 小时）。

## 2. 产品形态

- DDE 原生快捷面板插件。
- 在 Dock 中提供一个常驻状态图标。
- 在任务栏快捷面板中提供一个整行额度卡片。
- 第一版不提供独立应用窗口和详情页面。

## 3. 核心功能

### 3.1 额度展示

显示当前 Codex 账号的：

- 套餐类型，例如 Plus。
- 已使用或剩余额度百分比。
- 额度周期，例如 5 小时或 7 天。
- 下一次额度恢复时间或恢复倒计时。
- 服务端实际返回的主要和次要额度窗口。

界面必须明确百分比表示“已用”还是“剩余”，避免歧义。

不得把额度周期硬编码为固定的 5 小时或 7 天，应以 Codex 返回的数据为准。

### 3.2 状态图标

Dock 图标通过颜色或环形进度表达当前额度状态：

- 正常：绿色。
- 接近上限：橙色。
- 即将耗尽或已经耗尽：红色。
- 暂无数据或读取失败：灰色。

如果存在多个额度窗口，以最接近上限的窗口决定图标状态。

### 3.3 数据刷新

- 插件启动后自动读取一次额度。
- 额度变化后及时更新界面。
- 定期刷新额度，刷新频率不需要过高。
- 用户点击额度卡片时可以手动刷新。
- 单击 Dock 状态图标同样手动刷新；为区分双击，单击在系统双击间隔后触发。
- 双击 Dock 状态图标打开 Codex 桌面版（官方 Linux 版 ChatGPT 应用，可选依赖，
  未安装时悬停图标短暂提示原因，不影响额度监控）。
- 倒计时在本地更新，不应为更新倒计时频繁请求网络。

### 3.4 异常状态

界面应能区分并提示：

- 正在读取额度。
- 未找到 Codex CLI。
- Codex 尚未登录。
- 网络异常或额度读取失败。
- 当前账号没有可用的 Codex 套餐额度。

读取失败后允许用户点击重试，任何异常都不能导致任务栏崩溃或卡死。

## 4. 数据来源

第一版使用本机 Codex App Server 的 stdio 模式读取额度：

```text
codex app-server --listen stdio://
```

主要使用 Codex 提供的额度读取能力和额度变化通知。

该方案已在本机验证，可以获取套餐、额度窗口、已用百分比和恢复时间。

## 5. 安全和隐私要求

- 复用 Codex 当前用户已有的登录状态。
- 不读取、复制或解析 `~/.codex/auth.json`。
- 不在日志中输出 Token、Cookie 或其他登录凭据。
- 不抓取 ChatGPT 网页。
- 不直接调用 ChatGPT 私有 HTTP 接口。
- 不自行保存用户账号信息。
- v2 起本地保存额度采样历史（仅百分比与时间戳，48 小时自动清理），
  存放于 `$XDG_DATA_HOME/dde-codex-monitor/history.json`，不上传。

## 6. 第一版界面要求

快捷面板使用 DDE 的整行布局，内容保持紧凑，例如：

```text
[状态图标]  Codex Plus          剩余 85%
            7 天额度 · 6 天后恢复
```

如果同时存在两个额度窗口，可显示两条简要信息：

```text
[状态图标]  Codex Plus
            5 小时：剩余 65% · 2 小时后恢复
            7 天：剩余 85% · 6 天后恢复
```

要求：

- 卡片中的百分比显示"剩余"（100 − 已用）；状态图标的圆弧与颜色仍按"已用"比例绘制。
- 卡片底部显示剩余额度进度条：填充比例 = 剩余（与"剩余"文案一致），颜色 = 状态色
  （绿/橙/红），无数据时只显示半透明底槽。
- 适配系统亮色和暗色主题。
- 不使用固定背景色破坏系统主题。
- 信息能够在快捷面板规定尺寸内完整显示。
- 第一版直接使用简体中文，不要求国际化。

## 7. 明确不做

第一版不包含（标注 ✅ 的条目已在第二版按需放开，其余仍然不做）：

- 独立桌面应用。
- 单独的后台 daemon 或 D-Bus 服务。
- 数据库和历史记录存储。✅ v2 放开：历史用本地 JSON 文件，无数据库
- Token 使用趋势图。✅ v2 放开：详情页 48 小时趋势曲线
- 设置页面和复杂配置。
- 账号登录、退出或切换。
- API Key 账单和 API Platform 用量。
- Credits 购买、充值或额度重置操作。
- 网页抓取和本地会话文件扫描。
- Demo、Debug 子工程和自动化测试框架。
- 控制中心设置入口。

除非第一版无法正常工作，否则不要增加上述内容。

## 8. 兼容范围

- 目标系统：deepin/UOS v25。
- 插件接口：`PluginsItemInterfaceV2`。
- 插件类型：DDE 快捷面板整行插件。
- 数据方式：Codex App Server stdio。
- 第一版只保证使用 ChatGPT 账号登录的 Codex 可用。
- API Key 模式不承诺显示套餐额度。

需要考虑 Dock 进程可能找不到安装在 NVM 目录中的 `codex` 命令。插件应能够发现当前用户实际安装的 Codex CLI，或者允许通过环境变量指定路径，但不需要为此增加设置界面。

## 9. 验收标准

满足以下条件即可认为第一版完成：

1. 插件能够被 DDE 任务栏正常加载和显示。
2. 能读取当前 Codex 账号的真实额度百分比和恢复时间。
3. 展示结果与 Codex `/usage` 中的信息基本一致。
4. 存在多个额度窗口时能够正确展示。
5. 点击卡片可以刷新额度。
6. Codex 未安装、未登录或断网时有清晰提示。
7. 恢复网络后可以通过重试恢复正常显示。
8. 亮色和暗色主题下均可正常阅读。
9. 插件不会阻塞或导致 DDE 任务栏崩溃。
10. 工程没有超出第一版需求边界的复杂组件。

验收以人工安装和手动操作为主，不要求开发自动化测试。

## 10. 参考实现方案

本节用于帮助开发者快速落地，不改变前面的需求范围。如果实现中有更简洁且满足验收标准的做法，可以自行调整。

### 10.1 整体结构

```text
dde-tray-loader
    └── CodexMonitorPlugin
        ├── QuotaIconWidget       Dock 状态图标（单击刷新 / 双击打开桌面版）
        ├── QuotaPanelWidget      快捷面板额度卡片
        ├── CodexAppServerClient  QProcess + JSONL
        │       └── codex app-server --listen stdio://
        └── CodexDesktopLauncher  查找并启动 Codex 桌面版
```

整个插件只包含一个动态库和一个由插件管理的 Codex 子进程。

建议文件结构：

```text
.
├── CMakeLists.txt
├── DESIGN.md
└── src
    ├── codex-monitor.json
    ├── codexmonitorplugin.h
    ├── codexmonitorplugin.cpp
    ├── codexappserverclient.h
    ├── codexappserverclient.cpp
    ├── codexdesktoplauncher.h
    ├── codexdesktoplauncher.cpp
    ├── quotawidgets.h
    └── quotawidgets.cpp
```

不要增加 `demo/`、`tests/`、`debug/`、`service/` 等目录。

### 10.2 DDE 插件接口

插件类继承：

```cpp
class CodexMonitorPlugin : public QObject, public PluginsItemInterfaceV2
```

必要声明：

```cpp
Q_OBJECT
Q_INTERFACES(PluginsItemInterfaceV2)
Q_PLUGIN_METADATA(IID ModuleInterface_iid_V2 FILE "codex-monitor.json")
```

固定信息：

```cpp
pluginName()         -> "codex-monitor"
pluginDisplayName()  -> "Codex 额度"
flags()              -> Dock::Type_Quick | Dock::Quick_Panel_Full
```

第一版不添加 `Attribute_CanSetting` 或 `Attribute_Normal`，避免引入控制中心入口及 DCI 图标资源。

`init()` 保存由 Loader 管理的 `PluginProxyInterface` 指针，并注册插件项：

```cpp
m_proxyInter = proxyInter;
m_proxyInter->itemAdded(this, QStringLiteral("codex-monitor"));
```

`itemWidget()` 按以下规则返回控件：

- `itemKey == "codex-monitor"`：返回 Dock 状态图标。
- `itemKey == Dock::QUICK_ITEM_KEY`：返回快捷面板卡片。
- 其他值：返回 `nullptr`。

快捷面板只设置固定高度，不锁定宽度：

```cpp
setFixedHeight(Dock::QUICK_ITEM_HEIGHT);
```

第一版 `itemPopupApplet()` 返回 `nullptr`。

插件元数据 `codex-monitor.json`：

```json
{
  "api": "2.0.0"
}
```

### 10.3 Codex 客户端职责

`CodexAppServerClient` 单独负责：

- 查找 Codex CLI。
- 启动和关闭 App Server 子进程。
- 完成 JSON-RPC 初始化。
- 请求并解析额度。
- 监听额度变化通知。
- 处理请求超时、进程退出和有限重试。

UI 和插件类不直接处理 JSON 或进程通信。

### 10.4 Codex CLI 路径

按以下顺序查找：

1. 环境变量 `DDE_CODEX_MONITOR_CODEX_BIN` 指定的文件。
2. `QStandardPaths::findExecutable("codex")`。
3. `$HOME/.local/bin/codex`。
4. `$HOME/.nvm/versions/node/*/bin/codex` 中最后修改时间最新的可执行文件。

本机 Codex 安装在 NVM 目录中，而 Dock 进程的 `PATH` 可能不包含该目录。

如果找到的是 NVM 中的 Codex，启动前应将其 `bin` 目录放到子进程 `PATH` 最前面，否则 Codex 脚本使用的 `/usr/bin/env node` 可能找不到 Node.js：

```cpp
QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
env.insert("PATH", codexBinDir + ":" + env.value("PATH"));
process.setProcessEnvironment(env);
```

### 10.5 Codex 桌面版启动

双击 Dock 状态图标时，按以下顺序查找桌面入口文件：

1. 环境变量 `DDE_CODEX_MONITOR_DESKTOP_ENTRY` 指定的 desktop 文件（路径或文件名）。
2. `QStandardPaths` 的 `GenericDataLocation` 下的 `applications/chatgpt.desktop`
   （覆盖 `~/.local/share/applications` 与 `/usr/share/applications` 等 XDG 目录）。
3. 同目录下的 `codex.desktop` 作为备选文件名。

找到后解析 `[Desktop Entry]` 段的 `Exec` 与 `Path`：按 desktop entry 规范处理引号与
`\s` 等转义，丢弃 `%f`/`%U` 等字段代码，裸命令名用 `QStandardPaths::findExecutable`
解析（另补查 `$HOME/.local/bin`），最后 `QProcess::startDetached` 脱离启动。
启动失败只通过悬停提示与日志反馈，不允许影响任务栏。

### 10.6 启动 App Server

```cpp
process.setProgram(codexPath);
process.setArguments({"app-server", "--listen", "stdio://"});
process.setProcessChannelMode(QProcess::SeparateChannels);
process.start();
```

只解析 stdout。stderr 不是 JSONL，不能与 stdout 合并；stderr 仅用于输出有限的错误日志。

插件销毁时先调用 `terminate()`，短暂等待后仍未退出再调用 `kill()`，避免留下子进程。

### 10.7 JSONL 读取

`readyReadStandardOutput` 得到的数据不保证是一条完整 JSON。客户端需要维护 `QByteArray` 缓冲区：

1. 追加本次读取的数据。
2. 按换行符循环取出完整行。
3. 空行直接忽略。
4. 使用 `QJsonDocument::fromJson()` 解析完整行。
5. 没有换行的尾部保留到下一次读取。

不能假设一次 `readyReadStandardOutput` 就对应一条响应。

### 10.8 App Server 初始化

进程启动成功后发送：

```json
{
  "method": "initialize",
  "id": 1,
  "params": {
    "clientInfo": {
      "name": "dde_codex_monitor",
      "title": "DDE Codex Monitor",
      "version": "0.1.0"
    }
  }
}
```

收到 `id == 1` 的成功响应后发送：

```json
{ "method": "initialized", "params": {} }
{ "method": "account/rateLimits/read", "id": 2 }
```

每个 JSON 对象必须序列化成单行并追加换行符后写入 stdin。不要在 `initialize` 成功前调用额度接口。

### 10.9 额度读取和更新

使用：

```json
{ "method": "account/rateLimits/read", "id": 2 }
```

额度变化通知：

```json
{
  "method": "account/rateLimits/updated",
  "params": {
    "rateLimits": {}
  }
}
```

刷新策略：

- 启动成功后读取一次。
- 每 5 分钟读取一次。
- 用户点击额度卡片时立即读取。
- 收到额度变化通知时立即更新界面。
- 每分钟只更新本地倒计时。
- 同一时间最多保留一个额度请求。
- 请求 10 秒没有响应，进入读取失败状态。
- 子进程异常退出后等待 30 秒再尝试启动，不能高频循环重启。

### 10.10 数据模型和解析

可以使用简单数据结构：

```cpp
struct QuotaWindow {
    bool valid = false;
    double usedPercent = 0;
    int durationMinutes = 0;
    qint64 resetsAt = 0;
};

struct QuotaState {
    QString planType;
    QuotaWindow primary;
    QuotaWindow secondary;
};
```

解析优先级：

1. 优先使用 `rateLimitsByLimitId.codex`。
2. 不存在时使用顶层 `rateLimits`。
3. 两者都不存在时显示无额度数据。

字段对应关系：

- `usedPercent`：已用百分比。
- `windowDurationMins`：额度窗口分钟数。
- `resetsAt`：恢复时间，Unix 秒级时间戳。
- `planType`：套餐类型。

百分比限制在 0 到 100 范围。缺少字段或字段类型错误的窗口视为无效，不能因此崩溃。

### 10.11 界面实现建议

Dock 图标可以直接使用 `QPainter` 绘制，不增加 SVG/DCI 资源：

- 24×24 透明控件。
- 灰色底环。
- 彩色圆弧表示已用百分比。
- 中间绘制简短的 `C`。

如果 primary 和 secondary 都存在，以已用比例较高的窗口决定圆弧和颜色。

快捷面板可使用普通 Qt Widgets 布局。窗口长度按返回的分钟数格式化：

- 小于 60 分钟：`N 分钟`。
- 能整除 60 且小于一天：`N 小时`。
- 能整除 1440：`N 天`。
- 其他情况直接显示分钟。

`resetsAt` 使用本地时区转换并优先显示相对时间，例如“2 小时 14 分后恢复”。倒计时结束后主动读取一次最新额度。

### 10.12 构建配置

目标系统使用 Qt 6 和 DDE 托盘 V2 API。开发依赖：

```bash
sudo apt install build-essential cmake qt6-base-dev dde-tray-loader-dev
```

核心 CMake 配置可以是：

```cmake
cmake_minimum_required(VERSION 3.16)
project(dde-codex-monitor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(DdeTrayLoader CONFIG REQUIRED)

add_library(dde-codex-monitor SHARED
    src/codexmonitorplugin.cpp
    src/codexmonitorplugin.h
    src/codexappserverclient.cpp
    src/codexappserverclient.h
    src/codexdesktoplauncher.cpp
    src/codexdesktoplauncher.h
    src/quotawidgets.cpp
    src/quotawidgets.h
    src/codex-monitor.json
)

target_link_libraries(dde-codex-monitor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

install(TARGETS dde-codex-monitor
    LIBRARY DESTINATION lib/dde-dock/plugins
)
```

插件接口头文件：

```cpp
#include <pluginsiteminterface_v2.h>
#include <pluginproxyinterface.h>
```

构建安装：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Deepin 25 的 `/usr` 只读，直接 `cmake --install` 会失败；按 README 的方式打包
deb 后用 apt 安装：

```bash
install -D build/libdde-codex-monitor.so \
  packaging/dde-codex-monitor/usr/lib/dde-dock/plugins/libdde-codex-monitor.so
dpkg-deb --build --root-owner-group packaging/dde-codex-monitor
sudo apt install -y packaging/dde-codex-monitor.deb
```

预期安装结果：

```text
/usr/lib/dde-dock/plugins/libdde-codex-monitor.so
```

### 10.13 注册快捷面板插件

安装动态库后，把 `codex-monitor` 追加到现有 `quickPlugins` 数组。

先读取现有值：

```bash
dde-dconfig get \
  -a org.deepin.ds.dock \
  -r org.deepin.ds.dock.tray \
  -k quickPlugins
```

保留所有原有项目并追加 `codex-monitor`，再写回完整数组：

```bash
dde-dconfig set \
  -a org.deepin.ds.dock \
  -r org.deepin.ds.dock.tray \
  -k quickPlugins \
  -v '["network", "bluetooth", "...原有项目...", "codex-monitor"]'
```

`dde-dconfig set` 会整体替换数组，不能丢掉已有插件。

重新登录最稳妥。开发阶段也可以手动重启 DDE Shell：

```bash
systemctl --user restart dde-shell@DDE.service
```

该操作会短暂重启桌面 Shell，应由用户自行执行。

### 10.14 复制额度信息与 X11 剪贴板

右键菜单“复制额度信息”把当前额度摘要写入系统剪贴板。注意：托盘插件进程运行在
dde-shell 内嵌的 Wayland 合成器上（QPA 为 `libqwayland-generic`），该合成器不实现
`wl_data_device`，因此插件进程里 `QClipboard::setText()` 是静默空操作。

`CodexClipboard` 用一条独立的 XCB 连接直接成为 X11 CLIPBOARD selection 属主，
并在插件存活期间通过 `QSocketNotifier` 持续服务粘贴方的 `SelectionRequest`
（支持 `TARGETS` / `UTF8_STRING` / `STRING` / `TEXT` / `text/plain` /
`TIMESTAMP`）。`DISPLAY` 优先读环境变量，读不到回退 deepin 固定的 `:0`；
插件进程环境变量被 dde-shell 清理，连 X 前需补上 `HOME`/`XAUTHORITY`，
否则 Xau 拿不到认证 cookie。deepin 的剪贴板守护与 Xwayland 桥都基于 X11
selection，此通道在 X11 会话与 treeland Wayland 会话下均可用。XCB 连接失败时
退回 `QClipboard::setText()` 兜底；复制成功后悬停提示短暂显示“已复制到剪贴板”。
