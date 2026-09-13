#pragma once
// IniFile — 极简 INI 读取（卸载配置 uninstall.ini）

#include <QString>
#include <QHash>
#include <QMap>

namespace esp {

class IniFile
{
public:
    explicit IniFile(const QString &path);

    bool isValid() const { return valid_; }
    QString path() const { return path_; }

    // 按 section/key 读取；缺省返回 fallback。
    QString get(const QString &section, const QString &key, const QString &fallback = QString()) const;

    // 保存（用于安装器生成 uninstall.ini）
    void set(const QString &section, const QString &key, const QString &value);
    bool save(QString *error = nullptr) const;

private:
    bool valid_ = false;
    QString path_;
    QMap<QPair<QString, QString>, QString> data_;
};

} // namespace esp
