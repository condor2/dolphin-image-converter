// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class ImageMagickRunner;

class ImageConverterDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode {
        Resize,
        RotateLeft,
        RotateRight,
        Convert
    };

    explicit ImageConverterDialog(Mode mode, QStringList files, QWidget *parent = nullptr);
    void reject() override;

private slots:
    void processImages();
    void updateUiForFormat();
    void updateUiForResizeMode();

private:
    QString outputPath(const QString &input, const QString &extension = {}) const;
    bool outputUsesNoSuffix() const;
    QStringList buildArguments(const QString &input, const QString &output) const;
    void buildUi();
    void buildResizeOptions();
    void buildRotateOptions();
    void buildConvertOptions();

    Mode m_mode;
    QStringList m_files;

    QComboBox *m_resizeMode = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QCheckBox *m_onlyShrink = nullptr;
    QSpinBox *m_quality = nullptr;
    QSpinBox *m_parallelJobs = nullptr;
    QComboBox *m_format = nullptr;
    QCheckBox *m_stripMetadata = nullptr;
    QCheckBox *m_overwriteOriginal = nullptr;
    QCheckBox *m_useOriginalBaseName = nullptr;
    QLineEdit *m_suffix = nullptr;
    QCheckBox *m_sameOutputFolder = nullptr;
    QLineEdit *m_outputDirectory = nullptr;
    QLabel *m_qualityLabel = nullptr;
    QLabel *m_resizeHint = nullptr;
    int m_lastWidth = 1920;
    int m_lastHeight = 1080;
    bool m_updatingResizeUi = false;
    ImageMagickRunner *m_runner = nullptr;
};
