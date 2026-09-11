// SPDX-License-Identifier: GPL-3.0-or-later
#include "imageconverterdialog.h"
#include "imagemagickrunner.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QThread>
#include <QUuid>
#include <QVector>
#include <utility>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
#include <QVBoxLayout>

ImageConverterDialog::ImageConverterDialog(Mode mode, QStringList files, QWidget *parent)
    : QDialog(parent), m_mode(mode), m_files(std::move(files))
{
    buildUi();
}

void ImageConverterDialog::buildUi()
{
    setMinimumWidth(440);

    switch (m_mode) {
    case Mode::Resize:
        setWindowTitle(tr("Resize Images"));
        break;
    case Mode::RotateLeft:
        setWindowTitle(tr("Rotate Images Left"));
        break;
    case Mode::RotateRight:
        setWindowTitle(tr("Rotate Images Right"));
        break;
    case Mode::Convert:
        setWindowTitle(tr("Convert Images"));
        break;
    }

    auto *layout = new QVBoxLayout(this);

    auto *summary = new QLabel(tr("%n image(s) selected", nullptr, m_files.size()), this);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary);

    switch (m_mode) {
    case Mode::Resize:
        buildResizeOptions();
        break;
    case Mode::RotateLeft:
    case Mode::RotateRight:
        buildRotateOptions();
        break;
    case Mode::Convert:
        buildConvertOptions();
        break;
    }

    auto *outputBox = new QGroupBox(tr("Output"), this);
    auto *outputLayout = new QFormLayout(outputBox);

    QCheckBox *noSuffixCheck = nullptr;
    if (m_mode == Mode::Convert) {
        m_useOriginalBaseName = new QCheckBox(tr("Use original base name (no suffix)"), outputBox);
        m_useOriginalBaseName->setChecked(false);
        noSuffixCheck = m_useOriginalBaseName;
    } else {
        m_overwriteOriginal = new QCheckBox(tr("Overwrite original files"), outputBox);
        m_overwriteOriginal->setChecked(false);
        noSuffixCheck = m_overwriteOriginal;
    }
    outputLayout->addRow(noSuffixCheck);

    m_suffix = new QLineEdit(outputBox);
    switch (m_mode) {
    case Mode::Resize:
        m_suffix->setText(QStringLiteral("_resized"));
        break;
    case Mode::RotateLeft:
    case Mode::RotateRight:
        m_suffix->setText(QStringLiteral("_rotated"));
        break;
    case Mode::Convert:
        m_suffix->setText(QStringLiteral("_converted"));
        break;
    }
    outputLayout->addRow(tr("Filename suffix:"), m_suffix);

    connect(noSuffixCheck, &QCheckBox::toggled, m_suffix, [this](bool checked) {
        m_suffix->setEnabled(!checked);
    });

    if (m_mode == Mode::Convert) {
        m_sameOutputFolder = new QCheckBox(tr("Same folder as source"), outputBox);
        m_sameOutputFolder->setChecked(true);
        outputLayout->addRow(m_sameOutputFolder);

        auto *folderWidget = new QWidget(outputBox);
        auto *folderLayout = new QHBoxLayout(folderWidget);
        folderLayout->setContentsMargins(0, 0, 0, 0);

        m_outputDirectory = new QLineEdit(folderWidget);
        m_outputDirectory->setReadOnly(true);
        m_outputDirectory->setPlaceholderText(tr("Choose an output folder"));
        m_outputDirectory->setEnabled(false);

        auto *browseButton = new QPushButton(tr("Browse..."), folderWidget);
        browseButton->setEnabled(false);

        folderLayout->addWidget(m_outputDirectory, 1);
        folderLayout->addWidget(browseButton);
        outputLayout->addRow(tr("Output folder:"), folderWidget);

        connect(m_sameOutputFolder, &QCheckBox::toggled, this,
                [this, browseButton](bool sameFolder) {
                    m_outputDirectory->setEnabled(!sameFolder);
                    browseButton->setEnabled(!sameFolder);
                });

        connect(browseButton, &QPushButton::clicked, this, [this]() {
            QString startDirectory;
            if (m_outputDirectory && !m_outputDirectory->text().isEmpty())
                startDirectory = m_outputDirectory->text();
            else if (!m_files.isEmpty())
                startDirectory = QFileInfo(m_files.constFirst()).absolutePath();

            const QString directory = QFileDialog::getExistingDirectory(
                this, tr("Choose Output Folder"), startDirectory, QFileDialog::ShowDirsOnly);
            if (!directory.isEmpty())
                m_outputDirectory->setText(QDir::cleanPath(directory));
        });
    }

    layout->addWidget(outputBox);

    auto *processingBox = new QGroupBox(tr("Processing"), this);
    auto *processingLayout = new QFormLayout(processingBox);
    m_parallelJobs = new QSpinBox(processingBox);
    const int logicalProcessors = qMax(1, QThread::idealThreadCount());
    const int selectedFileCount = qMax(1, int(m_files.size()));
    const int maximumJobs = qMax(1, qMin(logicalProcessors, selectedFileCount));
    m_parallelJobs->setRange(1, maximumJobs);
    m_parallelJobs->setValue(qMin(4, maximumJobs));
    m_parallelJobs->setToolTip(tr("Number of ImageMagick processes allowed to run at the same time."));
    processingLayout->addRow(tr("Parallel jobs:"), m_parallelJobs);
    layout->addWidget(processingBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *runButton = buttons->addButton(tr("Process"), QDialogButtonBox::AcceptRole);
    runButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(runButton, &QPushButton::clicked, this, &ImageConverterDialog::processImages);
    layout->addWidget(buttons);
}

void ImageConverterDialog::buildResizeOptions()
{
    auto *box = new QGroupBox(tr("Resize"), this);
    auto *form = new QFormLayout(box);

    m_resizeMode = new QComboBox(box);
    m_resizeMode->addItem(tr("By width (height automatic)"), QStringLiteral("width"));
    m_resizeMode->addItem(tr("By height (width automatic)"), QStringLiteral("height"));
    m_resizeMode->addItem(tr("Fit inside width × height"), QStringLiteral("fit"));

    m_width = new QSpinBox(box);
    m_width->setRange(0, 100000);
    m_width->setSpecialValueText(tr("Auto"));
    m_width->setValue(m_lastWidth);
    m_width->setSuffix(tr(" px"));

    m_height = new QSpinBox(box);
    m_height->setRange(0, 100000);
    m_height->setSpecialValueText(tr("Auto"));
    m_height->setValue(0);
    m_height->setSuffix(tr(" px"));

    m_onlyShrink = new QCheckBox(tr("Do not enlarge smaller images"), box);
    m_onlyShrink->setChecked(true);

    m_resizeHint = new QLabel(box);
    m_resizeHint->setWordWrap(true);

    form->addRow(tr("Resize mode:"), m_resizeMode);
    form->addRow(tr("Width:"), m_width);
    form->addRow(tr("Height:"), m_height);
    form->addRow(m_onlyShrink);
    form->addRow(m_resizeHint);

    connect(m_width, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_updatingResizeUi && value > 0)
            m_lastWidth = value;
    });
    connect(m_height, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_updatingResizeUi && value > 0)
            m_lastHeight = value;
    });
    connect(m_resizeMode, &QComboBox::currentIndexChanged, this, &ImageConverterDialog::updateUiForResizeMode);

    updateUiForResizeMode();
    static_cast<QVBoxLayout *>(layout())->addWidget(box);
}

void ImageConverterDialog::updateUiForResizeMode()
{
    if (!m_resizeMode || !m_width || !m_height)
        return;

    m_updatingResizeUi = true;
    const QString mode = m_resizeMode->currentData().toString();

    if (mode == QStringLiteral("width")) {
        m_width->setEnabled(true);
        if (m_width->value() == 0)
            m_width->setValue(m_lastWidth);
        m_height->setValue(0);
        m_height->setEnabled(false);
        if (m_resizeHint)
            m_resizeHint->setText(tr("Height is calculated automatically for each image so its aspect ratio is preserved."));
    } else if (mode == QStringLiteral("height")) {
        m_height->setEnabled(true);
        if (m_height->value() == 0)
            m_height->setValue(m_lastHeight);
        m_width->setValue(0);
        m_width->setEnabled(false);
        if (m_resizeHint)
            m_resizeHint->setText(tr("Width is calculated automatically for each image so its aspect ratio is preserved."));
    } else {
        m_width->setEnabled(true);
        m_height->setEnabled(true);
        if (m_width->value() == 0)
            m_width->setValue(m_lastWidth);
        if (m_height->value() == 0)
            m_height->setValue(m_lastHeight);
        if (m_resizeHint)
            m_resizeHint->setText(tr("Each image is fitted inside this box while preserving its aspect ratio."));
    }

    m_updatingResizeUi = false;
}

void ImageConverterDialog::buildRotateOptions()
{
    auto *box = new QGroupBox(tr("Rotation"), this);
    auto *layout = new QVBoxLayout(box);
    const QString direction = m_mode == Mode::RotateLeft ? tr("90° counter-clockwise") : tr("90° clockwise");
    layout->addWidget(new QLabel(tr("Rotate each selected image %1.").arg(direction), box));
    static_cast<QVBoxLayout *>(this->layout())->addWidget(box);
}

void ImageConverterDialog::buildConvertOptions()
{
    auto *box = new QGroupBox(tr("Conversion"), this);
    auto *form = new QFormLayout(box);

    m_format = new QComboBox(box);
    m_format->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    m_format->addItem(QStringLiteral("AVIF"), QStringLiteral("avif"));
    m_format->addItem(QStringLiteral("JPEG"), QStringLiteral("jpg"));
    m_format->addItem(QStringLiteral("PNG"), QStringLiteral("png"));

    m_quality = new QSpinBox(box);
    m_quality->setRange(1, 100);
    m_quality->setValue(85);
    m_quality->setSuffix(QStringLiteral("%"));

    m_qualityLabel = new QLabel(tr("Quality:"), box);
    m_stripMetadata = new QCheckBox(tr("Remove EXIF and other metadata"), box);
    m_stripMetadata->setChecked(false);
    auto *metadataHint = new QLabel(
        tr("Removing metadata also removes embedded ICC color profiles."), box);
    metadataHint->setWordWrap(true);

    form->addRow(tr("Format:"), m_format);
    form->addRow(m_qualityLabel, m_quality);
    form->addRow(m_stripMetadata);
    form->addRow(metadataHint);

    connect(m_format, &QComboBox::currentIndexChanged, this, &ImageConverterDialog::updateUiForFormat);
    updateUiForFormat();

    static_cast<QVBoxLayout *>(layout())->addWidget(box);
}

void ImageConverterDialog::updateUiForFormat()
{
    if (!m_format || !m_quality)
        return;

    const QString format = m_format->currentData().toString();
    const bool qualityRelevant = format != QStringLiteral("png");
    m_quality->setEnabled(qualityRelevant);
    if (m_qualityLabel)
        m_qualityLabel->setEnabled(qualityRelevant);
}

bool ImageConverterDialog::outputUsesNoSuffix() const
{
    if (m_mode == Mode::Convert)
        return m_useOriginalBaseName && m_useOriginalBaseName->isChecked();
    return m_overwriteOriginal && m_overwriteOriginal->isChecked();
}

QString ImageConverterDialog::outputPath(const QString &input, const QString &extension) const
{
    const QFileInfo info(input);

    if (m_mode != Mode::Convert && outputUsesNoSuffix() && extension.isEmpty())
        return input;

    const QString targetExtension = extension.isEmpty() ? info.suffix() : extension;
    const QString suffix = outputUsesNoSuffix() ? QString() : m_suffix->text();
    const QString basename = info.completeBaseName() + suffix;

    QDir targetDirectory = info.dir();
    if (m_mode == Mode::Convert && m_sameOutputFolder && !m_sameOutputFolder->isChecked()
        && m_outputDirectory && !m_outputDirectory->text().isEmpty()) {
        targetDirectory = QDir(m_outputDirectory->text());
    }

    if (targetExtension.isEmpty())
        return targetDirectory.filePath(basename);

    return targetDirectory.filePath(basename + QStringLiteral(".") + targetExtension);
}

QStringList ImageConverterDialog::buildArguments(const QString &input, const QString &output) const
{
    QStringList args;

    if (m_mode == Mode::Convert) {
        const QString format = m_format->currentData().toString();
        // JPEG, ordinary PNG, and AVIF outputs are treated as single-frame.
        // Reading only frame 0 prevents ImageMagick from creating name-0/name-1
        // side outputs for animated GIFs or multi-page TIFF files.
        if (format == QStringLiteral("jpg")
            || format == QStringLiteral("png")
            || format == QStringLiteral("avif")) {
            args << (input + QStringLiteral("[0]"));
        } else {
            args << input;
        }
    } else {
        args << input;
    }

    switch (m_mode) {
    case Mode::Resize: {
        const QString mode = m_resizeMode->currentData().toString();
        QString geometry;
        if (mode == QStringLiteral("width"))
            geometry = QStringLiteral("%1x").arg(m_width->value());
        else if (mode == QStringLiteral("height"))
            geometry = QStringLiteral("x%1").arg(m_height->value());
        else
            geometry = QStringLiteral("%1x%2").arg(m_width->value()).arg(m_height->value());

        if (m_onlyShrink->isChecked())
            geometry += QLatin1Char('>');

        args << QStringLiteral("-auto-orient")
             << QStringLiteral("-resize") << geometry
             << output;
        break;
    }
    case Mode::RotateLeft:
        args << QStringLiteral("-auto-orient")
             << QStringLiteral("-rotate") << QStringLiteral("-90")
             << output;
        break;
    case Mode::RotateRight:
        args << QStringLiteral("-auto-orient")
             << QStringLiteral("-rotate") << QStringLiteral("90")
             << output;
        break;
    case Mode::Convert: {
        const QString format = m_format->currentData().toString();

        // Apply EXIF Orientation before metadata can be stripped. This is
        // especially important for phone photos converted to formats where
        // Orientation metadata is not preserved consistently.
        args << QStringLiteral("-auto-orient");

        // JPEG has no alpha channel. Flatten transparent pixels onto white
        // instead of letting them become black on conversion.
        if (format == QStringLiteral("jpg")) {
            args << QStringLiteral("-background") << QStringLiteral("white")
                 << QStringLiteral("-alpha") << QStringLiteral("remove")
                 << QStringLiteral("-alpha") << QStringLiteral("off");
        }

        if (m_stripMetadata->isChecked())
            args << QStringLiteral("-strip");
        if (format != QStringLiteral("png"))
            args << QStringLiteral("-quality") << QString::number(m_quality->value());
        if (format == QStringLiteral("webp")) {
            const bool parallel = m_parallelJobs && m_parallelJobs->value() > 1;
            args << QStringLiteral("-define")
                 << QStringLiteral("webp:thread-level=%1").arg(parallel ? 0 : 1);
        }
        args << output;
        break;
    }
    }

    return args;
}

static QString resolvedCommitPath(const QString &requestedOutput)
{
    const QFileInfo info(requestedOutput);
    if (info.isSymLink()) {
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty())
            return canonical;
    }
    return info.absoluteFilePath();
}

static QString temporaryOutputPathFor(const QString &commitOutput,
                                      const QString &requestedOutput,
                                      const QString &input)
{
    QString extension = QFileInfo(requestedOutput).suffix();
    if (extension.isEmpty()) {
        QMimeDatabase mimeDatabase;
        const QMimeType mime = mimeDatabase.mimeTypeForFile(input, QMimeDatabase::MatchContent);
        extension = mime.preferredSuffix();
    }

    QString name = QStringLiteral(".dolphin-image-converter-%1-%2")
                       .arg(QCoreApplication::applicationPid())
                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!extension.isEmpty())
        name += QStringLiteral(".") + extension;

    return QFileInfo(commitOutput).dir().filePath(name);
}

static qint64 hardLinkCount(const QString &path)
{
#ifdef Q_OS_UNIX
    struct stat st {};
    const QByteArray encoded = QFile::encodeName(path);
    if (::stat(encoded.constData(), &st) == 0 && S_ISREG(st.st_mode))
        return qint64(st.st_nlink);
#else
    Q_UNUSED(path);
#endif
    return 1;
}

void ImageConverterDialog::reject()
{
    // Keep the owner dialog alive while ImageMagickRunner is stopping child
    // processes. This also covers the window-manager close button.
    if (m_runner)
        return;
    QDialog::reject();
}

static QString imageMagickFormatForPath(const QString &path)
{
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty()) {
        QMimeDatabase database;
        suffix = database.mimeTypeForFile(path, QMimeDatabase::MatchContent).preferredSuffix().toLower();
    }

    if (suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("jpe")
        || suffix == QStringLiteral("jfif")
        || suffix == QStringLiteral("jif")) {
        return QStringLiteral("JPEG");
    }
    if (suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff"))
        return QStringLiteral("TIFF");
    if (suffix == QStringLiteral("heic") || suffix == QStringLiteral("heif"))
        return QStringLiteral("HEIC");
    return suffix.toUpper();
}

void ImageConverterDialog::processImages()
{
    if (m_runner)
        return;

    if (!ImageMagickRunner::isAvailable()) {
        QMessageBox::critical(this, tr("ImageMagick not found"),
                              tr("The 'magick' executable was not found in PATH.\n\n"
                                 "Install ImageMagick 7 and try again."));
        return;
    }

    QSet<QString> requiredWritableFormats;
    if (m_mode == Mode::Convert) {
        requiredWritableFormats.insert(m_format->currentData().toString().toUpper());
    } else {
        for (const QString &input : m_files) {
            const QString format = imageMagickFormatForPath(input);
            if (!format.isEmpty())
                requiredWritableFormats.insert(format);
        }
    }

    QStringList unavailableFormats;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString &format : std::as_const(requiredWritableFormats)) {
        if (!ImageMagickRunner::canWriteFormat(format))
            unavailableFormats << format;
    }
    QApplication::restoreOverrideCursor();
    if (!unavailableFormats.isEmpty()) {
        unavailableFormats.sort(Qt::CaseInsensitive);
        QMessageBox::warning(
            this,
            tr("Output format unavailable"),
            tr("ImageMagick cannot write the following output format(s) on this system:\n\n%1\n\n"
               "Install the required encoder/delegate or choose a different output format.")
                .arg(unavailableFormats.join(QStringLiteral(", "))));
        return;
    }

    if (!outputUsesNoSuffix() && m_suffix->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Missing suffix"),
                             tr("Choose a filename suffix or enable the no-suffix/overwrite option."));
        return;
    }

    if (m_mode == Mode::Resize) {
        const QString resizeMode = m_resizeMode->currentData().toString();
        const bool invalidWidth = (resizeMode == QStringLiteral("width") || resizeMode == QStringLiteral("fit"))
                               && m_width->value() <= 0;
        const bool invalidHeight = (resizeMode == QStringLiteral("height") || resizeMode == QStringLiteral("fit"))
                                && m_height->value() <= 0;
        if (invalidWidth || invalidHeight) {
            QMessageBox::warning(this, tr("Invalid resize size"),
                                 tr("Width and height used by the selected resize mode must be greater than zero."));
            return;
        }
    }

    if (m_mode == Mode::Convert && m_sameOutputFolder && !m_sameOutputFolder->isChecked()) {
        if (!m_outputDirectory || m_outputDirectory->text().isEmpty()) {
            QMessageBox::warning(this, tr("Output folder required"),
                                 tr("Choose an output folder or enable 'Same folder as source'."));
            return;
        }

        const QFileInfo outputDirectoryInfo(m_outputDirectory->text());
        if (!outputDirectoryInfo.exists() || !outputDirectoryInfo.isDir()) {
            QMessageBox::warning(this, tr("Invalid output folder"),
                                 tr("The selected output folder does not exist."));
            return;
        }
        if (!outputDirectoryInfo.isWritable()) {
            QMessageBox::warning(this, tr("Output folder is not writable"),
                                 tr("You do not have permission to write to the selected output folder."));
            return;
        }
    }

    auto identityPath = [](const QString &path) {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
    };

    int sourceOverwrites = 0;
    int existingOutputs = 0;
    QSet<QString> plannedOutputs;
    QSet<QString> selectedSources;
    QStringList duplicateOutputs;
    QStringList conflictingSelectedSources;
    QStringList danglingOutputSymlinks;
    QStringList hardLinkedOutputs;
    QSet<QString> seenHardLinkedOutputs;

    for (const QString &input : m_files)
        selectedSources.insert(identityPath(QFileInfo(input).absoluteFilePath()));

    for (const QString &input : m_files) {
        QString extension;
        if (m_mode == Mode::Convert)
            extension = m_format->currentData().toString();

        const QString target = QFileInfo(outputPath(input, extension)).absoluteFilePath();
        const QString source = QFileInfo(input).absoluteFilePath();
        const QFileInfo targetInfo(target);

        if (targetInfo.isSymLink() && targetInfo.canonicalFilePath().isEmpty()) {
            danglingOutputSymlinks << target;
            continue;
        }

        const QString commitTarget = resolvedCommitPath(target);
        const QString targetIdentity = identityPath(commitTarget);
        const QString sourceIdentity = identityPath(source);

        if (plannedOutputs.contains(targetIdentity))
            duplicateOutputs << QFileInfo(target).fileName();
        else
            plannedOutputs.insert(targetIdentity);

        if (QFileInfo::exists(commitTarget) && hardLinkCount(commitTarget) > 1
            && !seenHardLinkedOutputs.contains(targetIdentity)) {
            seenHardLinkedOutputs.insert(targetIdentity);
            hardLinkedOutputs << target;
        }

        if (targetIdentity == sourceIdentity) {
            ++sourceOverwrites;
        } else if (selectedSources.contains(targetIdentity)) {
            conflictingSelectedSources << QFileInfo(target).fileName();
        } else if (QFileInfo::exists(commitTarget)) {
            ++existingOutputs;
        }
    }

    if (!danglingOutputSymlinks.isEmpty()) {
        danglingOutputSymlinks.removeDuplicates();
        QMessageBox::warning(
            this,
            tr("Dangling output symbolic link"),
            tr("An output path is a symbolic link whose target does not exist:\n\n%1\n\n"
               "Choose a different filename or output folder before continuing.")
                .arg(danglingOutputSymlinks.join(QStringLiteral("\n"))));
        return;
    }

    if (!duplicateOutputs.isEmpty()) {
        duplicateOutputs.removeDuplicates();
        QMessageBox::warning(
            this,
            tr("Duplicate output names"),
            tr("Two or more selected images would create the same output filename:\n\n%1\n\n"
               "Change the filename suffix or choose a different output folder.")
                .arg(duplicateOutputs.join(QStringLiteral("\n"))));
        return;
    }

    if (!conflictingSelectedSources.isEmpty()) {
        conflictingSelectedSources.removeDuplicates();
        QMessageBox::warning(
            this,
            tr("Output conflicts with selected source"),
            tr("An output path would replace another selected source image:\n\n%1\n\n"
               "Change the filename suffix or output folder.")
                .arg(conflictingSelectedSources.join(QStringLiteral("\n"))));
        return;
    }

    if (!hardLinkedOutputs.isEmpty()) {
        hardLinkedOutputs.removeDuplicates();
        const auto answer = QMessageBox::warning(
            this,
            tr("Hard-linked output files"),
            tr("%n output file(s) have multiple hard links. Atomic replacement changes only the selected path; "
               "other hard links will keep the previous file content.\n\nContinue?",
               nullptr,
               hardLinkedOutputs.size()),
            QMessageBox::Cancel | QMessageBox::Ok,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Ok)
            return;
    }

    if (sourceOverwrites > 0) {
        const auto answer = QMessageBox::warning(
            this,
            tr("Overwrite source images?"),
            tr("%n source image(s) will be overwritten. This cannot be undone by this application.",
               nullptr,
               sourceOverwrites),
            QMessageBox::Cancel | QMessageBox::Ok,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Ok)
            return;
    }

    if (existingOutputs > 0) {
        const auto answer = QMessageBox::warning(
            this,
            tr("Existing output files"),
            tr("%n output file(s) already exist and will be replaced.", nullptr, existingOutputs),
            QMessageBox::Cancel | QMessageBox::Ok,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Ok)
            return;
    }

    QString label;
    switch (m_mode) {
    case Mode::Resize: label = tr("Resizing images..."); break;
    case Mode::RotateLeft:
    case Mode::RotateRight: label = tr("Rotating images..."); break;
    case Mode::Convert: label = tr("Converting images..."); break;
    }

    m_runner = new ImageMagickRunner(this);
    connect(m_runner,
            &ImageMagickRunner::finished,
            this,
            [this](int succeeded,
                   int failed,
                   bool canceled,
                   const QStringList &errors,
                   const QStringList &warnings) {
                if (m_runner) {
                    m_runner->deleteLater();
                    m_runner = nullptr;
                }

                auto combinedDetails = [&errors, &warnings]() {
                    QStringList sections;
                    if (!errors.isEmpty())
                        sections << tr("Errors:\n%1").arg(errors.join(QStringLiteral("\n\n")));
                    if (!warnings.isEmpty())
                        sections << tr("Warnings:\n%1").arg(warnings.join(QStringLiteral("\n\n")));
                    QString details = sections.join(QStringLiteral("\n\n"));
                    if (details.size() > 6000)
                        details = details.left(6000) + tr("\n\n[additional details omitted]");
                    return details;
                };

                if (canceled) {
                    const int canceledOrNotProcessed = qMax(0, int(m_files.size()) - succeeded - failed);
                    QMessageBox message(QMessageBox::Information,
                                        tr("Canceled"),
                                        tr("Processing was canceled.\n\nSuccessful: %1\nFailed: %2\nCanceled or not processed: %3")
                                            .arg(succeeded)
                                            .arg(failed)
                                            .arg(canceledOrNotProcessed),
                                        QMessageBox::Ok,
                                        this);
                    if (!errors.isEmpty() || !warnings.isEmpty())
                        message.setDetailedText(combinedDetails());
                    message.exec();
                    return;
                }

                if (failed == 0 && warnings.isEmpty()) {
                    QMessageBox::information(this,
                                             tr("Done"),
                                             tr("Processed %n image(s) successfully.", nullptr, succeeded));
                    accept();
                    return;
                }

                if (failed == 0) {
                    const QString processed =
                        tr("Processed %n image(s) successfully.", nullptr, succeeded);
                    const QString reported =
                        tr("%n warning(s) were reported.", nullptr, warnings.size());
                    QMessageBox message(QMessageBox::Warning,
                                        tr("Completed with warnings"),
                                        processed + QStringLiteral("\n") + reported,
                                        QMessageBox::Ok,
                                        this);
                    message.setDetailedText(combinedDetails());
                    message.exec();
                    accept();
                    return;
                }

                QMessageBox message(QMessageBox::Warning,
                                    tr("Completed with errors"),
                                    tr("Successful: %1\nFailed: %2\nWarnings: %3")
                                        .arg(succeeded)
                                        .arg(failed)
                                        .arg(warnings.size()),
                                    QMessageBox::Ok,
                                    this);
                message.setDetailedText(combinedDetails());
                message.exec();
            });

    QVector<ImageMagickJob> jobs;
    jobs.reserve(m_files.size());
    for (const QString &input : m_files) {
        QString extension;
        if (m_mode == Mode::Convert)
            extension = m_format->currentData().toString();

        const QString requestedOutput = QFileInfo(outputPath(input, extension)).absoluteFilePath();
        const QString finalOutput = resolvedCommitPath(requestedOutput);
        const QString temporaryOutput = temporaryOutputPathFor(finalOutput, requestedOutput, input);

        ImageMagickJob job;
        job.input = input;
        job.finalOutput = finalOutput;
        job.temporaryOutput = temporaryOutput;
        job.arguments = buildArguments(input, temporaryOutput);
        jobs.push_back(std::move(job));
    }

    QStringList stagingDirectories;
    QSet<QString> seenStagingDirectories;
    for (const ImageMagickJob &job : std::as_const(jobs)) {
        const QString directory = QFileInfo(job.temporaryOutput).absolutePath();
        if (!seenStagingDirectories.contains(directory)) {
            seenStagingDirectories.insert(directory);
            stagingDirectories << directory;
        }
    }
    ImageMagickRunner::cleanupStaleTemporaryOutputs(stagingDirectories);

    m_runner->start(
        this,
        std::move(jobs),
        label,
        m_parallelJobs ? m_parallelJobs->value() : 1);
}
