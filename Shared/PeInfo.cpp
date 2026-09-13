#include "PeInfo.h"

#include <QFile>

namespace esp {

QString peArchitecture(const QString &exePath)
{
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("unknown");

    // DOS 头
    QByteArray dos(64, '\0');
    if (f.read(dos.data(), 64) != 64 || dos[0] != 'M' || dos[1] != 'Z')
        return QStringLiteral("unknown");
    const quint32 e_lfanew = quint32(quint8(dos[0x3C])) | (quint32(quint8(dos[0x3D])) << 8)
                           | (quint32(quint8(dos[0x3E])) << 16) | (quint32(quint8(dos[0x3F])) << 24);

    // PE 签名 + COFF 头
    if (!f.seek(qint64(e_lfanew)))
        return QStringLiteral("unknown");
    QByteArray coff(24, '\0');
    if (f.read(coff.data(), 24) != 24)
        return QStringLiteral("unknown");
    if (coff[0] != 'P' || coff[1] != 'E' || coff[2] != '\0' || coff[3] != '\0')
        return QStringLiteral("unknown");
    const quint16 machine = quint16(quint8(coff[4])) | (quint16(quint8(coff[5])) << 8);
    switch (machine) {
    case 0x8664: return QStringLiteral("x64");
    case 0x014C: return QStringLiteral("x86");
    case 0xAA64: return QStringLiteral("arm64");
    default:     return QStringLiteral("unknown");
    }
}

} // namespace esp
