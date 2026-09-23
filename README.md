# ExeSimplePackager — ESP 三件套（C++ / Qt 6）

简易程序打包器三件套：把程序目录打包成**单一可执行安装程序**，MSI 风格安装向导引导安装，
同风格卸载器（卸载逻辑对齐 ProgramReg 的 uninstall.ini / 注册表方案）。

**特色：安装器与卸载器的外观（配色 + 背景图）都可以由打包者在打包时自定义**，
左侧栏 / 按钮 / 进度条配色、预设主题、背景图片（铺满 / 适应 / 拉伸 / 平铺 / 居中、
不透明度可调）实时预览，所见即所得；**还能把 Python 项目直接打包成自包含应用**
（随包携带解释器，目标机无需安装 Python）。

win7兼容性支持正在研发中，请耐心等待

## 三件套

| 程序 | 说明 |
|---|---|
| **ESPackager.exe** | 打包器：目录 → ZIP（miniz）→ 封包进 ESPSetup.exe（ESPPKG01 尾部索引），卸载器运行时与外观主题一并注入；内置「外观主题」编辑器（实时预览） |
| **ESPSetup.exe** | MSI 风格安装向导：欢迎 / 许可协议 / 安装选项 / 进度 / 完成，上一步·下一步·取消；按封包内主题渲染；安装时把卸载器外观写入安装目录 |
| **ESPUninstall.exe** | 同风格卸载器：确认 / 进度 / 完成；按 uninstall.ini 清理（含 `[Theme]` 外观），含延迟自删 |

## 自定义外观（本次新增）

- **两套独立主题**：安装器主题、卸载器主题各自配置，互不影响。
- **配色**：主色 / 副色（按钮渐变、进度条、焦点、复选框）+ 侧栏主色 / 侧栏副色（侧栏渐变）。
  8 套预设：经典蓝紫、深海蓝、翠绿、落日橙、品红、石墨灰、玫瑰红、青色，也可逐个取色自定义。
- **背景图**：支持 PNG / JPG / BMP / WEBP / GIF，可选填充方式与不透明度（0–100%）。
  存在背景图时侧栏与底部按钮栏自动半透明，让背景透出。
- **实时预览**：打包器右侧按同一套绘制代码渲染缩略图，与安装器 / 卸载器实际窗口一致。
- **体积与启动速度友好**：背景图在打包时按最长边 2560 缩放并重编码（无透明通道时 PNG/JPEG 取更小者）；
  运行时只读封包尾部的「主题块」，不加载 ZIP 负载。

## Python 打包（随包携带解释器）

勾选打包器里的「Python 打包」页即可把 Python 项目打包成普通 Windows 应用。

- **探测解释器**：注册表 + PATH + 常见安装目录 + `py -0p`；本机无需预装工具链。
- **产物布局**（安装目录）：
  ```
  <AppName>.exe      ESPLauncher 复本（入口；可选替换为应用自带 .ico）—— 进程名就是应用名
  launch.ini         Dll / Script / Args / Home（UTF-16，非 ASCII 路径安全）
  python3XX.dll  DLLs\  Lib\  tcl\     随包携带的解释器运行时（默认不含 site-packages）
  app\               用户项目文件（入口脚本默认 app\main.py）
  ```
- **实现方式**：启动器在**本进程内** `LoadLibrary(python3XX.dll)` + `Py_Main(...)`，
  不产生子进程 —— 任务管理器/卸载器看到的进程名就是 `<AppName>.exe`，
  卸载时按进程名结束既准确又安全（不会误杀其它 python.exe），也便于
  `sys.executable` / `multiprocessing` 正常工作。
- **命令行规则**：无参数 → 运行 `launch.ini` 的入口脚本（可带 Args）；有参数 → 原样透传
  给解释器（等价 `python.exe`，`-c` / `-m` 都可用）。
- **可选**：显示控制台窗口（控制台子系统启动器）/ 包含 site-packages / 应用图标（打包时
  用 `UpdateResource` 写入启动器，exe、快捷方式、控制面板、安装向导全部显示该图标）。
- **体积参考**：仅标准库约 36 MB（解压后）/ 44 MB 安装包（含项目文件）；勾选 site-packages 会显著增大。
- **反阻塞**：解释器探测只在打开该页/点击「重新探测」时进行，且全部在后台线程（8 秒超时）；
  **Microsoft Store 别名（%LOCALAPPDATA%\Microsoft\WindowsApps\python*.exe）与 0 字节占位文件直接跳过**
  —— 调用它们会长时间不返回，曾导致界面「未响应」。
- **限制**：不随包携带 `python.exe` / `pythonw.exe`（入口固定为 `<AppName>.exe`）；
  若项目里恰好有名为 `Lib` / `DLLs` / `tcl` 的目录会与运行时冲突（打包时会提示）。

## 封包格式 ESPPKG01

```
[安装器 exe 原样][ZIP 负载][主题块][JSON 索引（4 字节长度前缀）]["ESPPKG01"]
```

- ZIP 负载 = 应用文件 + `__esp_uninstall/ESPUninstall.exe`（安装时解压为 `uninstall.exe`）
- 主题块 = `"ESPTHEME"` + 安装器图片长度 + 卸载器图片长度 + 两张图片字节（可为空）
- 索引 JSON：`format / payloadOffset / payloadSize / themeOffset / themeSize /
  theme{installer{primary,secondary,sidebarStart,sidebarEnd,bgFit,bgOpacity}, uninstaller{…}} /
  appName / version / publisher / appId / mainExe / iconPng`

旧封包（无主题块）仍可正常安装，行为与之前一致。

## 安装产物（与 ProgramReg 一致）

- 注册表：`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\<appId>`
  （DisplayName / DisplayVersion / Publisher / DisplayIcon / InstallLocation /
  UninstallString / QuietUninstallString / NoModify / NoRepair / EstimatedSize，
  按主程序 PE 架构选择 32/64 位视图）
- `uninstall.ini`：AppInfo + Cleanup 段（StartMenuFolder / RegistryKeyName / RegistryView /
  PinTaskbar / DesktopShortcut / DeleteInstallDir / ExtraPath0..n）
  **+ Theme 段**（Primary / Secondary / SidebarStart / SidebarEnd / BgImage / BgFit / BgOpacity）
- 卸载器背景图落盘为安装目录下的 `esp_theme_bg.png`（或 `.jpg`），卸载时随目录一起删除
- 快捷方式：开始菜单（所有用户）、桌面；任务栏固定（Shell 动词，Win11 部分版本不支持则提示手动）

## 界面响应性（防「未响应」）

- 压缩 / 解压 / 安装 / 卸载全部在工作线程（`QThread`）执行，UI 线程只处理信号与进度；
- **QThread 完成回调一律先 `wait()` 再 `deleteLater()`**：否则工作线程尚在收尾时父对象析构会触发
  `QThread: Destroyed while thread is still running` → `qFatal` → 进程以 **0xC0000409** 退出；
- 背景图解码一律**有界**（`QImageReader::setScaledSize`），不会整图解码超大图片；
  打包器选图时在后台线程读取 + 重编码，读取期间禁用按钮并显示状态；
- 窗口背景图只解码一次并缓存，Cover 模式按尺寸预缩放缓存，重绘不重复缩放；
- 安装日志按 120ms 批量刷新（单次超过 80 行则立即刷新），避免逐条重绘拖慢界面；
- **Python 解释器探测只在打开「Python 打包」页 / 点「重新探测」时进行**，全部在后台线程
  （单个 8 秒超时，卡住的解释器直接 kill），并跳过 Store 别名与 0 字节占位文件；
- Python 运行时文件收集与压缩在打包工作线程完成，进度条实时反映；
- `/auto`（脚本）模式下任何参数校验失败都直接输出 `FAIL <原因>` 并退出，**绝不弹模态框**
  （避免自动化卡在一个没人点的确认框上）；CLI 输出在检测到 stdout 已被重定向时不接管控制台；
- 打包 / 安装 / 卸载进行中禁止关闭窗口，避免线程未结束导致退出异常。

## 构建

```powershell
# 1) 构建三件套（自包含静态 exe，无需 Qt DLL）
powershell -ExecutionPolicy Bypass -File scripts\build.ps1

# 2) 端到端验收（需管理员：写 HKLM / 公共目录）
powershell -ExecutionPolicy Bypass -File scripts\verify.ps1

# 3) 视觉验收：截取安装器 / 卸载器真实窗口
powershell -ExecutionPolicy Bypass -File scripts\capture-setup.ps1 -Exe .verify\out\MyAppTest-Setup.exe -Out shot.png
```

前置条件：
- Visual Studio 2022/2026（MSVC 工具集 + CMake + Ninja，脚本自动定位）
- 静态 Qt（默认 `C:\Qt\6.9.3-static-msvc2022_64`，可用 `-QtPrefix` 覆盖）

发布：三个 exe **加上两个启动器**（ESPLauncher.exe / ESPLauncherC.exe）放在同一目录即可
（打包器从同目录读取 ESPSetup.exe、ESPUninstall.exe 与 Python 启动器）。

## 命令行模式

```powershell
# 打包（含外观主题）
ESPackager.exe /auto /dir=<src> /out=<setup.exe> [/name=..] [/ver=..] [/pub=..] [/mainexe=..] [/appid=..] [/stub=..] `
    [/instpreset=classic|ocean|forest|sunset|magenta|graphite|rose|teal] `
    [/instcolor=#RRGGBB,#RRGGBB[,#RRGGBB,#RRGGBB]]   # 主色,副色[,侧栏主色,侧栏副色]
    [/instbg=<图片>] [/instfit=cover|contain|stretch|tile|center] [/instopacity=0..100] `
    [/uninstpreset=..] [/uninstcolor=..] [/uninstbg=..] [/uninstfit=..] [/uninstopacity=..] `
    [/pypack=1] [/python=<python.exe>] [/pyscript=main.py] [/pyargs=..] [/pyconsole=0|1] `
    [/pysite=0|1] [/pyicon=<app.ico>]

# 列出探测到的 Python 解释器（含被跳过的 Store 别名，便于排查）
ESPackager.exe /list-python

# 导出主题效果图（不打包）：生成 <out>_installer.png / <out>_uninstaller.png
ESPackager.exe /export-preview=<out.png> [/instpreset=..] [/instbg=..] ...

# 静默安装（日志 %TEMP%\ESPSetup.log）
Setup.exe /quiet /dir=<target> /agree [/startmenu=0|1] [/desktop=0|1] [/taskbar=0|1]

# 静默卸载（日志 %TEMP%\ESPUninstall.log）
uninstall.exe /quiet [/ini=path]
```

（GUI 模式也会解析这些参数：直接以命令行启动打包器即可让界面带上指定配置）

## 端到端验证

`scripts\verify.ps1` 覆盖 **71 项**断言：

- 主题部分：构造测试程序 → 生成主题素材图 → 打包（校验 ESPPKG01 尾部索引 + 主题块标记/长度/配色/填充参数）
  → 静默安装（文件 / uninstall.ini `[Theme]` / 背景图落盘且字节数与封包一致 / 注册表 / 快捷方式）
  → 静默卸载（目录含背景图延迟自删 / 注册表 / 快捷方式全部清除）→ 导出主题效果图
- Python 部分：解释器探测（必须跳过 Store 别名）→ 打包 Python 应用（自定义图标 + 入口参数）
  → 静默安装（入口 exe / 随包解释器 / app\ 子目录 / 无 `__esp_uninstall` 残留 / site-packages 默认不含）
  → 运行打包后的应用（输出、模块导入、数据文件、launch.ini 参数、`-c` 透传）→ 图标已替换
  → 启动响应性（窗口 < 4 秒 + 全程 Responding）→ GUI 版应用**运行中卸载**（按进程名结束 + 清理干净）

## 目录结构（参考 VS C++ 桌面模板项目）

```
ExeSimplePackager/
├── CMakeLists.txt
├── Shared/                     共享静态库
│   ├── ThemeSpec.{h,cpp}       主题数据模型：配色/背景图、预设、JSON+INI 序列化、有界解码、背景绘制
│   ├── ThemePreview.{h,cpp}    主题效果预览（与运行时同一套绘制，WYSIWYG）
│   ├── Theme.{h,cpp}           主题化 QSS + 无边框圆角窗口（背景图卡片）
│   ├── PythonEnv.{h,cpp}       Python 解释器发现/校验（跳过 Store 别名，进程调用只在工作线程）
│   ├── PythonPack.{h,cpp}      Python 打包：运行时清单、启动器生成、图标资源替换
│   ├── PkgFormat.{h,cpp}       ESPPKG01 封包读写（含主题块）
│   ├── InstallCore.{h,cpp}     安装核心（含卸载器主题持久化）
│   ├── UninstallRunner.{h,cpp} 卸载核心
│   └── …（ZipHelper/PeInfo/RegOps/Shortcut/ShellPin/IniFile/IconExtract/miniz）
├── Launcher/                   ESPLauncher(.exe / C.exe)：无 Qt 的 ~200 KB 启动器（进程内嵌解释器）
├── Packager/                   ESPackager（ThemeEditor 主题编辑器 + PythonPanel Python 打包页）
├── Setup/                      ESPSetup（提权 manifest，MSI 风格向导）
├── Uninstall/                  ESPUninstall（提权 manifest）
└── scripts/                    build.ps1 / verify.ps1 / capture-setup.ps1 / startup-probe.ps1
                                / gen-launcher-icon.ps1
```

每个 exe 目录含 WindowsProject1 模板风格资源文件：
- `<Name>.rc`：图标（大+小 ID）+ VERSIONINFO 版本信息 + 字符串表
- `resource.h`：资源 ID
- `<Name>.ico`：多尺寸图标

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
- 新增/修改翻译：`lupdate` 提取源码字符串 → 编辑 `assets\i18n\esp_*.ts` → `lrelease` 生成 `.qm`
  → `assets\i18n\i18n.qrc` 已列出全部 qm → 重新构建
- 字符串规范：窗口类用 `tr()`；Shared 静态库用 `QCoreApplication::translate("上下文", ...)`

## 许可说明

Qt 以 LGPL v3 发布；本工具静态链接 Qt，分发时需保留重新链接能力（提供源码与构建脚本，
本项目满足该要求）。miniz 为公共领域软件。
