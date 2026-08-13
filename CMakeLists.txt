cmake_minimum_required(VERSION 3.16)
project(dde-codex-monitor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(DdeTrayLoader CONFIG REQUIRED)

# DdeTrayLoaderConfig.cmake 把头文件路径写死为 /usr/include/dde-dock，
# 使用本地解包 dev 包构建时用该变量覆盖
set(DDE_TRAY_LOADER_INCLUDE_OVERRIDE "" CACHE PATH "Override DdeTrayLoader include dir")

add_library(dde-codex-monitor SHARED
    src/codexmonitorplugin.cpp
    src/codexmonitorplugin.h
    src/codexappserverclient.cpp
    src/codexappserverclient.h
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
)

install(TARGETS dde-codex-monitor
    LIBRARY DESTINATION lib/dde-dock/plugins
)
