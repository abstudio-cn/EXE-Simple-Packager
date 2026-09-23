#include "ZipHelper.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "thirdparty/miniz.h"
#include <QCoreApplication>

namespace esp {

namespace {
QString tr_(const char *s)
{
    return QCoreApplication::translate("ZipHelper", s);
}
}

static QString toArchivePath(const QString &native)
{
    QString p = native;
    p.replace('\\', '/');
    while (p.startsWith('/'))
        p.remove(0, 1);
    return p;
}

void collectDirEntries(const QString &dir, QVector<ZipEntry> *entries, QString *error)
{
    const QDir root(dir);
    if (!root.exists()) {
        if (error) *error = tr_("目录不存在：%1").arg(dir);
        return;
    }
    // BFS，保证目录条目先于其内容
    QVector<QString> pending{dir};
    while (!pending.isEmpty()) {
        const QString cur = pending.takeFirst();
        const QFileInfoList list = QDir(cur).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : list) {
            ZipEntry e;
            e.name = toArchivePath(root.relativeFilePath(fi.absoluteFilePath()));
            e.isDir = fi.isDir();
            e.size = quint64(fi.size());
            entries->append(e);
            if (fi.isDir())
                pending.append(fi.absoluteFilePath());
        }
    }
}

QByteArray zipDirectory(const QString &dir,
                        const std::function<void(int, int)> &progress,
                        QString *error,
                        const QString &prefix)
{
    QVector<ZipEntry> entries;
    collectDirEntries(dir, &entries, error);
    if (error && !error->isEmpty())
        return {};

    // 过滤出文件数（目录也写入归档，便于保留空目录）
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        if (error) *error = tr_("初始化 ZIP 写入器失败。");
        return {};
    }

    int fileDone = 0, fileTotal = 0;
    for (const ZipEntry &e : entries)
        if (!e.isDir) fileTotal++;

    bool ok = true;
    for (const ZipEntry &e : entries) {
        const QString absPath = QDir(dir).filePath(QString(e.name).replace('/', QDir::separator()));
        const QString arcName = prefix + e.name;
        if (e.isDir) {
            if (!mz_zip_writer_add_mem(&zip, qPrintable(arcName + '/'), nullptr, 0,
                                       MZ_BEST_COMPRESSION)) {
                ok = false;
                if (error) *error = tr_("写入目录条目失败：%1").arg(arcName);
                break;
            }
        } else {
            QFile f(absPath);
            if (!f.open(QIODevice::ReadOnly)) {
                ok = false;
                if (error) *error = tr_("读取文件失败：%1").arg(absPath);
                break;
            }
            const QByteArray data = f.readAll();
            f.close();
            if (!mz_zip_writer_add_mem(&zip, qPrintable(arcName), data.constData(), data.size(),
                                       MZ_BEST_COMPRESSION)) {
                ok = false;
                if (error) *error = tr_("压缩文件失败：%1").arg(arcName);
                break;
            }
            fileDone++;
            if (progress) progress(fileDone, fileTotal);
        }
    }

    if (!ok) {
        mz_zip_writer_end(&zip);
        return {};
    }
    void *buf = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buf, &size) || !buf) {
        mz_zip_writer_end(&zip);
        if (error) *error = tr_("完成 ZIP 归档失败。");
        return {};
    }
    QByteArray result(static_cast<const char *>(buf), qsizetype(size));
    mz_zip_writer_end(&zip); // 内部 free(buf)
    return result;
}

QByteArray zipDirectoryWithExtras(
    const QString &dir,
    const QVector<QPair<QString, QString>> &extraFiles,
    const std::function<void(int, int)> &progress,
    QString *error,
    const QString &prefix)
{
    QVector<ZipEntry> entries;
    collectDirEntries(dir, &entries, error);
    if (error && !error->isEmpty())
        return {};

    int fileTotal = 0;
    for (const ZipEntry &e : entries)
        if (!e.isDir) fileTotal++;
    fileTotal += extraFiles.size();

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        if (error) *error = tr_("初始化 ZIP 写入器失败。");
        return {};
    }

    int fileDone = 0;
    bool ok = true;
    auto addFile = [&](const QString &archivePath, const QString &diskPath) {
        QFile f(diskPath);
        if (!f.open(QIODevice::ReadOnly)) {
            ok = false;
            if (error) *error = tr_("读取文件失败：%1").arg(diskPath);
            return;
        }
        const QByteArray data = f.readAll();
        f.close();
        if (!mz_zip_writer_add_mem(&zip, qPrintable(archivePath), data.constData(), data.size(),
                                   MZ_BEST_COMPRESSION)) {
            ok = false;
            if (error) *error = tr_("压缩文件失败：%1").arg(archivePath);
            return;
        }
        fileDone++;
        if (progress) progress(fileDone, fileTotal);
    };

    // 额外文件先写入（__esp_uninstall/ 运行时）
    for (const auto &pair : extraFiles)
        addFile(pair.first, pair.second);
    if (!ok) {
        mz_zip_writer_end(&zip);
        return {};
    }

    for (const ZipEntry &e : entries) {
        const QString absPath = QDir(dir).filePath(QString(e.name).replace('/', QDir::separator()));
        const QString arcName = prefix + e.name;
        if (e.isDir) {
            if (!mz_zip_writer_add_mem(&zip, qPrintable(arcName + '/'), nullptr, 0,
                                       MZ_BEST_COMPRESSION)) {
                ok = false;
                if (error) *error = tr_("写入目录条目失败：%1").arg(arcName);
                break;
            }
        } else {
            addFile(arcName, absPath);
            if (!ok) break;
        }
    }

    if (!ok) {
        mz_zip_writer_end(&zip);
        return {};
    }
    void *buf = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buf, &size) || !buf) {
        mz_zip_writer_end(&zip);
        if (error) *error = tr_("完成 ZIP 归档失败。");
        return {};
    }
    QByteArray result(static_cast<const char *>(buf), qsizetype(size));
    mz_zip_writer_end(&zip);
    return result;
}

bool listZipEntries(const QByteArray &zipData, QVector<ZipEntry> *entries, QString *error)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, zipData.constData(), size_t(zipData.size()), 0)) {
        if (error) *error = tr_("打开 ZIP 数据失败（数据损坏？）。");
        return false;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            continue;
        ZipEntry e;
        e.name = QString::fromUtf8(st.m_filename);
        e.isDir = mz_zip_reader_is_file_a_directory(&zip, i) != 0;
        e.size = quint64(st.m_uncomp_size);
        entries->append(e);
    }
    mz_zip_reader_end(&zip);
    return true;
}

bool extractEntryToMem(const QByteArray &zipData, const QString &entryName,
                       QByteArray *data, QString *error)
{
    if (entryName.isEmpty())
        return false;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, zipData.constData(), size_t(zipData.size()), 0)) {
        if (error) *error = tr_("打开 ZIP 数据失败（数据损坏？）。");
        return false;
    }

    // 归一化：'\' → '/'，去掉前导斜杠（主程序名可能来自 Windows 路径）
    const auto normalize = [](const QString &p) {
        QString r = p;
        r.replace(QLatin1Char('\\'), QLatin1Char('/'));
        while (r.startsWith(QLatin1Char('/')))
            r.remove(0, 1);
        return r;
    };
    const QString want = normalize(entryName);
    const QString wantFile = want.section(QLatin1Char('/'), -1);

    QString matched;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            continue;
        if (mz_zip_reader_is_file_a_directory(&zip, i))
            continue;
        const QString name = normalize(QString::fromUtf8(st.m_filename));
        // 1) 精确（归一化后）；2) 忽略大小写；3) 按文件名（末段）回退（主程序可能在子目录）
        if (name == want) {
            matched = QString::fromUtf8(st.m_filename);
            break;
        }
        if (matched.isEmpty() && name.compare(want, Qt::CaseInsensitive) == 0)
            matched = QString::fromUtf8(st.m_filename);
        if (matched.isEmpty() && name.section(QLatin1Char('/'), -1).compare(wantFile, Qt::CaseInsensitive) == 0)
            matched = QString::fromUtf8(st.m_filename);
    }
    if (matched.isEmpty()) {
        mz_zip_reader_end(&zip);
        return false;
    }

    const int idx = mz_zip_reader_locate_file(&zip, qPrintable(matched), nullptr, 0);
    if (idx < 0) {
        mz_zip_reader_end(&zip);
        return false;
    }
    mz_zip_archive_file_stat st;
    mz_zip_reader_file_stat(&zip, mz_uint(idx), &st);
    QByteArray out(qsizetype(st.m_uncomp_size), Qt::Uninitialized);
    if (!mz_zip_reader_extract_to_mem(&zip, mz_uint(idx), out.data(), size_t(st.m_uncomp_size), 0)) {
        mz_zip_reader_end(&zip);
        return false;
    }
    mz_zip_reader_end(&zip);
    *data = std::move(out);
    return true;
}

bool unzipToDir(const QByteArray &zipData, const QString &destDir,
                const QString &extractPrefix,
                const std::function<void(int, int)> &progress,
                QString *error,
                const QString &excludePrefix)
{
    QVector<ZipEntry> entries;
    if (!listZipEntries(zipData, &entries, error))
        return false;

    QVector<const ZipEntry *> wanted;
    int fileTotal = 0;
    for (const ZipEntry &e : entries) {
        if (!extractPrefix.isEmpty()) {
            if (!e.name.startsWith(extractPrefix))
                continue;
        }
        if (!excludePrefix.isEmpty() && e.name.startsWith(excludePrefix))
            continue;
        wanted.append(&e);
        if (!e.isDir) fileTotal++;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_mem(&zip, zipData.constData(), size_t(zipData.size()), 0)) {
        if (error) *error = tr_("打开 ZIP 数据失败（数据损坏？）。");
        return false;
    }

    bool ok = true;
    int fileDone = 0;
    const QByteArray prefixUtf8 = extractPrefix.toUtf8();
    for (const ZipEntry *e : wanted) {
        QString rel = e->name;
        if (!extractPrefix.isEmpty()) {
            rel.remove(0, prefixUtf8.size());
            if (rel.isEmpty()) continue; // 前缀目录本身
        }
        const QString absPath = QDir(destDir).filePath(rel);
        if (e->isDir) {
            if (!QDir().mkpath(absPath)) {
                ok = false;
                if (error) *error = tr_("创建目录失败：%1").arg(absPath);
                break;
            }
            continue;
        }
        const int idx = mz_zip_reader_locate_file(&zip, qPrintable(e->name), nullptr, 0);
        if (idx < 0) {
            ok = false;
            if (error) *error = tr_("定位归档条目失败：%1").arg(e->name);
            break;
        }
        mz_zip_archive_file_stat st;
        mz_zip_reader_file_stat(&zip, mz_uint(idx), &st);
        QByteArray data(qsizetype(st.m_uncomp_size), Qt::Uninitialized);
        if (!mz_zip_reader_extract_to_mem(&zip, mz_uint(idx), data.data(), size_t(st.m_uncomp_size), 0)) {
            ok = false;
            if (error) *error = tr_("解压失败：%1").arg(e->name);
            break;
        }
        QDir().mkpath(QFileInfo(absPath).absolutePath());
        QFile f(absPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            ok = false;
            if (error) *error = tr_("写入文件失败：%1").arg(absPath);
            break;
        }
        f.write(data);
        f.close();
        fileDone++;
        if (progress) progress(fileDone, fileTotal);
    }

    mz_zip_reader_end(&zip);
    return ok;
}

} // namespace esp
