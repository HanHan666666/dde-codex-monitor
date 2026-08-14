cmake_minimum_required(VERSION 3.16)
project(dde-codex-monitor VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)

# dde-tray-loader-dev 提供的 DdeTrayLoaderConfig.cmake 把头文件路径写死为
# /usr/include/dde-dock。两种构建方式二选一：
#  1) 系统已安装 dde-tray-loader-dev：直接 find_package
#  2) 没有开发包（如 CI）：设置 -DDDE_TRAY_LOADER_INCLUDE_OVERRIDE=<dde-dock 头文件目录>
find_package(DdeTrayLoader CONFIG QUIET)
set(DDE_TRAY_LOADER_INCLUDE_OVERRIDE "" CACHE PATH "Override DdeTrayLoader include dir")

if(NOT DdeTrayLoader_FOUND AND NOT DDE_TRAY_LOADER_INCLUDE_OVERRIDE)
    message(FATAL_ERROR "DdeTrayLoader not found. Install dde-tray-loader-dev or pass "
                        "-DDDE_TRAY_LOADER_INCLUDE_OVERRIDE=<dir> with the dde-dock headers.")
endif()

add_library(dde-codex-monitor SHARED
    src/codexmonitorplugin.cpp
    src/codexmonitorplugin.h
    src/codexappserverclient.cpp
    src/codexappserverclient.h
    src/codexclipboard.cpp
    src/codexclipboard.h
    src/codexdesktoplauncher.cpp
    src/codexdesktoplauncher.h
    src/codexhistory.cpp
    src/codexhistory.h
    src/codexnotifier.cpp
    src/codexnotifier.h
    src/quotawidgets.cpp
    src/quotawidgets.h
    src/codex-monitor.json
)

target_include_directories(dde-codex-monitor PRIVATE
    "${DDE_TRAY_LOADER_INCLUDE_DIR}"
    "${DDE_TRAY_LOADER_INCLUDE_OVERRIDE}"
)

target_link_libraries(dde-codex-monitor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    PkgConfig::XCB
)

install(TARGETS dde-codex-monitor
    LIBRARY DESTINATION lib/dde-dock/plugins
)
