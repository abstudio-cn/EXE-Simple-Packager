#include "InstallCore.h"

#include "ZipHelper.h"
#include "PeInfo.h"
#include "RegOps.h"
#include "Shortcut.h"
#include "ShellPin.h"
#include "IniFile.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QCoreApplication>

namespace esp {

namespace {
// i18n：静态库无 QObject 上下文，统一用 translate
QString tr_(const char *s)
{
    return QCoreApplication::translate("InstallCore", s);
}

QString commonProgramsDir()
{
    wchar_t buf[MAX_PATH] = {0};
    if (SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK)
        return {};
    return QString::fromWCharArray(buf);
}

QString commonDesktopDir()
{
    wchar_t buf[MAX_PATH] = {0};
    if (SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK)
        return {};
    return QString::fromWCharArray(buf);
}

quint64 estimateDirSizeKb(const QString &dir)
{
    quint64 total = 0;
    QDir d(dir);
    if (!d.exists())
        return 0;
    for (const QFileInfo &fi : d.entryInfoList(QDir::Files | QDir::Hidden | QDir::System,
                                               QDir::DirsFirst | QDir::Name)) {
        if (fi.isDir()) {
            total += estimateDirSizeKb(fi.absoluteFilePath());
        } else {
            total += quint64(fi.size());
        }
    }
    return total;
}

QString samePathTrim(const QString &p)
{
    return QDir::cleanPath(p).toLower().replace('/', '\\');
}

bool samePath(const QString &a, const QString &b)
{
    return samePathTrim(a) == samePathTrim(b);
}
} // namespace

int runInstall(const QByteArray &zipData, const InstallOptions &o,
               const std::function<void(const QString &)> &log,
               const std::function<void(int percent)> &progress,
               QStringList *warningsOut)
{
    int failures = 0;
    auto setP = [&](int p) { if (progress) progress(p); };
    auto warn = [&](const QString &w) {
        if (warningsOut) warningsOut->append(w);
        log(tr_("[警告] ") + w);
    };
    const QString installDir = QDir::cleanPath(o.installDir);

    log(tr_("开始安装 %1 %2 ...").arg(o.appName, o.version));
    setP(3);

    // 1. 创建安装目录
    if (!QDir().mkpath(installDir)) {
        log(tr_("[错误] 创建安装目录失败：%1").arg(installDir));
        return 1;
    }

    // 2. 解压负载：__esp_uninstall/ 前缀 → 安装目录根（卸载器运行时），其余 → 安装目录
    QString err;
    if (!unzipToDir(zipData, installDir, QStringLiteral("__esp_uninstall/"),
                    [&](int done, int total) {
                        if (total > 0)
                            setP(4 + int(done * 10 / total)); // 4-14%
                    }, &err)) {
        log(tr_("[错误] 解压卸载器运行时失败：") + err);
        failures++;
    }
    if (!unzipToDir(zipData, installDir, QString(), nullptr, &err)) {
        log(tr_("[错误] 解压应用文件失败：") + err);
        failures++;
    } else {
        log(tr_("已解压全部应用文件。"));
    }
    if (failures > 0)
        return failures;
    setP(70);

    // 卸载器改名为 uninstall.exe
    const QString deployedUninstaller = QDir(installDir).filePath(QStringLiteral("ESPUninstall.exe"));
    const QString finalUninstaller = QDir(installDir).filePath(QStringLiteral("uninstall.exe"));
    if (QFile::exists(deployedUninstaller)) {
        if (QFile::exists(finalUninstaller))
            QFile::remove(finalUninstaller);
        if (!QFile::rename(deployedUninstaller, finalUninstaller))
            warn(tr_("重命名卸载器为 uninstall.exe 失败，注册表将指向 ESPUninstall.exe。"));
    }

    // 3. 架构 → 注册表视图
    QString arch = o.mainExeName.isEmpty() ? QStringLiteral("unknown")
                   : peArchitecture(QDir(installDir).filePath(o.mainExeName));
    if (arch == QLatin1String("unknown")) {
        arch = QStringLiteral("x64");
        warn(tr_("无法识别主程序架构（%1），默认按 64 位注册表视图处理。").arg(o.mainExeName));
    }
    const QString view = (arch == QLatin1String("x86")) ? QStringLiteral("32") : QStringLiteral("64");
    setP(78);

    // 4. 生成 uninstall.ini
    const QString iniPath = QDir(installDir).filePath(QStringLiteral("uninstall.ini"));
    IniFile ini(iniPath);
    ini.set(QStringLiteral("AppInfo"), QStringLiteral("AppName"), o.appName);
    ini.set(QStringLiteral("AppInfo"), QStringLiteral("MainExe"), o.mainExeName);
    ini.set(QStringLiteral("Cleanup"), QStringLiteral("InstallDir"), installDir);
    ini.set(QStringLiteral("Cleanup"), QStringLiteral("StartMenuFolder"),
            o.startMenuShortcut ? o.appName : QString());
    ini.set(QStringLiteral("Cleanup"), QStringLiteral("RegistryKeyName"), o.appId);
    ini.set(QStringLiteral("Cleanup"), QStringLiteral("RegistryView"), view);
    ini.set(QStringLiteral("Cleanup"), QStringLiteral("PinTaskbar"), o.taskbarShortcut ? QStringLiteral("1") : QStringLiteral("0"));
    ini.set(QStringLiteral("Cleanup"), QStringLiteral("DesktopShortcut"), o.desktopShortcut ? QStringLiteral("1") : QStringLiteral("0"));
    ini.set(QStringLiteral("Cleanup"), QStringLiteral("InstallScript"), QString());
    ini.set(QStringLiteral("Cleanup"), QStringLiteral("UninstallScript"), QString());
    ini.set(QStringLiteral("Cleanup"), QStringLiteral("DeleteInstallDir"), QStringLiteral("1"));
    if (!ini.save(&err)) {
        log(tr_("[错误] 写入卸载配置失败：") + err);
        failures++;
    } else {
        log(tr_("已写入卸载配置 uninstall.ini。"));
    }

    // 5. 注册表卸载项（控制面板）
    const QString uninstallKey = QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + o.appId;
    const QString uninstallExe = QFile::exists(finalUninstaller) ? finalUninstaller : deployedUninstaller;
    const QString mainExeFull = o.mainExeName.isEmpty() ? QString()
                                : QDir(installDir).filePath(o.mainExeName);

    if (regSetValue(uninstallKey, QStringLiteral("DisplayName"), o.appName, view, &err)
        && regSetValue(uninstallKey, QStringLiteral("DisplayVersion"),
                       o.version.isEmpty() ? QStringLiteral("1.0.0") : o.version, view, &err)
        && regSetValue(uninstallKey, QStringLiteral("Publisher"), o.publisher, view, &err)
        && regSetValue(uninstallKey, QStringLiteral("DisplayIcon"), mainExeFull, view, &err)
        && regSetValue(uninstallKey, QStringLiteral("InstallLocation"), installDir, view, &err)
        && regSetValue(uninstallKey, QStringLiteral("UninstallString"),
                       QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(uninstallExe)), view, &err)
        && regSetValue(uninstallKey, QStringLiteral("QuietUninstallString"),
                       QStringLiteral("\"%1\" /quiet").arg(QDir::toNativeSeparators(uninstallExe)), view, &err)
        && regSetDword(uninstallKey, QStringLiteral("NoModify"), 1, view, &err)
        && regSetDword(uninstallKey, QStringLiteral("NoRepair"), 1, view, &err)
        && regSetDword(uninstallKey, QStringLiteral("EstimatedSize"),
                       quint32(qMin<quint64>(estimateDirSizeKb(installDir) / 1024, 0x7FFFFFFF)), view, &err)) {
        log(tr_("已写入注册表卸载项（%1 视图）。").arg(view));
    } else {
        log(tr_("[错误] 写入注册表卸载项失败：") + err);
        failures++;
    }
    setP(85);

    // 6. 开始菜单快捷方式
    QString startMenuLnk;
    if (o.startMenuShortcut && !mainExeFull.isEmpty()) {
        startMenuLnk = QDir(commonProgramsDir()).filePath(o.appName + QStringLiteral("/") + o.appName + QStringLiteral(".lnk"));
        if (createShortcut(mainExeFull, startMenuLnk, installDir, o.appName, &err))
            log(tr_("已创建开始菜单快捷方式。"));
        else {
            log(tr_("[错误] 创建开始菜单快捷方式失败：") + err);
            failures++;
        }
    }

    // 7. 桌面快捷方式
    if (o.desktopShortcut && !mainExeFull.isEmpty()) {
        const QString desktopLnk = QDir(commonDesktopDir()).filePath(o.appName + QStringLiteral(".lnk"));
        if (createShortcut(mainExeFull, desktopLnk, installDir, o.appName, &err))
            log(tr_("已创建桌面快捷方式。"));
        else {
            log(tr_("[错误] 创建桌面快捷方式失败：") + err);
            failures++;
        }
    }
    setP(92);

    // 8. 任务栏固定（优先 .lnk）
    if (o.taskbarShortcut && !mainExeFull.isEmpty()) {
        QString pinTarget = QFile::exists(startMenuLnk) ? startMenuLnk : mainExeFull;
        bool pinned = toggleTaskbarPin(pinTarget, true);
        if (!pinned && startMenuLnk.isEmpty()) {
            const QString lnk = QDir(installDir).filePath(o.appName + QStringLiteral(".lnk"));
            if (createShortcut(mainExeFull, lnk, installDir, o.appName, nullptr))
                pinned = toggleTaskbarPin(lnk, true);
        }
        if (!pinned)
            warn(tr_("未能自动固定到任务栏，请手动右键程序选择「固定到任务栏」。"));
        else
            log(tr_("已固定到任务栏。"));
    }

    log(failures == 0 ? tr_("安装完成。")
                      : tr_("安装流程结束，有 %1 个步骤未成功。").arg(failures));
    setP(100);
    return failures;
}

QString installedMainExe(const QString &installDir)
{
    const QString iniPath = QDir(installDir).filePath(QStringLiteral("uninstall.ini"));
    if (!QFile::exists(iniPath))
        return {};
    IniFile ini(iniPath);
    const QString name = ini.get(QStringLiteral("AppInfo"), QStringLiteral("MainExe"));
    if (name.isEmpty())
        return {};
    return QDir(installDir).filePath(name);
}

} // namespace esp
