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
    meta->themeOffset = qint64(obj.value("themeOffset").toDouble());
    meta->themeSize = qint64(obj.value("themeSize").toDouble());
    if (meta->themeSize <= 0)
        meta->themeOffset = meta->themeSize = 0;

    // 主题（配色等；背景图字节在主题块中，由 readThemeImages 单独读取）
    const QJsonObject themeObj = obj.value("theme").toObject();
    if (!themeObj.isEmpty()) {
        themeStyleFromJson(themeObj.value("installer").toObject(), &meta->theme.installer);
        themeStyleFromJson(themeObj.value("uninstaller").toObject(), &meta->theme.uninstaller);
    }

    const qint64 payloadEnd = meta->payloadOffset + meta->payloadSize;
    const qint64 dataEnd = meta->themeSize > 0 ? meta->themeOffset : tailEnd;
    if (meta->payloadOffset < 0 || meta->payloadSize <= 0 || payloadEnd > dataEnd) {
        if (error) *error = tr_("封包索引中的负载范围非法（文件可能被截断）。");
        return false;
    }
    if (meta->themeSize > 0
        && (meta->themeOffset < payloadEnd || meta->themeOffset + meta->themeSize > tailEnd)) {
        if (error) *error = tr_("封包索引中的主题块范围非法（文件可能被截断）。");
        return false;
    }
    return true;
}

bool readThemeImages(const QString &exePath, PkgMeta *meta, QString *error)
{
    if (!meta)
        return false;
    if (meta->themeSize <= 0 || meta->themeOffset <= 0)
        return true;   // 无主题块（旧封包）→ 使用默认外观

    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = tr_("无法打开文件：%1").arg(exePath);
        return false;
    }
    if (!f.seek(meta->themeOffset)) {
        if (error) *error = tr_("定位主题块失败。");
        return false;
    }
    const QByteArray head = f.read(16);
    if (head.size() != 16 || head.left(8) != QByteArray(kThemeBlobMagic)) {
        if (error) *error = tr_("主题块损坏（标记缺失）。");
        return false;
    }
    auto le32 = [](const QByteArray &b, int off) {
        return quint32(quint8(b[off])) | (quint32(quint8(b[off + 1])) << 8)
             | (quint32(quint8(b[off + 2])) << 16) | (quint32(quint8(b[off + 3])) << 24);
    };
    const quint32 instLen = le32(head, 8);
    const quint32 uninstLen = le32(head, 12);
    const qint64 need = qint64(16) + instLen + uninstLen;
    if (need > meta->themeSize) {
        if (error) *error = tr_("主题块长度非法。");
        return false;
    }
    if (instLen > 0) {
        const QByteArray d = f.read(instLen);
        if (d.size() != qint64(instLen)) {
            if (error) *error = tr_("读取安装器背景图失败。");
            return false;
        }
        meta->theme.installer.bgImage = d;
    }
    if (uninstLen > 0) {
        const QByteArray d = f.read(uninstLen);
        if (d.size() != qint64(uninstLen)) {
            if (error) *error = tr_("读取卸载器背景图失败。");
            return false;
        }
        meta->theme.uninstaller.bgImage = d;
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

    // 主题块：安装器 / 卸载器背景图（独立段，启动时只需读这一小段）
    QByteArray themeBlob;
    {
        const QByteArray instImg = meta.theme.installer.bgImage;
        const QByteArray uninstImg = meta.theme.uninstaller.bgImage;
        if (!instImg.isEmpty() || !uninstImg.isEmpty()) {
            themeBlob.append(kThemeBlobMagic, 8);
            const auto le32 = [](quint32 v) {
                QByteArray b(4, '\0');
                b[0] = char(v & 0xFF);
                b[1] = char((v >> 8) & 0xFF);
                b[2] = char((v >> 16) & 0xFF);
                b[3] = char((v >> 24) & 0xFF);
                return b;
            };
            themeBlob.append(le32(quint32(instImg.size())));
            themeBlob.append(le32(quint32(uninstImg.size())));
            themeBlob.append(instImg);
            themeBlob.append(uninstImg);
        }
    }

    const qint64 themeOffset = themeBlob.isEmpty()
                                   ? 0 : qint64(stubData.size()) + qint64(zipData.size());

    QJsonObject obj;
    obj["format"] = QStringLiteral("ESPPKG01");
    obj["payloadOffset"] = double(stubData.size());
    obj["payloadSize"] = double(zipData.size());
    obj["themeOffset"] = double(themeOffset);
    obj["themeSize"] = double(themeBlob.size());
    obj["appName"] = meta.appName;
    obj["version"] = meta.version;
    obj["publisher"] = meta.publisher;
    obj["appId"] = meta.appId;
    obj["mainExe"] = meta.mainExeName;
    obj["iconPng"] = QString::fromLatin1(meta.iconPng.toBase64());
    QJsonObject themeObj;
    themeObj["installer"] = themeStyleToJson(meta.theme.installer);
    themeObj["uninstaller"] = themeStyleToJson(meta.theme.uninstaller);
    obj["theme"] = themeObj;
    const QByteArray jsonBuf = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QFile out(outExe);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = tr_("无法创建输出文件：%1").arg(outExe);
        return false;
    }
    out.write(stubData);
    out.write(zipData);
    out.write(themeBlob);
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
