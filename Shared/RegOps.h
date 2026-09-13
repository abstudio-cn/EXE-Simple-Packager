#pragma once
// RegOps — Win32 注册表操作封装（HKEY_LOCAL_MACHINE，支持 32/64 视图）

#include <QString>
#include <QStringList>

namespace esp {

// 打开（或创建）HKLM 下子键，按 view 选择注册表视图：
//   "32" → KEY_WOW64_32KEY, "64" → KEY_WOW64_64KEY, 其他 → 默认（跟随本进程）
bool regSetValue(const QString &subKey, const QString &valueName, const QString &value,
                 const QString &view = QString(), QString *error = nullptr);
bool regSetDword(const QString &subKey, const QString &valueName, quint32 value,
                 const QString &view = QString(), QString *error = nullptr);

// 删除子树。missingOk=true 时键不存在不算失败。
bool regDeleteTree(const QString &subKey, bool missingOk, const QString &view = QString(),
                   QString *error = nullptr);

// 键是否存在（读取访问）。
bool regKeyExists(const QString &subKey, const QString &view = QString());

QString regQueryString(const QString &subKey, const QString &valueName,
                       const QString &view = QString());

} // namespace esp
