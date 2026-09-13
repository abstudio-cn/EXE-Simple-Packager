# ExeSimplePackager — ESP 三件套（C++ / Qt 6）

简易程序打包器三件套：把程序目录打包成**单一可执行安装程序**，MSI 风格安装向导引导安装，
同风格卸载器（卸载逻辑对齐 ProgramReg 的 uninstall.ini / 注册表方案）。

## 三件套

| 程序 | 说明 |
|---|---|
| **ESPackager.exe** | 打包器：目录 → ZIP（miniz）→ 封包进 ESPSetup.exe（ESPPKG01 尾部索引），卸载器运行时一并注入 |
| **ESPSetup.exe** | MSI 风格安装向导：欢迎 / 许可协议 / 安装选项 / 进度 / 完成，上一步·下一步·取消 |
| **ESPUninstall.exe** | 同风格卸载器：确认 / 进度 / 完成；按 uninstall.ini 清理，含延迟自删 |

## 封包格式 ESPPKG01

```
[安装器 exe 原样][ZIP 负载][JSON 索引（4 字节长度前缀）]["ESPPKG01"]
```

- ZIP 负载 = 应用文件 + `__esp_uninstall/ESPUninstall.exe`（安装时解压为 `uninstall.exe`）
- 索引 JSON：`format / payloadOffset / payloadSize / appName / version / publisher / appId / mainExe`

## 安装产物（与 ProgramReg 一致）

- 注册表：`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\<appId>`
  （DisplayName / DisplayVersion / Publisher / DisplayIcon / InstallLocation /
  UninstallString / QuietUninstallString / NoModify / NoRepair / EstimatedSize，
  按主程序 PE 架构选择 32/64 位视图）
- `uninstall.ini`：AppInfo + Cleanup 段（StartMenuFolder / RegistryKeyName / RegistryView /
  PinTaskbar / DesktopShortcut / DeleteInstallDir / ExtraPath0..n）
- 快捷方式：开始菜单（所有用户）、桌面；任务栏固定（Shell 动词，Win11 部分版本不支持则提示手动）

## 构建

```powershell
# 1) 一次性：编译静态 Qt（qtbase，约 20-40 分钟）
powershell -File scripts\build-qt-static.ps1

# 2) 构建三件套（自包含静态 exe，无需 Qt DLL）
powershell -File scripts\build.ps1

# 3) （可选）生成 VS 2026 解决方案（build-vs\ExeSimplePackager.slnx）
powershell -File scripts\gen-vs.ps1
```

前置条件：
- Visual Studio 2022/2026（MSVC 工具集 + CMake + Ninja）
- Python + `pip install aqtinstall`（下载 Qt 二进制 / 源码 / jom）

发布：三个 exe 放在同一目录即可（打包器从 ESPSetup.exe 同目录读取 ESPUninstall.exe）。

## 命令行模式

```powershell
# 打包
ESPackager.exe /auto /dir=<src> /out=<setup.exe> [/name=..] [/ver=..] [/pub=..] [/mainexe=..] [/appid=..] [/stub=..]

# 静默安装（日志 %TEMP%\ESPSetup.log）
Setup.exe /quiet /dir=<target> /agree [/startmenu=0|1] [/desktop=0|1] [/taskbar=0|1]

# 静默卸载（日志 %TEMP%\ESPUninstall.log）
uninstall.exe /quiet [/ini=path]
```

## 端到端验证

```powershell
# 需管理员权限（写 HKLM / ProgramData / 公共桌面）
powershell -File scripts\verify.ps1
```

覆盖：构造测试应用 → 打包（校验 ESPPKG01 尾部）→ 静默安装（文件 / uninstall.ini /
注册表 / 快捷方式）→ 静默卸载（目录清理含延迟自删 / 注册表 / 快捷方式全部清除）。

## 目录结构（参考 VS C++ 桌面模板项目）

```
ExeSimplePackager/
├── ExeSimplePackager.slnx      ← build-vs\ExeSimplePackager.slnx（VS 2026 解决方案）
├── CMakeLists.txt
├── Shared/                     共享静态库 + framework.h / targetver.h（Windows 模板头）
├── Packager/                   ESPackager（main + 窗口 + .rc + resource.h + .ico）
├── Setup/                      ESPSetup（提权 manifest，MSI 风格向导 + .rc + resource.h + .ico）
├── Uninstall/                  ESPUninstall（提权 manifest + .rc + resource.h + .ico）
└── scripts/                    build.ps1 / build-qt-static.ps1 / gen-vs.ps1 / verify.ps1 / gen-icons.py
```

每个 exe 目录含 WindowsProject1 模板风格资源文件：
- `<Name>.rc`：图标（大+小 ID）+ VERSIONINFO 版本信息 + 字符串表
- `resource.h`：资源 ID
- `<Name>.ico`：多尺寸图标（256/64/48/32/24/16，PIL 生成，`scripts\gen-icons.py`）

rc 为 `.rc.in` 模板（CMake `configure_file` 注入图标绝对路径——RC 的 ICON 语句不搜索 /I 路径），
生成到 build 目录后由 rc.exe 编译。

## 多语言（i18n）

三件套自动识别系统语言，翻译资源（esp_*.qm + Qt 官方 qtbase_*.qm）**内嵌于 exe**，无需外部文件：

| 语言 | 代码 | | 语言 | 代码 |
|---|---|---|---|---|
| 英语 | en | | 日语 | ja |
| 简体中文 | zh_CN | | 韩语 | ko |
| 繁体中文 | zh_TW | | 法语 | fr |
| 德语 | de | | 西班牙语 | es |
| 俄语 | ru | | | |

- 强制指定语言（调试/测试）：设置环境变量 `ESP_LANG=ja` 等
- 新增/修改翻译：编辑 `scripts\gen-i18n.py`（+ `gen-i18n-en.py`）→ 运行生成 .ts →
  `lrelease` 编译 .qm → 更新 `assets\i18n\i18n.qrc` → 重新构建
- 字符串规范：窗口类用 `tr()`；Shared 静态库用 `QCoreApplication::translate("上下文", ...)`

## 许可说明

Qt 以 LGPL v3 发布；本工具静态链接 Qt，分发时需保留重新链接能力（提供源码与构建脚本，
本项目满足该要求）。miniz 为公共领域软件。
