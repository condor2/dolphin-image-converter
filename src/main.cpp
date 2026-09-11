// SPDX-License-Identifier: GPL-3.0-or-later
#include "imageconverterdialog.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMessageBox>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QUrl>

static QStringList localFilesFromArguments(const QStringList &arguments)
{
    QStringList files;
    for (const QString &argument : arguments) {
        const QUrl url = QUrl::fromUserInput(argument);
        QString path;
        if (url.isLocalFile())
            path = url.toLocalFile();
        else if (QFileInfo(argument).exists())
            path = QFileInfo(argument).absoluteFilePath();

        if (!path.isEmpty() && QFileInfo(path).isFile())
            files << path;
    }
    files.removeDuplicates();
    return files;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DolphinImageConverter"));
    QApplication::setApplicationName(QStringLiteral("dolphin-image-converter"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    QTranslator translator;
    QTranslator qtTranslator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    const bool englishFirst = !uiLanguages.isEmpty()
                           && uiLanguages.first().startsWith(QStringLiteral("en"), Qt::CaseInsensitive);

    // QLocale::system().uiLanguages() already honors LANGUAGE on Unix. Do not
    // fall through from an explicitly preferred English locale to Romanian just
    // because this project does not ship a separate English catalog.
    if (!englishFirst) {
        if (translator.load(QLocale::system(),
                            QStringLiteral("dolphin-image-converter"),
                            QStringLiteral("_"),
                            QStringLiteral(":/i18n"))) {
            app.installTranslator(&translator);
        }

        // Standard Qt button text (OK, Cancel, Show Details...) is translated
        // when the platform provides the matching qtbase catalog.
        if (qtTranslator.load(QLocale::system(),
                              QStringLiteral("qtbase"),
                              QStringLiteral("_"),
                              QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            app.installTranslator(&qtTranslator);
        }
    }

    QApplication::setApplicationDisplayName(QObject::tr("Dolphin Image Converter"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Batch image tools for KDE Dolphin"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption resizeOption(QStringLiteral("resize"), QObject::tr("Open the resize dialog."));
    QCommandLineOption rotateLeftOption(QStringLiteral("rotate-left"), QObject::tr("Rotate images 90 degrees counter-clockwise."));
    QCommandLineOption rotateRightOption(QStringLiteral("rotate-right"), QObject::tr("Rotate images 90 degrees clockwise."));
    QCommandLineOption convertOption(QStringLiteral("convert"), QObject::tr("Open the format conversion dialog."));

    parser.addOption(resizeOption);
    parser.addOption(rotateLeftOption);
    parser.addOption(rotateRightOption);
    parser.addOption(convertOption);
    parser.addPositionalArgument(QStringLiteral("files"), QObject::tr("Image files or file:// URLs."), QStringLiteral("[files...]"));
    parser.process(app);

    const int modeCount = int(parser.isSet(resizeOption))
                        + int(parser.isSet(rotateLeftOption))
                        + int(parser.isSet(rotateRightOption))
                        + int(parser.isSet(convertOption));

    if (modeCount != 1) {
        QMessageBox::critical(nullptr, QObject::tr("Invalid operation"),
                              QObject::tr("Choose exactly one operation: --resize, --rotate-left, --rotate-right or --convert."));
        return 2;
    }

    const QStringList files = localFilesFromArguments(parser.positionalArguments());
    if (files.isEmpty()) {
        QMessageBox::critical(nullptr, QObject::tr("No images selected"),
                              QObject::tr("No valid local image files were provided."));
        return 2;
    }

    ImageConverterDialog::Mode mode = ImageConverterDialog::Mode::Resize;
    if (parser.isSet(rotateLeftOption))
        mode = ImageConverterDialog::Mode::RotateLeft;
    else if (parser.isSet(rotateRightOption))
        mode = ImageConverterDialog::Mode::RotateRight;
    else if (parser.isSet(convertOption))
        mode = ImageConverterDialog::Mode::Convert;

    ImageConverterDialog dialog(mode, files);
    return dialog.exec() == QDialog::Accepted ? 0 : 1;
}
