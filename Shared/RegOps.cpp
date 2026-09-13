#include "RegOps.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <QString>
#include <QCoreApplication>

namespace esp {

namespace {
QString tr_(const char *s)
{
    return QCoreApplication::translate("RegOps", s);
}
}

static REGSAM viewFlags(const QString &view)
{
    if (view == QLatin1String("32"))
        return KEY_WOW64_32KEY;
    if (view == QLatin1String("64"))
        return KEY_WOW64_64KEY;
    return 0;
}

static QString winErrorText(LONG rc)
{
    wchar_t buf[512] = {0};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, DWORD(rc), 0, buf, 511, nullptr);
    const QString msg = QString::fromWCharArray(buf).trimmed();
    return tr_("错误 %1：%2").arg(rc).arg(msg);
}

bool regSetValue(const QString &subKey, const QString &valueName, const QString &value,
                 const QString &view, QString *error)
{
    HKEY key = nullptr;
    const std::wstring sk = subKey.toStdWString();
    LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE | viewFlags(view),
                              nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        if (error) *error = tr_("创建注册表键失败 %1：").arg(subKey) + winErrorText(rc);
        return false;
    }
    const std::wstring vn = valueName.toStdWString();
    const std::wstring vv = value.toStdWString();
    rc = RegSetValueExW(key, vn.c_str(), 0, REG_SZ,
                        reinterpret_cast<const BYTE *>(vv.c_str()),
                        DWORD((vv.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        if (error) *error = tr_("写入注册表值失败 %1\\%2：").arg(subKey, valueName) + winErrorText(rc);
        return false;
    }
    return true;
}

bool regSetDword(const QString &subKey, const QString &valueName, quint32 value,
                 const QString &view, QString *error)
{
    HKEY key = nullptr;
    const std::wstring sk = subKey.toStdWString();
    LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE | viewFlags(view),
                              nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        if (error) *error = tr_("创建注册表键失败 %1：").arg(subKey) + winErrorText(rc);
        return false;
    }
    const std::wstring vn = valueName.toStdWString();
    const DWORD v = DWORD(value);
    rc = RegSetValueExW(key, vn.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE *>(&v), sizeof(v));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        if (error) *error = tr_("写入注册表值失败 %1\\%2：").arg(subKey, valueName) + winErrorText(rc);
        return false;
    }
    return true;
}

static LONG deleteTreeRecurse(HKEY root, const std::wstring &sub)
{
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ | KEY_WRITE, &key);
    if (rc != ERROR_SUCCESS)
        return rc;
    wchar_t name[256];
    DWORD nameLen = 0;
    while ((rc = RegEnumKeyExW(key, 0, name, &nameLen, nullptr, nullptr, nullptr, nullptr)) == ERROR_SUCCESS) {
        nameLen = 256;
        deleteTreeRecurse(root, sub + L"\\" + name);
    }
    RegCloseKey(key);
    return RegDeleteKeyW(root, sub.c_str());
}

bool regDeleteTree(const QString &subKey, bool missingOk, const QString &view, QString *error)
{
    const std::wstring sk = subKey.toStdWString();
    LONG rc = deleteTreeRecurse(HKEY_LOCAL_MACHINE, sk);
    if (rc == ERROR_FILE_NOT_FOUND && missingOk)
        return true;
    if (rc != ERROR_SUCCESS) {
        if (error) *error = tr_("删除注册表键失败 %1：").arg(subKey) + winErrorText(rc);
        return false;
    }
    return true;
}

bool regKeyExists(const QString &subKey, const QString &view)
{
    HKEY key = nullptr;
    const std::wstring sk = subKey.toStdWString();
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), 0,
                            KEY_READ | viewFlags(view), &key);
    if (rc != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

QString regQueryString(const QString &subKey, const QString &valueName, const QString &view)
{
    HKEY key = nullptr;
    const std::wstring sk = subKey.toStdWString();
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, sk.c_str(), 0,
                            KEY_READ | viewFlags(view), &key);
    if (rc != ERROR_SUCCESS)
        return {};
    const std::wstring vn = valueName.toStdWString();
    wchar_t buf[1024] = {0};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    rc = RegQueryValueExW(key, vn.c_str(), nullptr, &type,
                          reinterpret_cast<BYTE *>(buf), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    return QString::fromWCharArray(buf);
}

} // namespace esp
