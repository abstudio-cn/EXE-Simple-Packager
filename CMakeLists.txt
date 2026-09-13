cmake_minimum_required(VERSION 3.21)
project(ExeSimplePackager LANGUAGES C CXX)

# 静态 Qt 仅构建了 Release 版（无 Debug 库）：
# - 多配置生成器（VS 生成器）限制配置类型为 Release/RelWithDebInfo
# - 单配置生成器（Ninja，VS 文件夹模式默认）若为 Debug/未指定，强制回退 Release，
#   避免 /MTd 与 Qt 库(/MT) 的 LNK2038/LIBCMTD 冲突。
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Release;RelWithDebInfo" CACHE STRING "" FORCE)
else()
    if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif()
endif()

# 静态 Qt 默认路径回退：Visual Studio 直接打开项目/文件夹时无命令行 -D 参数，
# 此处自动补充前缀，保证 find_package(Qt6) 开箱即用。
# 外部已传 CMAKE_PREFIX_PATH 或缓存里已有 Qt6_DIR 时优先使用外部配置。
if(NOT DEFINED Qt6_DIR AND NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.9.3-static-msvc2022_64")
    message(STATUS "使用默认静态 Qt 前缀：${CMAKE_PREFIX_PATH}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# MSVC：源码含中文，必须 /utf-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ============ 共享静态库 ============
add_library(esp_shared STATIC
    Shared/PkgFormat.cpp
    Shared/ZipHelper.cpp
    Shared/PeInfo.cpp
    Shared/RegOps.cpp
    Shared/Shortcut.cpp
    Shared/ShellPin.cpp
    Shared/IniFile.cpp
    Shared/InstallCore.cpp
    Shared/UninstallRunner.cpp
    Shared/Theme.cpp
    Shared/IconExtract.cpp
    Shared/thirdparty/miniz.c
)
target_include_directories(esp_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Shared
    ${CMAKE_CURRENT_SOURCE_DIR}/Shared/thirdparty
)
target_link_libraries(esp_shared PUBLIC Qt6::Widgets)
set_target_properties(esp_shared PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# miniz 以 C 编译
set_source_files_properties(Shared/thirdparty/miniz.c PROPERTIES LANGUAGE C)

# ============ 资源文件 ============
# rc 模板通过 configure_file 注入图标绝对路径（RC 的 ICON 语句不搜索 /I 路径），
# 结构参考 WindowsProject1（rc + resource.h + ico）。
set(ESPPACKAGER_ICO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Packager/ESPackager.ico")
set(ESPSETUP_ICO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Setup/ESPSetup.ico")
set(ESPUNINSTALL_ICO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Uninstall/ESPUninstall.ico")
# RC 的 ICON 路径需反斜杠
string(REPLACE "/" "\\\\" ESPPACKAGER_ICO_PATH "${ESPPACKAGER_ICO_PATH}")
string(REPLACE "/" "\\\\" ESPSETUP_ICO_PATH "${ESPSETUP_ICO_PATH}")
string(REPLACE "/" "\\\\" ESPUNINSTALL_ICO_PATH "${ESPUNINSTALL_ICO_PATH}")
configure_file(Packager/ESPackager.rc.in ${CMAKE_CURRENT_BINARY_DIR}/Packager/ESPPackager.rc @ONLY)
configure_file(Setup/ESPSetup.rc.in ${CMAKE_CURRENT_BINARY_DIR}/Setup/ESPSetup.rc @ONLY)
configure_file(Uninstall/ESPUninstall.rc.in ${CMAKE_CURRENT_BINARY_DIR}/Uninstall/ESPUninstall.rc @ONLY)

# ============ 内嵌多语言翻译（qrc 打包进 exe） ============
set(I18N_QRC ${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n/i18n.qrc)

# ============ 打包器 ============
add_executable(ESPackager WIN32
    Packager/main.cpp
    Packager/PackagerWindow.cpp
    ${I18N_QRC}
    ${CMAKE_CURRENT_BINARY_DIR}/Packager/ESPPackager.rc
)
target_link_libraries(ESPackager PRIVATE esp_shared)
# 生成的 rc 位于 build 目录，其 #include 需搜索源目录
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/Packager/ESPPackager.rc PROPERTIES
    COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}/Packager\"")

# ============ 安装向导（需要管理员权限） ============
add_executable(ESPSetup WIN32
    Setup/main.cpp
    Setup/WizardWindow.cpp
    ${I18N_QRC}
    ${CMAKE_CURRENT_BINARY_DIR}/Setup/ESPSetup.rc
)
target_link_libraries(ESPSetup PRIVATE esp_shared)
target_link_options(ESPSetup PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/Setup/ESPSetup.rc PROPERTIES
    COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}/Setup\"")

# ============ 卸载器 ============
# 注意：卸载器 manifest 用 asInvoker（默认）+ 运行时自我提权（runas），
# 避免控制面板启动 requireAdministrator 进程被拒时出现
# 「没有足够的权限卸载…」的系统级错误（对齐 Inno Setup 的做法）。
add_executable(ESPUninstall WIN32
    Uninstall/main.cpp
    Uninstall/UninstallWindow.cpp
    ${I18N_QRC}
    ${CMAKE_CURRENT_BINARY_DIR}/Uninstall/ESPUninstall.rc
)
target_link_libraries(ESPUninstall PRIVATE esp_shared shell32)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/Uninstall/ESPUninstall.rc PROPERTIES
    COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}/Uninstall\"")

# 输出到 build/bin
set_target_properties(ESPackager ESPSetup ESPUninstall PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
