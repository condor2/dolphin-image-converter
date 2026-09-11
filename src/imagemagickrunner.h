// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

class QProgressDialog;
class QWidget;

struct ImageMagickJob
{
    QString input;
    QStringList arguments;
    QString temporaryOutput;
    QString finalOutput;
};

class ImageMagickRunner : public QObject
{
    Q_OBJECT

public:
    explicit ImageMagickRunner(QObject *parent = nullptr);
    ~ImageMagickRunner() override;

    static QString executable();
    static bool isAvailable();
    static bool canWriteFormat(const QString &format);
    static void cleanupStaleTemporaryOutputs(const QStringList &directories);

    void start(
        QWidget *parent,
        QVector<ImageMagickJob> jobs,
        const QString &operationLabel,
        int maxWorkers
    );

signals:
    void finished(int succeeded, int failed, bool canceled, const QStringList &errors, const QStringList &warnings);

private slots:
    void cancel();

private:
    void pumpQueue();
    void startJob(int index);
    void handleProcessFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess *process, QProcess::ProcessError error);
    void updateProgress();
    void finishBatch();
    void recordFailure(const QString &input, const QString &message);
    void cleanupTemporaryOutput(const QString &path);
    void cleanupAllTemporaryOutputs();
    bool commitTemporaryOutput(const ImageMagickJob &job, QString *errorMessage, QStringList *warnings);
    static qint64 parallelMemoryLimitMiB(int workers);

    QVector<ImageMagickJob> m_jobs;
    QHash<QProcess *, int> m_activeProcesses;
    QProgressDialog *m_progress = nullptr;
    QString m_operationLabel;
    int m_nextIndex = 0;
    int m_completed = 0;
    int m_succeeded = 0;
    int m_failed = 0;
    int m_maxWorkers = 1;
    qint64 m_memoryLimitMiB = 0;
    bool m_canceled = false;
    bool m_finished = false;
    QStringList m_errors;
    QStringList m_warnings;
};
