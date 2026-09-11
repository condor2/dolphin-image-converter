// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

struct JpegOrientationInfo
{
    // EXIF: 1..8 = valid Orientation, 0 = missing, -1 = malformed/unexpected.
    int orientation = 0;
    // XMP tiff:Orientation: 1..8 = valid, 0 = missing, -1 = present but unparseable.
    int xmpOrientation = 0;
};

JpegOrientationInfo inspectJpegOrientation(const QByteArray &data);
JpegOrientationInfo inspectJpegOrientationFile(const QString &path);
int rewriteJpegOrientation(QByteArray &data, bool rotateClockwise);
