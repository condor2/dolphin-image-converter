#include "imageconverterdialog.h"
#include "imagemagickrunner.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
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

    m_overwrite = new QCheckBox(outputBox);
    if (m_mode == Mode::Convert) {
        m_overwrite->setText(tr("Use original base name (no suffix)"));
        m_overwrite->setChecked(false);
    } else {
        m_overwrite->setText(tr("Overwrite original files"));
        m_overwrite->setChecked(false);
    }
    outputLayout->addRow(m_overwrite);

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

    connect(m_overwrite, &QCheckBox::toggled, m_suffix, [this](bool checked) {
        m_suffix->setEnabled(!checked);
    });

    layout->addWidget(outputBox);

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

    form->addRow(tr("Format:"), m_format);
    form->addRow(m_qualityLabel, m_quality);
    form->addRow(m_stripMetadata);

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

QString ImageConverterDialog::outputPath(const QString &input, const QString &extension) const
{
    const QFileInfo info(input);

    if (m_overwrite && m_overwrite->isChecked() && extension.isEmpty())
        return input;

    const QString targetExtension = extension.isEmpty() ? info.suffix() : extension;
    const QString suffix = (m_overwrite && m_overwrite->isChecked()) ? QString() : m_suffix->text();
    const QString basename = info.completeBaseName() + suffix;

    return info.dir().filePath(basename + QStringLiteral(".") + targetExtension);
}

QStringList ImageConverterDialog::buildArguments(const QString &input) const
{
    QStringList args;
    args << input;

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
             << outputPath(input);
        break;
    }
    case Mode::RotateLeft:
        args << QStringLiteral("-auto-orient")
             << QStringLiteral("-rotate") << QStringLiteral("-90")
             << outputPath(input);
        break;
    case Mode::RotateRight:
        args << QStringLiteral("-auto-orient")
             << QStringLiteral("-rotate") << QStringLiteral("90")
             << outputPath(input);
        break;
    case Mode::Convert: {
        const QString format = m_format->currentData().toString();
        if (m_stripMetadata->isChecked())
            args << QStringLiteral("-strip");
        if (format != QStringLiteral("png"))
            args << QStringLiteral("-quality") << QString::number(m_quality->value());
        if (format == QStringLiteral("webp"))
            args << QStringLiteral("-define") << QStringLiteral("webp:thread-level=1");
        args << outputPath(input, format);
        break;
    }
    }

    return args;
}

void ImageConverterDialog::processImages()
{
    if (!ImageMagickRunner::isAvailable()) {
        QMessageBox::critical(this, tr("ImageMagick not found"),
                              tr("The 'magick' executable was not found in PATH.\n\n"
                                 "Install ImageMagick 7 and try again."));
        return;
    }

    if (!m_overwrite->isChecked() && m_suffix->text().isEmpty() && m_mode != Mode::Convert) {
        QMessageBox::warning(this, tr("Missing suffix"),
                             tr("Choose a filename suffix or enable overwrite."));
        return;
    }

    if (m_overwrite->isChecked() && m_mode != Mode::Convert) {
        const auto answer = QMessageBox::warning(
            this,
            tr("Overwrite original files?"),
            tr("The selected images will be replaced. This cannot be undone by this application."),
            QMessageBox::Cancel | QMessageBox::Ok,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Ok)
            return;
    }

    int existingOutputs = 0;
    for (const QString &input : m_files) {
        QString extension;
        if (m_mode == Mode::Convert)
            extension = m_format->currentData().toString();
        const QString target = outputPath(input, extension);
        if (target != input && QFileInfo::exists(target))
            ++existingOutputs;
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

    const auto result = ImageMagickRunner::runBatch(
        this,
        m_files,
        label,
        [this](const QString &input) { return buildArguments(input); });

    if (result.failed == 0) {
        QMessageBox::information(this, tr("Done"),
                                 tr("Processed %n image(s) successfully.", nullptr, result.succeeded));
        accept();
        return;
    }

    QString details = result.errors.join(QStringLiteral("\n\n"));
    if (details.size() > 6000)
        details = details.left(6000) + tr("\n\n[additional errors omitted]");

    QMessageBox message(QMessageBox::Warning,
                        tr("Completed with errors"),
                        tr("Successful: %1\nFailed: %2").arg(result.succeeded).arg(result.failed),
                        QMessageBox::Ok,
                        this);
    message.setDetailedText(details);
    message.exec();
}
