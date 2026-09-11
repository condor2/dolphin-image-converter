// SPDX-License-Identifier: GPL-3.0-or-later
#include "jpegorientation.h"

#include <QFile>
#include <QRegularExpression>

namespace {

struct ParsedJpegOrientation
{
    int orientation = 0;
    int xmpOrientation = 0;
    qsizetype xmpValueOffset = -1;
    int xmpMatchCount = 0;
    qsizetype orientationValueOffset = -1;
    bool littleEndian = true;
};

enum class HeaderCoverage {
    Complete,
    NeedMore,
    Malformed
};

static int xmpOrientationValue(const QByteArray &payload, qsizetype *digitOffset, int *matchCount)
{
    if (digitOffset)
        *digitOffset = -1;
    if (matchCount)
        *matchCount = 0;

    if (!payload.contains("tiff:Orientation"))
        return 0;

    // Covers both common forms:
    //   tiff:Orientation="6"
    //   <tiff:Orientation>6</tiff:Orientation>
    // fromLatin1() preserves a 1:1 byte-to-character mapping, therefore the
    // captured offset can be used directly as a byte offset in the APP1 payload.
    static const QRegularExpression re(
        QStringLiteral(R"(tiff:Orientation\s*(?:=\s*["']|>)\s*([1-8])\b)"));
    const QString text = QString::fromLatin1(payload);

    // The element form <tiff:Orientation>6</tiff:Orientation> contains the
    // token twice; the closing tag is not a separate metadata value.
    const int tokenCount = int(payload.count("tiff:Orientation")
                             - payload.count("</tiff:Orientation"));
    int value = -1;
    qsizetype lastOffset = -1;
    int count = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        ++count;
        value = match.captured(1).toInt();
        lastOffset = match.capturedStart(1);
    }

    if (digitOffset)
        *digitOffset = lastOffset;
    if (matchCount)
        *matchCount = count;

    // If an Orientation token exists in a form we did not parse, treat the
    // XMP as unsafe instead of rewriting only the subset we understood.
    if (count == 0 || count != tokenCount)
        return -1;
    return value;
}

static HeaderCoverage jpegHeaderCoverage(const QByteArray &data)
{
    auto byteAt = [&data](qsizetype index) -> quint8 {
        return quint8(data.at(index));
    };

    if (data.size() < 2)
        return HeaderCoverage::NeedMore;
    if (byteAt(0) != 0xFF || byteAt(1) != 0xD8)
        return HeaderCoverage::Malformed;

    qsizetype position = 2;
    while (true) {
        if (position >= data.size())
            return HeaderCoverage::NeedMore;
        if (byteAt(position) != 0xFF)
            return HeaderCoverage::Malformed;

        while (position < data.size() && byteAt(position) == 0xFF)
            ++position;
        if (position >= data.size())
            return HeaderCoverage::NeedMore;

        const quint8 marker = byteAt(position++);
        if (marker == 0xDA || marker == 0xD9) // SOS / EOI
            return HeaderCoverage::Complete;

        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
            continue;

        if (position + 2 > data.size())
            return HeaderCoverage::NeedMore;

        const qsizetype length = (qsizetype(byteAt(position)) << 8)
                               | qsizetype(byteAt(position + 1));
        if (length < 2)
            return HeaderCoverage::Malformed;

        const qsizetype end = position + length;
        if (end > data.size())
            return HeaderCoverage::NeedMore;
        position = end;
    }
}

static ParsedJpegOrientation parseJpegOrientation(const QByteArray &data)
{
    ParsedJpegOrientation result;

    auto byteAt = [&data](qsizetype index) -> quint8 {
        return quint8(data.at(index));
    };

    if (data.size() < 4 || byteAt(0) != 0xFF || byteAt(1) != 0xD8) {
        result.orientation = -1;
        return result;
    }

    qsizetype position = 2;
    while (position + 1 < data.size()) {
        if (byteAt(position) != 0xFF) {
            result.orientation = -1;
            return result;
        }

        // JPEG permits fill bytes between markers.
        while (position < data.size() && byteAt(position) == 0xFF)
            ++position;
        if (position >= data.size()) {
            result.orientation = -1;
            return result;
        }

        const quint8 marker = byteAt(position++);
        if (marker == 0xDA || marker == 0xD9) // SOS / EOI
            break;

        // Standalone markers have no length field.
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
            continue;

        if (position + 2 > data.size()) {
            result.orientation = -1;
            return result;
        }

        const qsizetype length = (qsizetype(byteAt(position)) << 8)
                               | qsizetype(byteAt(position + 1));
        if (length < 2) {
            result.orientation = -1;
            return result;
        }

        const qsizetype segment = position + 2;
        const qsizetype end = position + length;
        if (end > data.size()) {
            result.orientation = -1;
            return result;
        }

        if (marker == 0xE1) {
            static const QByteArray xmpHeader("http://ns.adobe.com/xap/1.0/\0", 29);
            if (length >= 2 + xmpHeader.size()
                && data.mid(segment, xmpHeader.size()) == xmpHeader) {
                const QByteArray payload = data.mid(segment + xmpHeader.size(),
                                                    end - segment - xmpHeader.size());
                qsizetype digitOffset = -1;
                int matchCount = 0;
                const int value = xmpOrientationValue(payload, &digitOffset, &matchCount);

                if (matchCount > 0) {
                    result.xmpMatchCount += matchCount;
                    // A metadata-only rewrite is only safe when there is exactly
                    // one matching XMP Orientation value in the whole JPEG.
                    if (result.xmpMatchCount == 1 && digitOffset >= 0)
                        result.xmpValueOffset = segment + xmpHeader.size() + digitOffset;
                    else
                        result.xmpValueOffset = -1;
                }

                if (value != 0) {
                    if (result.xmpOrientation == 0)
                        result.xmpOrientation = value;
                    else if (result.xmpOrientation != value)
                        result.xmpOrientation = -1;
                }
            }

            if (length >= 16
                && data.mid(segment, 6) == QByteArray("Exif\0\0", 6)) {
                const qsizetype tiff = segment + 6;
                if (tiff + 8 > end) {
                    result.orientation = -1;
                    return result;
                }

                const QByteArray byteOrder = data.mid(tiff, 2);
                const bool littleEndian = byteOrder == QByteArrayLiteral("II");
                if (!littleEndian && byteOrder != QByteArrayLiteral("MM")) {
                    result.orientation = -1;
                    return result;
                }

                auto read16 = [&](qsizetype index) -> quint16 {
                    if (index + 2 > end)
                        return 0;
                    if (littleEndian)
                        return quint16(byteAt(index) | (quint16(byteAt(index + 1)) << 8));
                    return quint16((quint16(byteAt(index)) << 8) | byteAt(index + 1));
                };
                auto read32 = [&](qsizetype index) -> quint32 {
                    if (index + 4 > end)
                        return 0;
                    if (littleEndian)
                        return quint32(read16(index)) | (quint32(read16(index + 2)) << 16);
                    return (quint32(read16(index)) << 16) | quint32(read16(index + 2));
                };

                if (read16(tiff + 2) != 42) {
                    result.orientation = -1;
                    return result;
                }

                const quint32 ifdOffset = read32(tiff + 4);
                if (ifdOffset > quint32(end - tiff)) {
                    result.orientation = -1;
                    return result;
                }

                const qsizetype ifd = tiff + qsizetype(ifdOffset);
                if (ifd + 2 > end) {
                    result.orientation = -1;
                    return result;
                }

                const int entryCount = read16(ifd);
                for (int index = 0; index < entryCount; ++index) {
                    const qsizetype entry = ifd + 2 + qsizetype(12) * index;
                    if (entry + 12 > end) {
                        result.orientation = -1;
                        return result;
                    }

                    if (read16(entry) != 0x0112)
                        continue;

                    // EXIF Orientation is defined as SHORT with count 1. If the
                    // tag exists in any other form, do not guess how another
                    // metadata reader will interpret it.
                    if (read16(entry + 2) != 3 || read32(entry + 4) != 1) {
                        result.orientation = -1;
                        return result;
                    }

                    const int value = read16(entry + 8);
                    if (value < 1 || value > 8) {
                        result.orientation = -1;
                        return result;
                    }

                    if (result.orientation != 0 && result.orientation != value) {
                        result.orientation = -1;
                        return result;
                    }

                    result.orientation = value;
                    result.orientationValueOffset = entry + 8;
                    result.littleEndian = littleEndian;
                    // Keep scanning APP1 segments so XMP can still be detected.
                    break;
                }
            }
        }

        position = end;
    }

    return result;
}

} // namespace

JpegOrientationInfo inspectJpegOrientation(const QByteArray &data)
{
    const ParsedJpegOrientation parsed = parseJpegOrientation(data);
    return {parsed.orientation, parsed.xmpOrientation};
}

JpegOrientationInfo inspectJpegOrientationFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {-1, 0};

    constexpr qint64 headerProbeSize = 256 * 1024;
    QByteArray data = file.read(headerProbeSize);
    const HeaderCoverage coverage = jpegHeaderCoverage(data);

    if (coverage == HeaderCoverage::NeedMore) {
        data += file.readAll();
    }

    return inspectJpegOrientation(data);
}

int rewriteJpegOrientation(QByteArray &data, bool rotateClockwise)
{
    static constexpr int rotateCw[9]  = {0, 6, 7, 8, 5, 2, 3, 4, 1};
    static constexpr int rotateCcw[9] = {0, 8, 5, 6, 7, 4, 1, 2, 3};

    const ParsedJpegOrientation parsed = parseJpegOrientation(data);
    const bool xmpRewritable = parsed.xmpOrientation == parsed.orientation
                            && parsed.xmpMatchCount == 1
                            && parsed.xmpValueOffset >= 0;
    if ((parsed.xmpOrientation != 0 && !xmpRewritable)
        || parsed.orientation < 1 || parsed.orientation > 8
        || parsed.orientationValueOffset < 0) {
        return 0;
    }

    const int *map = rotateClockwise ? rotateCw : rotateCcw;
    const int newOrientation = map[parsed.orientation];
    data[parsed.orientationValueOffset] = char(parsed.littleEndian ? newOrientation : 0);
    data[parsed.orientationValueOffset + 1] = char(parsed.littleEndian ? 0 : newOrientation);
    if (parsed.xmpOrientation != 0)
        data[parsed.xmpValueOffset] = char('0' + newOrientation);
    return parsed.orientation;
}
