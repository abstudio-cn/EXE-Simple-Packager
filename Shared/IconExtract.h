#pragma once
// IconExtract — 从 Windows 可执行文件中提取主图标
#include <QPixmap>
#include <QByteArray>

namespace esp {

// 从磁盘上的 exe 提取图标；失败返回空 QPixmap
QPixmap extractExeIcon(const QString &exePath, int size = 64);

// 从磁盘上的 exe 提取图标并编码为 PNG 字节；失败返回空 QByteArray
QByteArray extractExeIconPng(const QString &exePath, int size = 64);

// 从内存中的 exe 数据提取图标（落临时文件）；失败返回空 QPixmap
QPixmap extractExeIconFromData(const QByteArray &exeData, int size = 64);

} // namespace esp
