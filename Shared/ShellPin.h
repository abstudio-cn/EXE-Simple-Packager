#pragma once
// ShellPin — 通过 Shell 动词「固定到任务栏/取消固定」实现任务栏固定
// （对应 ProgramReg 的 ShellPin.cs；Win11 部分版本不暴露动词，失败返回 false）

#include <QString>

namespace esp {

bool toggleTaskbarPin(const QString &path, bool pin);

} // namespace esp
