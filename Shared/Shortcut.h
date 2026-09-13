#pragma once
// Shortcut — 通过 IShellLink COM 创建 .lnk 快捷方式

#include <QString>

namespace esp {

bool createShortcut(const QString &targetExe, const QString &lnkPath,
                    const QString &workDir, const QString &description,
                    QString *error = nullptr);

} // namespace esp
