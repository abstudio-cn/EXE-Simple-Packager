#include "Shortcut.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>

namespace esp {

namespace {
QString tr_(const char *s)
{
    return QCoreApplication::translate("Shortcut", s);
}
}

bool createShortcut(const QString &targetExe, const QString &lnkPath,
                    const QString &workDir, const QString &description,
                    QString *error)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IShellLinkW *shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, reinterpret_cast<void **>(&shellLink));
    if (FAILED(hr) || !shellLink) {
        if (error) *error = tr_("创建 ShellLink COM 对象失败 (0x%1)。").arg(ulong(hr), 8, 16, QLatin1Char('0'));
        return false;
    }

    shellLink->SetPath(targetExe.toStdWString().c_str());
    shellLink->SetWorkingDirectory(workDir.toStdWString().c_str());
    shellLink->SetDescription(description.toStdWString().c_str());

    IPersistFile *persist = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist));
    bool ok = false;
    if (SUCCEEDED(hr) && persist) {
        QDir().mkpath(QFileInfo(lnkPath).absolutePath());
        hr = persist->Save(lnkPath.toStdWString().c_str(), TRUE);
        ok = SUCCEEDED(hr);
        persist->Release();
    }
    shellLink->Release();
    if (!ok && error)
        *error = tr_("保存快捷方式失败 (0x%1)：%2").arg(ulong(hr), 8, 16, QLatin1Char('0')).arg(lnkPath);
    return ok;
}

} // namespace esp
