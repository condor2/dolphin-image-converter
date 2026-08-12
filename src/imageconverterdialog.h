#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;

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

private slots:
    void processImages();
    void updateUiForFormat();
    void updateUiForResizeMode();

private:
    QString outputPath(const QString &input, const QString &extension = {}) const;
    QStringList buildArguments(const QString &input) const;
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
    QComboBox *m_format = nullptr;
    QCheckBox *m_stripMetadata = nullptr;
    QCheckBox *m_overwrite = nullptr;
    QLineEdit *m_suffix = nullptr;
    QLabel *m_qualityLabel = nullptr;
    QLabel *m_resizeHint = nullptr;
    int m_lastWidth = 1920;
    int m_lastHeight = 1080;
    bool m_updatingResizeUi = false;
};
