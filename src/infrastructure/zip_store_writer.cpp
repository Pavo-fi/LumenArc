#include "zip_store_writer.h"

#include <QFile>
#include <QFileInfo>
#include <QDateTime>

namespace {
quint32 crcTable[256];
bool crcTableInit = false;

void initCrcTable()
{
    if (crcTableInit)
        return;
    for (quint32 i = 0; i < 256; ++i) {
        quint32 c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTable[i] = c;
    }
    crcTableInit = true;
}

void putU16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}

void putU32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}
} // namespace

quint32 ZipStoreWriter::crc32(const QByteArray &data)
{
    initCrcTable();
    quint32 c = 0xFFFFFFFFu;
    for (const char b : data)
        c = crcTable[(c ^ quint8(b)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void ZipStoreWriter::addFile(const QString &name, const QByteArray &data)
{
    for (auto &e : m_entries)
        if (e.name == name) {
            e.data = data;
            return;
        }
    m_entries.append({name, data});
}

bool ZipStoreWriter::writeTo(const QString &path) const
{
    QByteArray out;
    QVector<quint32> offsets;
    // DOS 纪元固定时间戳（产物确定性：同内容同字节——取证可复算）
    const quint16 dosTime = 0, dosDate = 0x0021;   // 1980-01-01 00:00
    for (const auto &e : m_entries) {
        const QByteArray nameUtf8 = e.name.toUtf8();
        const quint32 crc = crc32(e.data);
        offsets.append(quint32(out.size()));
        putU32(out, 0x04034b50);        // local file header
        putU16(out, 20);                // version needed
        putU16(out, 0x0800);            // flags: bit11 = UTF-8 文件名
        putU16(out, 0);                 // method: store
        putU16(out, dosTime);
        putU16(out, dosDate);
        putU32(out, crc);
        putU32(out, quint32(e.data.size()));
        putU32(out, quint32(e.data.size()));
        putU16(out, quint16(nameUtf8.size()));
        putU16(out, 0);                 // extra len
        out.append(nameUtf8);
        out.append(e.data);
    }
    const quint32 cdStart = quint32(out.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &e = m_entries[i];
        const QByteArray nameUtf8 = e.name.toUtf8();
        putU32(out, 0x02014b50);        // central directory
        putU16(out, 20);
        putU16(out, 20);
        putU16(out, 0x0800);
        putU16(out, 0);
        putU16(out, dosTime);
        putU16(out, dosDate);
        putU32(out, crc32(e.data));
        putU32(out, quint32(e.data.size()));
        putU32(out, quint32(e.data.size()));
        putU16(out, quint16(nameUtf8.size()));
        putU16(out, 0);                 // extra
        putU16(out, 0);                 // comment
        putU16(out, 0);                 // disk
        putU16(out, 0);                 // internal attr
        putU32(out, 0);                 // external attr
        putU32(out, offsets[i]);
        out.append(nameUtf8);
    }
    const quint32 cdSize = quint32(out.size()) - cdStart;
    putU32(out, 0x06054b50);            // end of central directory
    putU16(out, 0);
    putU16(out, 0);
    putU16(out, quint16(m_entries.size()));
    putU16(out, quint16(m_entries.size()));
    putU32(out, cdSize);
    putU32(out, cdStart);
    putU16(out, 0);

    // 原子写（取证口径：失败不留 0 字节残件——P-XX Release 0MB docx 排查硬化）：
    // 同目录 .tmp 写入 → flush+关闭 → 大小复核 → rename 覆盖
    const QString tmp = path + QStringLiteral(".tmp");
    QFile::remove(tmp);
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const qint64 w = f.write(out);
    f.flush();
    f.close();
    if (w != out.size() || QFileInfo(tmp).size() != out.size()) {
        QFile::remove(tmp);
        return false;
    }
    QFile::remove(path);                // Windows rename 不覆盖既有文件
    return QFile::rename(tmp, path);
}
