#include "IconExtract.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <QPixmap>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QTemporaryFile>

namespace esp {

namespace {
QPixmap iconToPixmap(HICON icon, int size)
{
    QImage img = QImage::fromHICON(icon);
    DestroyIcon(icon);
    if (img.isNull())
        return {};
    if (size > 0 && (img.width() != size || img.height() != size))
        img = img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return QPixmap::fromImage(img);
}
} // namespace

QPixmap extractExeIcon(const QString &exePath, int size)
{
    const QString native = QDir::toNativeSeparators(exePath);
    const LPCWSTR pathW = reinterpret_cast<LPCWSTR>(native.utf16());

    // 1) 优先 PrivateExtractIcons：可取 exe 内嵌的大图标
    HICON icon = nullptr;
    if (PrivateExtractIconsW(pathW, 0, size, size, &icon, nullptr, 1, 0) != 0 && icon)
        return iconToPixmap(icon, size);

    // 2) 回退 SHGetFileInfo（系统 shell 图标缓存）
    SHFILEINFOW sfi;
    ZeroMemory(&sfi, sizeof(sfi));
    const DWORD_PTR ok = SHGetFileInfoW(pathW, FILE_ATTRIBUTE_NORMAL,
                                        &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON);
    if (!ok || !sfi.hIcon)
        return {};
    return iconToPixmap(sfi.hIcon, size);
}

QByteArray extractExeIconPng(const QString &exePath, int size)
{
    const QPixmap pm = extractExeIcon(exePath, size);
    if (pm.isNull())
        return {};
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    pm.save(&buf, "PNG");
    return out;
}

QPixmap extractExeIconFromData(const QByteArray &exeData, int size)
{
    if (exeData.isEmpty())
        return {};
    const QString tmpPath = QDir::temp().filePath(QStringLiteral("esp_icon_XXXXXX.exe"));
    QTemporaryFile f(tmpPath);
    f.setAutoRemove(false);
    if (!f.open())
        return {};
    f.write(exeData);
    f.close();
    const QPixmap pm = extractExeIcon(f.fileName(), size);
    f.remove();
    return pm;
}

} // namespace esp
