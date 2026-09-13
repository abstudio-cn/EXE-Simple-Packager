#include "PkgFormat.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QCoreApplication>

namespace esp {

namespace {
QString tr_(const char *s)
{
    return QCoreApplication::translate("PkgFormat", s);
}
}

// 读尾部：最后 8 字节必须是 magic；前面 4 字节 LE 为 JSON 长度。
static bool readTail(const QString &exePath, QJsonObject *obj, qint64 *tailEnd, QString *error)
{
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = tr_("无法打开文件：%1").arg(exePath);
        return false;
    }
    const qint64 size = f.size();
    if (size < 12) {
        if (error) *error = tr_("文件过小，不是有效的封包。");
        return false;
    }
    QByteArray magic(8, '\0');
    if (!f.seek(size - 8) || f.read(magic.data(), 8) != 8 || magic != QByteArray(kPkgMagic)) {
        if (error) *error = tr_("未找到 ESPPKG01 封包标记（该文件不是由打包器生成的安装包）。");
        return false;
    }
    QByteArray lenBuf(4, '\0');
    if (!f.seek(size - 12) || f.read(lenBuf.data(), 4) != 4) {
        if (error) *error = tr_("读取索引长度失败。");
        return false;
    }
    const quint32 jsonLen = quint32(quint8(lenBuf[0])) | (quint32(quint8(lenBuf[1])) << 8)
                          | (quint32(quint8(lenBuf[2])) << 16) | (quint32(quint8(lenBuf[3])) << 24);
    const qint64 jsonStart = size - 12 - qint64(jsonLen);
    if (jsonStart < 0) {
        if (error) *error = tr_("索引长度非法。");
        return false;
    }
    QByteArray jsonBuf = QByteArray(qint32(jsonLen), '\0');
    if (!f.seek(jsonStart) || f.read(jsonBuf.data(), jsonLen) != qint64(jsonLen)) {
        if (error) *error = tr_("读取索引失败。");
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBuf, &pe);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = tr_("索引解析失败：%1").arg(pe.errorString());
        return false;
    }
    *obj = doc.object();
    if (tailEnd) *tailEnd = jsonStart;
    return true;
}

bool readPkgMeta(const QString &exePath, PkgMeta *meta, QString *error)
{
    QJsonObject obj;
    qint64 tailEnd = 0;
    if (!readTail(exePath, &obj, &tailEnd, error))
        return false;
    meta->appName = obj.value("appName").toString();
    meta->version = obj.value("version").toString();
    meta->publisher = obj.value("publisher").toString();
    meta->appId = obj.value("appId").toString();
    meta->mainExeName = obj.value("mainExe").toString();
    meta->iconPng = QByteArray::fromBase64(obj.value("iconPng").toString().toLatin1());
    meta->payloadOffset = qint64(obj.value("payloadOffset").toDouble());
    meta->payloadSize = qint64(obj.value("payloadSize").toDouble());
    if (meta->payloadOffset < 0 || meta->payloadSize <= 0 || meta->payloadOffset + meta->payloadSize > tailEnd) {
        if (error) *error = tr_("封包索引中的负载范围非法（文件可能被截断）。");
        return false;
    }
    return true;
}

bool readPayload(const QString &exePath, QByteArray *zipData, QString *error)
{
    PkgMeta meta;
    if (!readPkgMeta(exePath, &meta, error))
        return false;
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = tr_("无法打开文件：%1").arg(exePath);
        return false;
    }
    if (!f.seek(meta.payloadOffset)) {
        if (error) *error = tr_("定位负载失败。");
        return false;
    }
    *zipData = f.read(meta.payloadSize);
    if (zipData->size() != meta.payloadSize) {
        if (error) *error = tr_("读取负载数据不完整。");
        return false;
    }
    return true;
}

bool writePackage(const QString &stubExe, const QByteArray &zipData,
                  const PkgMeta &meta, const QString &outExe, QString *error)
{
    QFile stub(stubExe);
    if (!stub.open(QIODevice::ReadOnly)) {
        if (error) *error = tr_("无法打开安装器存根：%1").arg(stubExe);
        return false;
    }
    const QByteArray stubData = stub.readAll();
    stub.close();

    QJsonObject obj;
    obj["format"] = QStringLiteral("ESPPKG01");
    obj["payloadOffset"] = double(stubData.size());
    obj["payloadSize"] = double(zipData.size());
    obj["appName"] = meta.appName;
    obj["version"] = meta.version;
    obj["publisher"] = meta.publisher;
    obj["appId"] = meta.appId;
    obj["mainExe"] = meta.mainExeName;
    obj["iconPng"] = QString::fromLatin1(meta.iconPng.toBase64());
    const QByteArray jsonBuf = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QFile out(outExe);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = tr_("无法创建输出文件：%1").arg(outExe);
        return false;
    }
    out.write(stubData);
    out.write(zipData);
    out.write(jsonBuf);
    const quint32 jsonLen = quint32(jsonBuf.size());
    const char lenBytes[4] = {
        char(jsonLen & 0xFF), char((jsonLen >> 8) & 0xFF),
        char((jsonLen >> 16) & 0xFF), char((jsonLen >> 24) & 0xFF)
    };
    out.write(lenBytes, 4);
    out.write(kPkgMagic, 8);
    out.close();
    return true;
}

} // namespace esp
