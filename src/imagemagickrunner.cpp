#include "imagemagickrunner.h"

#include <QApplication>
#include <QFileInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>

QString ImageMagickRunner::executable()
{
    return QStandardPaths::findExecutable(QStringLiteral("magick"));
}

bool ImageMagickRunner::isAvailable()
{
    return !executable().isEmpty();
}

ImageMagickRunner::Result ImageMagickRunner::runBatch(
    QWidget *parent,
    const QStringList &files,
    const QString &operationLabel,
    const std::function<QStringList(const QString &input)> &argumentBuilder)
{
    Result result;
    const QString program = executable();

    QProgressDialog progress(operationLabel, QObject::tr("Cancel"), 0, files.size(), parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(true);
    progress.setAutoReset(true);

    for (int i = 0; i < files.size(); ++i) {
        if (progress.wasCanceled())
            break;

        const QString input = files.at(i);
        progress.setLabelText(QObject::tr("Processing %1 of %2\n%3")
                                  .arg(i + 1)
                                  .arg(files.size())
                                  .arg(QFileInfo(input).fileName()));
        progress.setValue(i);
        QApplication::processEvents();

        const QStringList arguments = argumentBuilder(input);
        QProcess process;
        process.setProgram(program);
        process.setArguments(arguments);
        process.start();

        if (!process.waitForStarted()) {
            ++result.failed;
            result.errors << QObject::tr("%1: could not start ImageMagick.")
                                 .arg(QFileInfo(input).fileName());
            continue;
        }

        while (!process.waitForFinished(100)) {
            QApplication::processEvents();
            if (progress.wasCanceled()) {
                process.kill();
                process.waitForFinished();
                break;
            }
        }

        if (progress.wasCanceled())
            break;

        if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
            ++result.succeeded;
        } else {
            ++result.failed;
            const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
            result.errors << QObject::tr("%1: %2")
                                 .arg(QFileInfo(input).fileName(),
                                      stderrText.isEmpty() ? QObject::tr("ImageMagick failed.") : stderrText);
        }
    }

    progress.setValue(files.size());
    return result;
}
