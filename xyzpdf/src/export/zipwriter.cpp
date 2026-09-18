#include "zipwriter.h"

#include <QDateTime>
#include <QFile>

namespace {

// The CRC-32 the format asks for: the usual polynomial, reflected, with the
// usual initial and final xor. Written out rather than linked: Qt does not
// export zlib's crc32() and this is fifteen lines.
quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 index = 0; index < 256; ++index) {
            quint32 value = index;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            table[index] = value;
        }
        ready = true;
    }

    quint32 crc = 0xFFFFFFFFu;
    for (const char byte : data)
        crc = table[(crc ^ quint8(byte)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void appendU16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendU32(QByteArray &out, quint32 value)
{
    appendU16(out, quint16(value & 0xFFFF));
    appendU16(out, quint16((value >> 16) & 0xFFFF));
}

// What the format calls a timestamp, which is DOS's: seconds in two-second units,
// minutes, hours, then the date. Modern ZIP files also carry a Unix time in the
// extra field; an unpacker that wants one takes it from the file system when it
// extracts, which is what every one of them does with a file that has none.
struct DosTime
{
    quint16 time = 0;
    quint16 date = 0;
};

DosTime dosTime(const QDateTime &when)
{
    const QDate date = when.date();
    const QTime time = when.time();
    DosTime out;
    out.time = quint16((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
    // 1980 is the epoch of this format: earlier than that and the month would
    // wrap into a nonsense year, so it is clamped rather than computed.
    out.date = quint16(((qMax(date.year(), 1980) - 1980) << 9) | (date.month() << 5)
                       | date.day());
    return out;
}

// Set on every entry: the file name is UTF-8 rather than Latin-1. Without it an
// unpacker is entitled to read the bytes of a Chinese name as ISO-8859-1 and
// produce a folder full of mojibake.
constexpr quint16 kFlagUtf8Names = 0x0800;
constexpr quint16 kMethodStored = 0;

} // namespace

void ZipWriter::addFile(const QString &name, const QByteArray &content)
{
    Entry entry;
    entry.name = name.toUtf8();
    entry.content = content;
    entry.crc = crc32(content);
    entries.append(entry);
}

QByteArray ZipWriter::toBytes() const
{
    const DosTime stamp = dosTime(QDateTime::currentDateTime());

    QByteArray out;
    QVector<quint32> offsets;
    offsets.reserve(entries.size());

    // ---- the data, each entry behind its own local header ----
    for (const Entry &entry : entries) {
        offsets.append(quint32(out.size()));

        out.append("PK\x03\x04", 4);       // local file header
        appendU16(out, 20);                // version needed
        appendU16(out, kFlagUtf8Names);
        appendU16(out, kMethodStored);
        appendU16(out, stamp.time);
        appendU16(out, stamp.date);
        appendU32(out, entry.crc);
        appendU32(out, quint32(entry.content.size())); // compressed == stored
        appendU32(out, quint32(entry.content.size()));
        appendU16(out, quint16(entry.name.size()));
        appendU16(out, 0);                 // no extra field
        out.append(entry.name);
        out.append(entry.content);
    }

    // ---- the central directory, which is what an unpacker reads first ----
    const quint32 directoryStart = quint32(out.size());
    for (int index = 0; index < entries.size(); ++index) {
        const Entry &entry = entries[index];

        out.append("PK\x01\x02", 4);       // central directory header
        appendU16(out, 20);                // version made by
        appendU16(out, 20);                // version needed
        appendU16(out, kFlagUtf8Names);
        appendU16(out, kMethodStored);
        appendU16(out, stamp.time);
        appendU16(out, stamp.date);
        appendU32(out, entry.crc);
        appendU32(out, quint32(entry.content.size()));
        appendU32(out, quint32(entry.content.size()));
        appendU16(out, quint16(entry.name.size()));
        appendU16(out, 0);                 // extra
        appendU16(out, 0);                 // comment
        appendU16(out, 0);                 // disk number
        appendU16(out, 0);                 // internal attributes
        appendU32(out, 0);                 // external attributes
        appendU32(out, offsets.at(index));
        out.append(entry.name);
    }
    const quint32 directorySize = quint32(out.size()) - directoryStart;

    // ---- and the end record, which says where the directory is ----
    out.append("PK\x05\x06", 4);
    appendU16(out, 0);                     // this disk
    appendU16(out, 0);                     // disk with the directory
    appendU16(out, quint16(entries.size()));
    appendU16(out, quint16(entries.size()));
    appendU32(out, directorySize);
    appendU32(out, directoryStart);
    appendU16(out, 0);                     // no comment
    return out;
}

bool ZipWriter::write(const QString &path, QString *error) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = toBytes();
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    // Closed before the answer is given rather than left to the destructor: the
    // file is what the caller is about to tell the user about, and a full disk
    // reports itself here, at the close, and nowhere else.
    if (!file.flush() || file.error() != QFile::NoError) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.close();
    return true;
}
