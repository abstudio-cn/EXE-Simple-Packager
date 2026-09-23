#include "PythonEnv.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>

#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace esp {

namespace {

QString tr_(const char *s)
{
    return QCoreApplication::translate("PythonEnv", s);
}

// 命中即视为候选的 exe 名
bool looksLikePythonExe(const QString &fileName)
{
    const QString base = QFileInfo(fileName).completeBaseName().toLower();
    return base == QLatin1String("python") || base == QLatin1String("python3")
        || base.startsWith(QLatin1String("python3"));
}

QString normalized(const QString &path)
{
    const QString p = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return p.toLower();
}

void addCandidate(QVector<PythonInterpreter> *list, const QString &exePath, const QString &note)
{
    if (exePath.isEmpty())
        return;
    const QString norm = normalized(exePath);
    for (const PythonInterpreter &e : *list) {
        if (normalized(e.exePath) == norm)
            return;   // 已存在
    }
    PythonInterpreter it;
    it.exePath = QDir::cleanPath(exePath);
    it.home = QFileInfo(it.exePath).absolutePath();
    QString reason;
    if (isUnusablePython(it.exePath, &reason)) {
        it.usable = false;
        it.note = reason;
    } else {
        it.dllName = findRuntimeDllName(it.home);
        if (it.dllName.isEmpty()) {
            it.usable = false;
            it.note = tr_("目录内未找到 python3XX.dll，不是完整的 Python 安装");
        }
    }
    it.version = versionFromPath(it.home);
    if (it.usable && !note.isEmpty())
        it.note = note;
    list->append(it);
}

QStringList registryInstallPaths()
{
    QStringList out;
    const QStringList roots = {
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Python\\PythonCore"),
        QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore"),
        QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\WOW6432Node\\Python\\PythonCore"),
    };
    for (const QString &root : roots) {
        QSettings settings(root, QSettings::NativeFormat);
        for (const QString &ver : settings.childGroups()) {
            // InstallPath 的默认值（"." ）即安装目录
            QSettings ip(root + QLatin1Char('/') + ver + QStringLiteral("/InstallPath"),
                         QSettings::NativeFormat);
            QString dir = ip.value(QStringLiteral(".")).toString();
            if (dir.isEmpty())
                dir = ip.value(QStringLiteral("ExecutablePath")).toString();
            if (dir.isEmpty())
                continue;
            const QFileInfo fi(dir);
            if (fi.isDir())
                out << QDir(dir).filePath(QStringLiteral("python.exe"));
            else
                out << dir;   // 已经是 exe 路径
        }
    }
    return out;
}

QStringList pathDirs()
{
    QStringList out;
    const QString path = QString::fromLocal8Bit(qgetenv("PATH"));
    for (const QString &p : path.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        out << p.trimmed();
    return out;
}

// 扫描常见安装根目录下的 Python3* 子目录
QStringList wellKnownRoots()
{
    QStringList roots;
    const QString local = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!local.isEmpty())
        roots << QDir(local).filePath(QStringLiteral("../Programs/Python"));   // %LOCALAPPDATA%\Programs\Python
    for (const QString &env : {QStringLiteral("ProgramFiles"), QStringLiteral("ProgramFiles(x86)")}) {
        const QString v = qEnvironmentVariable(env.toLatin1().constData());
        if (!v.isEmpty())
            roots << v;
    }
    roots << QStringLiteral("C:\\");
    return roots;
}

} // namespace

bool isUnusablePython(const QString &exePath, QString *reason)
{
    const auto setReason = [reason](const QString &r) {
        if (reason) *reason = r;
        return true;
    };
    if (exePath.isEmpty())
        return setReason(tr_("路径为空"));
    const QFileInfo fi(exePath);
    if (!fi.exists() || !fi.isFile())
        return setReason(tr_("文件不存在"));

    // 1) Microsoft Store 应用执行别名（0 字节占位/reparse point）：调用会长时间不返回
    const QString lower = QDir::toNativeSeparators(fi.absoluteFilePath()).toLower();
    if (lower.contains(QStringLiteral("\\microsoft\\windowsapps\\")))
        return setReason(tr_("Microsoft Store 应用执行别名（不可用）"));

    // 2) 0 字节 / 重解析点占位文件
    const DWORD attrs = GetFileAttributesW(reinterpret_cast<const wchar_t *>(fi.absoluteFilePath().utf16()));
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        return setReason(tr_("重解析点占位文件（不可用）"));
    if (fi.size() <= 0)
        return setReason(tr_("0 字节占位文件（不可用）"));
    return false;
}

QString findRuntimeDllName(const QString &home)
{
    const QDir d(home);
    if (!d.exists())
        return {};
    QString stable;   // python3.dll（稳定 ABI 转发层）
    for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("python3*.dll")}, QDir::Files)) {
        const QString name = fi.fileName().toLower();
        if (name.size() > 8 && name.at(7).isDigit())
            return fi.fileName();   // python313.dll 之类
        if (stable.isEmpty())
            stable = fi.fileName();
    }
    return stable;
}

QString versionFromPath(const QString &path)
{
    static const QRegularExpression re(QStringLiteral("(?:python|Python)\\s*3[._-]?(\\d{1,2})\\b"));
    const QRegularExpressionMatch m = re.match(path);
    if (!m.hasMatch())
        return {};
    return QStringLiteral("3.%1").arg(m.captured(1).toInt());
}

QVector<PythonInterpreter> findPythonCandidates()
{
    QVector<PythonInterpreter> out;

    // 1) 注册表
    for (const QString &exe : registryInstallPaths())
        addCandidate(&out, exe, tr_("来自注册表"));

    // 2) PATH（跳过 Store 别名与 0 字节占位）
    for (const QString &dir : pathDirs()) {
        const QDir d(dir);
        for (const QString &name : {QStringLiteral("python.exe"), QStringLiteral("python3.exe")}) {
            const QString p = d.filePath(name);
            if (QFileInfo::exists(p))
                addCandidate(&out, p, tr_("来自 PATH"));
        }
    }

    // 3) 常见安装目录 Python3*
    for (const QString &root : wellKnownRoots()) {
        const QDir base(root);
        if (!base.exists())
            continue;
        for (const QFileInfo &sub : base.entryInfoList({QStringLiteral("Python3*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString exe = QDir(sub.absoluteFilePath()).filePath(QStringLiteral("python.exe"));
            if (QFileInfo::exists(exe))
                addCandidate(&out, exe, tr_("常见安装目录"));
        }
    }

    // 4) 用户级 Python 安装（%LOCALAPPDATA%\Programs\Python\Python3*）
    const QString localPrograms = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                                      .filePath(QStringLiteral("Programs/Python"));
    for (const QFileInfo &sub : QDir(localPrograms).entryInfoList({QStringLiteral("Python3*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString exe = QDir(sub.absoluteFilePath()).filePath(QStringLiteral("python.exe"));
        if (QFileInfo::exists(exe))
            addCandidate(&out, exe, tr_("用户安装目录"));
    }

    // 可用项排前面
    std::stable_sort(out.begin(), out.end(), [](const PythonInterpreter &a, const PythonInterpreter &b) {
        if (a.usable != b.usable)
            return a.usable;
        return a.home < b.home;
    });
    return out;
}

void appendLauncherCandidates(QVector<PythonInterpreter> *list, int timeoutMs)
{
    if (!list)
        return;
    // py.exe 一般位于 C:\Windows；运行 py -0p 列出所有已安装解释器
    QString py = QDir(QStringLiteral("C:\\Windows")).filePath(QStringLiteral("py.exe"));
    if (!QFileInfo::exists(py)) {
        for (const QString &dir : pathDirs()) {
            const QString cand = QDir(dir).filePath(QStringLiteral("py.exe"));
            if (QFileInfo::exists(cand)) {
                py = cand;
                break;
            }
        }
    }
    if (py.isEmpty() || !QFileInfo::exists(py))
        return;

    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(py, {QStringLiteral("-0p")});
    if (!p.waitForStarted(3000)) {
        p.kill();
        return;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return;
    }
    const QString outText = QString::fromLocal8Bit(p.readAllStandardOutput());
    for (const QString &line : outText.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        // 形如 " -V:3.13 *        C:\...\Python313\python.exe" 或 " -V:3.13-32        C:\..."
        const int sep = line.indexOf(QStringLiteral(".exe"), 0, Qt::CaseInsensitive);
        if (sep < 0)
            continue;
        const int start = line.lastIndexOf(QLatin1Char(' '), sep);
        const QString exe = line.mid(start + 1, sep + 4 - start - 1).trimmed();
        if (exe.isEmpty())
            continue;
        const int before = list->size();
        addCandidate(list, exe, tr_("来自 py 启动器"));
        if (list->size() > before && list->last().version.isEmpty()) {
            // 从 " -V:3.13" 提取版本
            static const QRegularExpression re(QStringLiteral("-V:(\\d+\\.\\d+)"));
            const QRegularExpressionMatch m = re.match(line);
            if (m.hasMatch())
                list->last().version = m.captured(1);
        }
    }
}

bool probePythonVersion(const QString &exePath, QString *version, QString *error, int timeoutMs)
{
    if (exePath.isEmpty())
        return false;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(exePath, {QStringLiteral("-c"), QStringLiteral("import sys;print(sys.version.split()[0])")});
    if (!p.waitForStarted(3000)) {
        if (error) *error = tr_("无法启动该解释器。");
        p.kill();
        return false;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();   // 卡住的解释器（如 Store 别名）必须杀掉，绝不等待
        p.waitForFinished(1000);
        if (error) *error = tr_("解释器在 %1 秒内没有响应（已终止）。").arg(timeoutMs / 1000);
        return false;
    }
    const QString text = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    const QString first = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).value(0);
    if (p.exitCode() != 0 || first.isEmpty()) {
        if (error) *error = tr_("解释器返回异常（exit=%1）。").arg(p.exitCode());
        return false;
    }
    if (version) *version = first;
    return true;
}

} // namespace esp
