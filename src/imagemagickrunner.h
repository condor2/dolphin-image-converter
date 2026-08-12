#pragma once

#include <functional>
#include <QString>
#include <QStringList>

class QWidget;

class ImageMagickRunner
{
public:
    struct Result {
        int succeeded = 0;
        int failed = 0;
        QStringList errors;
    };

    static QString executable();
    static bool isAvailable();

    static Result runBatch(
        QWidget *parent,
        const QStringList &files,
        const QString &operationLabel,
        const std::function<QStringList(const QString &input)> &argumentBuilder
    );
};
