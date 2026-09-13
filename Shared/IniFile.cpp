#include "IniFile.h"

#include <QFile>
#include <QTextStream>

namespace esp {

IniFile::IniFile(const QString &path) : path_(path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    QString section;
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(';') || line.startsWith('#'))
            continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        data_[{section, key}] = value;
    }
    valid_ = true;
}

QString IniFile::get(const QString &section, const QString &key, const QString &fallback) const
{
    return data_.value({section, key}, fallback);
}

void IniFile::set(const QString &section, const QString &key, const QString &value)
{
    data_[{section, key}] = value;
    valid_ = true;
}

bool IniFile::save(QString *error) const
{
    QFile f(path_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法写入 %1").arg(path_);
        return false;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "; uninstall.ini - ESP 三件套安装器自动生成，请勿手工修改\n";
    QString lastSection;
    for (auto it = data_.cbegin(); it != data_.cend(); ++it) {
        if (it.key().first != lastSection) {
            lastSection = it.key().first;
            ts << '[' << lastSection << "]\n";
        }
        ts << it.key().second << '=' << it.value() << '\n';
    }
    ts.flush();
    f.close();
    return true;
}

} // namespace esp
