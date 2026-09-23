#pragma once
// PythonEnv — Python 解释器发现与校验
//
// 硬约束（源自 2026-09-21 的「未响应」事故）：
//  * 候选发现只做文件系统 / 注册表扫描，**不启动任何进程**，可在任意线程安全调用；
//  * 任何会启动进程的动作（--version、py -0p）**只能在后台线程**执行，且必须带超时；
//  * 必须跳过 Microsoft Store 应用执行别名（%LOCALAPPDATA%\Microsoft\WindowsApps\python*.exe）
//    与 0 字节占位文件 —— 调用它们会长时间不返回，UI 线程上一调就「未响应」。

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace esp {

struct PythonInterpreter
{
    QString exePath;               // python.exe 完整路径
    QString home;                  // 安装根目录
    QString dllName;               // 运行库文件名（位于 home，如 python313.dll）
    QString version;               // 已知版本（目录名/注册表推断，可能为空）
    bool    fromLauncher = false;  // 由 py.exe 启动器列出
    bool    usable = true;         // 是否可用（假 python 为 false）
    QString note;                  // 不可用原因或来源说明
};

} // namespace esp

Q_DECLARE_METATYPE(esp::PythonInterpreter)

namespace esp {

// 候选发现（纯扫描，不启动进程）：注册表 → PATH → 常见安装目录
QVector<PythonInterpreter> findPythonCandidates();
// 追加 py.exe 启动器列出的解释器（**会启动进程**，只能在工作线程调用）
void appendLauncherCandidates(QVector<PythonInterpreter> *list, int timeoutMs = 8000);

// 判定是否为「假 python」（Store 别名 / 0 字节占位 / 重解析点）。reason 返回原因。
bool isUnusablePython(const QString &exePath, QString *reason);

// home 下运行库文件名（python3XX.dll 优先，其次 python3.dll）；找不到返回空
QString findRuntimeDllName(const QString &home);

// 目录名推断版本（Python313 → 3.13）；推断不出返回空
QString versionFromPath(const QString &path);

// 启动进程校验（**仅限工作线程**）：返回形如 "3.13.13" 的版本号
bool probePythonVersion(const QString &exePath, QString *version, QString *error, int timeoutMs = 8000);

} // namespace esp
