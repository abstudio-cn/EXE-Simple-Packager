#pragma once
// ZipHelper — 基于 miniz 的内存 ZIP 打包/解压封装

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QPair>
#include <functional>

namespace esp {

struct ZipEntry
{
    QString name;       // 归档内相对路径（'/' 分隔）
    bool isDir = false;
    quint64 size = 0;
};

// 收集目录树条目（递归）。root 本身不参与。
void collectDirEntries(const QString &dir, QVector<ZipEntry> *entries, QString *error);

// 把整个目录树压缩为内存 ZIP。返回空 QByteArray 表示失败（error 有信息）。
// 进度回调：已处理文件数 / 总文件数。
QByteArray zipDirectory(const QString &dir,
                        const std::function<void(int done, int total)> &progress,
                        QString *error);

// 同 zipDirectory，额外附加 extraFiles（archivePath, diskPath）条目（如卸载器运行时）。
QByteArray zipDirectoryWithExtras(
    const QString &dir,
    const QVector<QPair<QString, QString>> &extraFiles,
    const std::function<void(int done, int total)> &progress,
    QString *error);

// 把内存 ZIP 解压到目标目录。返回 false 失败。
// extractPrefix：仅解压该前缀条目并去除前缀（如 "__esp_uninstall/"）；为空则全部解压。
bool unzipToDir(const QByteArray &zipData, const QString &destDir,
                const QString &extractPrefix,
                const std::function<void(int done, int total)> &progress,
                QString *error);

// 列出 ZIP 内条目。
bool listZipEntries(const QByteArray &zipData, QVector<ZipEntry> *entries, QString *error);

// 提取单个条目到内存（按归档内精确名匹配，失败时尝试不区分大小写）。
bool extractEntryToMem(const QByteArray &zipData, const QString &entryName,
                       QByteArray *data, QString *error);

} // namespace esp
