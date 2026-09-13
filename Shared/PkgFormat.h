#pragma once
// PkgFormat — ESPPKG01 封包格式：安装器 exe + 负载 ZIP + 尾部索引
//
// 文件布局：
//   [0 .. payloadOffset)            安装器 exe 原样
//   [payloadOffset .. +payloadSize) ZIP 压缩包（app 文件 + __esp_uninstall/ 卸载器运行时）
//   [尾部]                          JSON 索引（长度前缀 4 字节 LE）+ magic "ESPPKG01"
//
// 尾部索引 JSON:
//   { "format": "ESPPKG01", "payloadOffset": <qint64>, "payloadSize": <qint64>,
//     "appName": "...", "version": "...", "publisher": "...", "appId": "..." }

#include <QString>
#include <QByteArray>
#include <QJsonObject>

namespace esp {

constexpr const char* kPkgMagic = "ESPPKG01";

struct PkgMeta
{
    QString appName;
    QString version;
    QString publisher;
    QString appId;          // 卸载注册表子键名（如 "MyApp" 或 GUID）
    QString mainExeName;    // 主程序 exe 文件名（相对安装目录）
    QByteArray iconPng;     // 主程序图标（PNG 字节，打包时提取，可为空）
    qint64 payloadOffset = 0;
    qint64 payloadSize = 0;
};

// 从安装器 exe 自身读取尾部索引。失败返回 false 并置 error。
bool readPkgMeta(const QString &exePath, PkgMeta *meta, QString *error);

// 打包：把 stub exe + zip 数据 + 元数据写出成品安装器。返回 false 并置 error。
bool writePackage(const QString &stubExe, const QByteArray &zipData,
                  const PkgMeta &meta, const QString &outExe, QString *error);

// 从安装器读取负载 ZIP（整段读入内存）。
bool readPayload(const QString &exePath, QByteArray *zipData, QString *error);

} // namespace esp
