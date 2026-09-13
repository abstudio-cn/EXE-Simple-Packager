#pragma once
// PeInfo — 解析 PE 头识别可执行文件架构（对应 ProgramReg 的 PeInfo.cs）

#include <QString>

namespace esp {

// 返回 "x64" / "x86" / "arm64" / "unknown"
QString peArchitecture(const QString &exePath);

} // namespace esp
