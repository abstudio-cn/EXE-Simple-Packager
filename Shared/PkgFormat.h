#pragma once
// PkgFormat — ESPPKG01 封包格式：安装器 exe + 负载 ZIP + 主题块 + 尾部索引
//
// 文件布局：
//   [0 .. payloadOffset)            安装器 exe 原样
//   [payloadOffset .. +payloadSize) ZIP 压缩包（app 文件 + __esp_uninstall/ 卸载器运行时）
//   [themeOffset .. +themeSize)     主题块（安装器/卸载器背景图，可为空）
//   [尾部]                          JSON 索引（长度前缀 4 字节 LE）+ magic "ESPPKG01"
//
// 主题块布局（独立于 ZIP，便于启动时只读这一小段，不加载整个负载）：
//   [8] "ESPTHEME" [4] 安装器图片字节数(LE) [4] 卸载器图片字节数(LE) [图片字节…]
//
// 尾部索引 JSON:
//   { "format": "ESPPKG01", "payloadOffset": <qint64>, "payloadSize": <qint64>,
//     "themeOffset": <qint64>, "themeSize": <qint64>,
//     "theme": { "installer": {...}, "uninstaller": {...} },
//     "appName": "...", "version": "...", "publisher": "...", "appId": "..." }

#include "ThemeSpec.h"

#include <QString>
#include <QByteArray>
#include <QJsonObject>

namespace esp {

constexpr const char* kPkgMagic = "ESPPKG01";
constexpr const char* kThemeBlobMagic = "ESPTHEME";

struct PkgMeta
{
    QString appName;
    QString version;
    QString publisher;
    QString appId;          // 卸载注册表子键名（如 "MyApp" 或 GUID）
    QString mainExeName;    // 主程序 exe 文件名（相对安装目录）
    QByteArray iconPng;     // 主程序图标（PNG 字节，打包时提取，可为空）
    ThemePack theme;        // 安装器 / 卸载器自定义外观（背景图字节随主题块写入）
    qint64 payloadOffset = 0;
    qint64 payloadSize = 0;
    qint64 themeOffset = 0; // 0 表示无主题块
    qint64 themeSize = 0;
};

// 从安装器 exe 自身读取尾部索引。失败返回 false 并置 error。
bool readPkgMeta(const QString &exePath, PkgMeta *meta, QString *error);

// 单独读取主题块中的背景图字节（只读这一小段，不加载 ZIP 负载）。
bool readThemeImages(const QString &exePath, PkgMeta *meta, QString *error);

// 打包：把 stub exe + zip 数据 + 元数据写出成品安装器。返回 false 并置 error。
bool writePackage(const QString &stubExe, const QByteArray &zipData,
                  const PkgMeta &meta, const QString &outExe, QString *error);

// 从安装器读取负载 ZIP（整段读入内存）。
bool readPayload(const QString &exePath, QByteArray *zipData, QString *error);

} // namespace esp
