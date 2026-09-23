#include "PythonPack.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>

namespace esp {

namespace {

QString tr_(const char *s)
{
    return QCoreApplication::translate("PythonPack", s);
}

// 运行时可跳过的目录（相对 home，'/' 分隔前缀）：测试套件、缓存、bootstrap 包
const QStringList &skipDirPrefixes()
{
    static const QStringList list = {
        QStringLiteral("Lib/test"),
        QStringLiteral("Lib/ensurepip"),
        QStringLiteral("Lib/idlelib"),
        QStringLiteral("Lib/turtledemo"),
    };
    return list;
}

// Windows 图标组资源 ID（与 Launcher/ESPLauncher.rc 中 IDI_ESPLAUNCHER 一致）
constexpr WORD kIconGroupId = 107;

struct IcoEntry
{
    quint8 w = 0, h = 0, colors = 0, reserved = 0;
    quint16 planes = 1, bitCount = 32;
    quint32 size = 0, offset = 0;
};

bool parseIco(const QByteArray &data, QVector<IcoEntry> *entries, QString *error)
{
    if (data.size() < 6) {
        if (error) *error = tr_("图标文件过小。");
        return false;
    }
    const auto u16 = [&data](int off) {
        return quint16(quint8(data[off])) | (quint16(quint8(data[off + 1])) << 8);
    };
    const auto u32 = [&data](int off) {
        return quint32(quint8(data[off])) | (quint32(quint8(data[off + 1])) << 8)
             | (quint32(quint8(data[off + 2])) << 16) | (quint32(quint8(data[off + 3])) << 24);
    };
    if (u16(0) != 0 || u16(2) != 1) {
        if (error) *error = tr_("不是有效的 .ico 文件。");
        return false;
    }
    const int count = u16(4);
    for (int i = 0; i < count; ++i) {
        const int off = 6 + i * 16;
        if (off + 16 > data.size())
            break;
        IcoEntry e;
        e.w = quint8(data[off]);
        e.h = quint8(data[off + 1]);
        e.colors = quint8(data[off + 2]);
        e.reserved = quint8(data[off + 3]);
        e.planes = u16(off + 4);
        e.bitCount = u16(off + 6);
        e.size = u32(off + 8);
        e.offset = u32(off + 12);
        if (qint64(e.offset) + qint64(e.size) > data.size())
            continue;
        entries->append(e);
    }
    if (entries->isEmpty()) {
        if (error) *error = tr_(".ico 内没有可用图像。");
        return false;
    }
    return true;
}

struct LangCtx
{
    WORD first = 0;
    int count = 0;
};

BOOL CALLBACK langProc(HMODULE, LPCWSTR, LPCWSTR, WORD lang, LONG_PTR ctx)
{
    auto *c = reinterpret_cast<LangCtx *>(ctx);
    if (c->count == 0)
        c->first = lang;
    c->count++;
    return TRUE;
}

void appendU16(QByteArray *b, quint16 v)
{
    b->append(char(v & 0xFF));
    b->append(char((v >> 8) & 0xFF));
}

void appendU32(QByteArray *b, quint32 v)
{
    b->append(char(v & 0xFF));
    b->append(char((v >> 8) & 0xFF));
    b->append(char((v >> 16) & 0xFF));
    b->append(char((v >> 24) & 0xFF));
}

// GetPrivateProfileStringW 只在「带 BOM 的 UTF-16」INI 上正确读取非 ASCII 路径
QByteArray toUtf16LeWithBom(const QString &text)
{
    QByteArray out;
    out.append(char(0xFF));
    out.append(char(0xFE));
    for (const QChar c : text) {
        out.append(char(c.unicode() & 0xFF));
        out.append(char((c.unicode() >> 8) & 0xFF));
    }
    return out;
}

} // namespace

QStringList pythonRuntimeRelPaths(const QString &home, bool includeSitePackages,
                                  quint64 *totalBytes, QStringList *warnings)
{
    QStringList out;
    quint64 bytes = 0;
    const QDir root(home);
    if (!root.exists()) {
        if (warnings) warnings->append(tr_("解释器目录不存在：%1").arg(home));
        return out;
    }

    const auto addDir = [&](const QString &sub, const QStringList &extraSkip = {}) {
        const QString abs = root.filePath(sub);
        if (!QDir(abs).exists())
            return false;
        QDirIterator it(abs, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            const QString rel = root.relativeFilePath(p);
            const QString relPosix = QString(rel).replace(QLatin1Char('\\'), QLatin1Char('/'));
            bool skip = false;
            for (const QString &prefix : skipDirPrefixes()) {
                if (relPosix.startsWith(prefix + QLatin1Char('/')) || relPosix == prefix) {
                    skip = true;
                    break;
                }
            }
            for (const QString &prefix : extraSkip) {
                if (relPosix.startsWith(prefix))
                    skip = true;
            }
            if (!skip && relPosix.contains(QStringLiteral("__pycache__")))
                skip = true;
            if (skip)
                continue;
            out.append(relPosix);
            bytes += quint64(QFileInfo(p).size());
        }
        return true;
    };

    // 1) 顶层运行库（python3XX.dll / python3.dll / vcruntime*.dll 等）
    for (const QFileInfo &fi : root.entryInfoList({QStringLiteral("*.dll")}, QDir::Files)) {
        out.append(fi.fileName());
        bytes += quint64(fi.size());
    }
    // 2) 扩展模块与运行时依赖
    addDir(QStringLiteral("DLLs"));
    // 3) 标准库（site-packages 可选）
    if (includeSitePackages)
        addDir(QStringLiteral("Lib"));
    else
        addDir(QStringLiteral("Lib"), {QStringLiteral("Lib/site-packages")});
    // 4) tkinter 需要的 tcl/tk
    const bool hasTcl = addDir(QStringLiteral("tcl"));
    if (!hasTcl && QFileInfo::exists(root.filePath(QStringLiteral("DLLs/_tkinter.pyd")))) {
        if (warnings) warnings->append(tr_("未找到 tcl 目录，tkinter 界面可能无法启动。"));
    }

    if (!includeSitePackages && QDir(root.filePath(QStringLiteral("Lib/site-packages"))).exists()) {
        if (warnings)
            warnings->append(tr_("已跳过 Lib/site-packages（第三方依赖）；如需随包请勾选「包含 site-packages」。"));
    }
    if (totalBytes)
        *totalBytes = bytes;
    return out;
}

QString launcherTemplatePath(const PythonPackSpec &spec, QString *error)
{
    const QString name = spec.console ? QStringLiteral("ESPLauncherC.exe")
                                      : QStringLiteral("ESPLauncher.exe");
    const QString path = QDir(spec.launcherDir).filePath(name);
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = tr_("缺少启动器 %1（应与 ESPackager.exe 位于同一目录）。").arg(name);
        return {};
    }
    return path;
}

QString entryScriptInstallRelPath(const QString &projectDir, const QString &entryScript)
{
    const QFileInfo fi(entryScript);
    QString rel;
    if (fi.isAbsolute()) {
        rel = QDir(projectDir).relativeFilePath(fi.absoluteFilePath());
        if (rel.startsWith(QLatin1String("..")))
            rel = fi.fileName();   // 不在项目目录内：退化为文件名
    } else {
        rel = entryScript;
    }
    rel.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return QStringLiteral("app\\") + rel;
}

bool preparePythonLauncher(const PythonPackSpec &spec, const QString &destDir,
                           QString *launcherPath, QString *error, QStringList *warnings)
{
    QString err;
    const QString tpl = launcherTemplatePath(spec, &err);
    if (tpl.isEmpty()) {
        if (error) *error = err;
        return false;
    }
    if (!QDir().mkpath(destDir)) {
        if (error) *error = tr_("无法创建临时目录：%1").arg(destDir);
        return false;
    }

    const QString target = QDir(destDir).filePath(spec.appName + QStringLiteral(".exe"));
    QFile::remove(target);
    if (!QFile::copy(tpl, target)) {
        if (error) *error = tr_("复制启动器失败：%1").arg(target);
        return false;
    }

    if (!spec.iconPath.isEmpty() && QFileInfo::exists(spec.iconPath)) {
        QString iconErr;
        if (!replaceExeIcon(target, spec.iconPath, &iconErr)) {
            // 图标替换失败不阻断打包：使用启动器默认图标
            if (warnings)
                *warnings << tr_("图标替换失败（已使用默认图标）：%1").arg(iconErr);
        }
    }

    // launch.ini（UTF-16LE + BOM，保证中文/非 ASCII 路径正确）
    const QString home = QFileInfo(spec.interpreterExe).absolutePath();
    QString dll;
    for (const QFileInfo &fi : QDir(home).entryInfoList({QStringLiteral("python3*.dll")}, QDir::Files)) {
        const QString n = fi.fileName().toLower();
        if (n.size() > 8 && n.at(7).isDigit()) {
            dll = fi.fileName();
            break;
        }
        if (dll.isEmpty())
            dll = fi.fileName();
    }
    if (dll.isEmpty()) {
        if (error) *error = tr_("解释器目录中没有 python3XX.dll：%1").arg(home);
        return false;
    }

    QString ini;
    ini += QStringLiteral("[Python]\n");
    ini += QStringLiteral("Dll=%1\n").arg(dll);
    ini += QStringLiteral("Script=%1\n").arg(entryScriptInstallRelPath(spec.projectDir, spec.entryScript));
    ini += QStringLiteral("Args=%1\n").arg(spec.args);
    ini += QStringLiteral("Home=\n");

    const QString iniPath = QDir(destDir).filePath(QStringLiteral("launch.ini"));
    QFile f(iniPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = tr_("无法写入 launch.ini：%1").arg(iniPath);
        return false;
    }
    f.write(toUtf16LeWithBom(ini));
    f.close();

    if (launcherPath)
        *launcherPath = target;
    return true;
}

bool replaceExeIcon(const QString &exePath, const QString &icoPath, QString *error)
{
    QFile icoFile(icoPath);
    if (!icoFile.open(QIODevice::ReadOnly)) {
        if (error) *error = tr_("无法读取图标：%1").arg(icoPath);
        return false;
    }
    const QByteArray ico = icoFile.readAll();
    icoFile.close();

    QVector<IcoEntry> entries;
    if (!parseIco(ico, &entries, error))
        return false;

    // 1) 读现有图标组的 RT_ICON 编号与语言（就地覆盖，避免残留无用资源）
    QVector<quint16> existingIds;
    WORD lang = 0x0409;
    {
        HMODULE mod = LoadLibraryExW(reinterpret_cast<const wchar_t *>(exePath.utf16()),
                                     nullptr, LOAD_LIBRARY_AS_DATAFILE);
        if (mod) {
            LangCtx ctx;
            EnumResourceLanguagesW(mod, RT_GROUP_ICON, MAKEINTRESOURCEW(kIconGroupId), langProc,
                                   reinterpret_cast<LONG_PTR>(&ctx));
            if (ctx.count > 0)
                lang = ctx.first;
            HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(kIconGroupId), RT_GROUP_ICON);
            if (res) {
                const void *p = LockResource(LoadResource(mod, res));
                const DWORD sz = SizeofResource(mod, res);
                if (p && sz >= 6) {
                    const quint16 count = quint16(quint8(((const char *)p)[4]))
                                        | (quint16(quint8(((const char *)p)[5])) << 8);
                    for (int i = 0; i < count && (6 + (i + 1) * 14) <= int(sz); ++i) {
                        const char *e = static_cast<const char *>(p) + 6 + i * 14;
                        existingIds.append(quint16(quint8(e[12])) | (quint16(quint8(e[13])) << 8));
                    }
                }
            }
            FreeLibrary(mod);
        }
    }

    // 2) 写入
    HANDLE upd = BeginUpdateResourceW(reinterpret_cast<const wchar_t *>(exePath.utf16()), FALSE);
    if (!upd) {
        if (error) *error = tr_("无法打开可执行文件以替换图标（错误码 %1）。").arg(GetLastError());
        return false;
    }

    QByteArray group;
    appendU16(&group, 0);
    appendU16(&group, 1);
    appendU16(&group, quint16(entries.size()));

    quint16 nextFree = 200;
    bool ok = true;
    for (int i = 0; i < entries.size() && ok; ++i) {
        const IcoEntry &e = entries.at(i);
        const quint16 id = (i < existingIds.size()) ? existingIds.at(i) : nextFree++;
        const QByteArray img = ico.mid(int(e.offset), int(e.size));
        if (img.isEmpty()
            || !UpdateResourceW(upd, RT_ICON, MAKEINTRESOURCEW(id), lang,
                                const_cast<char *>(img.constData()), DWORD(img.size()))) {
            ok = false;
            break;
        }
        group.append(char(e.w));
        group.append(char(e.h));
        group.append(char(e.colors));
        group.append(char(e.reserved));
        appendU16(&group, e.planes);
        appendU16(&group, e.bitCount);
        appendU32(&group, e.size);
        appendU16(&group, id);
    }

    if (ok && !UpdateResourceW(upd, RT_GROUP_ICON, MAKEINTRESOURCEW(kIconGroupId), lang,
                               group.data(), DWORD(group.size())))
        ok = false;

    if (!EndUpdateResourceW(upd, !ok) && ok)
        ok = false;

    if (!ok && error)
        *error = tr_("写入图标资源失败（错误码 %1）。").arg(GetLastError());
    return ok;
}

} // namespace esp
