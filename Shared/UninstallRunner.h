#pragma once
// UninstallRunner — 按 uninstall.ini 执行卸载清理（对应 ProgramReg 的 UninstallRunner.cs）：
// 结束进程 → 取消任务栏固定 → 删开始菜单 → 删桌面快捷方式 → 删注册表
// → 删额外路径 → 删安装目录内容（保留自身）→ 安排延迟自删

#include <QString>
#include <QStringList>
#include <functional>

namespace esp {

class UninstallRunner
{
public:
    explicit UninstallRunner(const QString &iniPath,
                             const std::function<void(const QString &)> &log,
                             const std::function<void(int percent)> &progress = nullptr);

    QString appName() const { return appName_; }
    QString installDir() const { return installDir_; }
    int failures() const { return failures_; }
    QStringList manualCleanupItems() const { return manualCleanup_; }

    void run();

private:
    void killMainProcess();
    void unpinFromTaskbar();
    void removeStartMenuFolder();
    void removeDesktopShortcut();
    void removeRegistryKey();
    void removeExtraPaths();
    void deleteInstallDirContents();
    QString prepareUninstallScript();
    void runUninstallScript(const QString &tempScript);
    void scheduleSelfDelete();

    QString iniPath_;
    QString appName_;
    QString installDir_;
    QString mainExe_;
    QString selfExePath_;
    std::function<void(const QString &)> log_;
    std::function<void(int percent)> progress_;
    int failures_ = 0;
    QStringList manualCleanup_;
};

} // namespace esp
