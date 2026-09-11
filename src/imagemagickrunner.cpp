// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagemagickrunner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <cerrno>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#endif

#ifdef Q_OS_LINUX
#include <sys/xattr.h>
#endif
#include <utility>
#include <vector>

ImageMagickRunner::ImageMagickRunner(QObject *parent)
    : QObject(parent)
{
}

ImageMagickRunner::~ImageMagickRunner()
{
    cleanupAllTemporaryOutputs();
}

QString ImageMagickRunner::executable()
{
    return QStandardPaths::findExecutable(QStringLiteral("magick"));
}

bool ImageMagickRunner::isAvailable()
{
    return !executable().isEmpty();
}

static bool processIdIsAlive(qint64 pid)
{
#ifdef Q_OS_UNIX
    if (pid <= 0)
        return false;
    errno = 0;
    if (::kill(pid_t(pid), 0) == 0)
        return true;
    return errno == EPERM;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

void ImageMagickRunner::cleanupStaleTemporaryOutputs(const QStringList &directories)
{
    // Temporary files include the creator PID. A second exact pattern is kept
    // for early development builds that used UUID-only names. Unrelated hidden
    // files are never touched.
    static const QRegularExpression currentPattern(
        QStringLiteral(R"(^\.dolphin-image-converter-(\d+)-[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}(?:\.[^/]+)?$)"));
    static const QRegularExpression legacyPattern(
        QStringLiteral(R"(^\.dolphin-image-converter-[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}(?:\.[^/]+)?$)"));

    const QDateTime now = QDateTime::currentDateTimeUtc();
    constexpr qint64 currentMinAgeSeconds = 12 * 60 * 60;
    constexpr qint64 legacyMinAgeSeconds = 24 * 60 * 60;

    QStringList uniqueDirectories = directories;
    uniqueDirectories.removeDuplicates();

    for (const QString &directoryPath : std::as_const(uniqueDirectories)) {
        QDir directory(directoryPath);
        if (!directory.exists())
            continue;

        const QFileInfoList entries = directory.entryInfoList(
            QStringList{QStringLiteral(".dolphin-image-converter-*")},
            QDir::Files | QDir::Hidden | QDir::NoSymLinks,
            QDir::NoSort);

        for (const QFileInfo &entry : entries) {
            const QString name = entry.fileName();
            const qint64 ageSeconds = entry.lastModified().toUTC().secsTo(now);
            if (ageSeconds < 0)
                continue;

            const QRegularExpressionMatch currentMatch = currentPattern.match(name);
            if (currentMatch.hasMatch()) {
                const qint64 pid = currentMatch.captured(1).toLongLong();
                if (ageSeconds >= currentMinAgeSeconds && !processIdIsAlive(pid))
                    QFile::remove(entry.absoluteFilePath());
                continue;
            }

            if (legacyPattern.match(name).hasMatch() && ageSeconds >= legacyMinAgeSeconds)
                QFile::remove(entry.absoluteFilePath());
        }
    }
}

qint64 ImageMagickRunner::parallelMemoryLimitMiB(int workers)
{
    if (workers <= 1)
        return 0;

    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (!meminfo.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    qint64 totalKiB = 0;
    QTextStream stream(&meminfo);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (!line.startsWith(QStringLiteral("MemTotal:")))
            continue;

        const QStringList parts = line.simplified().split(QLatin1Char(' '));
        if (parts.size() >= 2)
            totalKiB = parts.at(1).toLongLong();
        break;
    }

    if (totalKiB <= 0)
        return 0;

    const qint64 totalMiB = totalKiB / 1024;
    // Reserve roughly half of physical RAM for the OS, Qt, filesystem cache and
    // other applications. ImageMagick can spill pixel cache to disk after this
    // per-worker memory limit is reached.
    const qint64 perWorker = totalMiB / (qMax(1, workers) * 2LL);
    return qBound<qint64>(qint64{128}, perWorker, qint64{2048});
}

void ImageMagickRunner::start(
    QWidget *parent,
    QVector<ImageMagickJob> jobs,
    const QString &operationLabel,
    int maxWorkers)
{
    m_jobs = std::move(jobs);
    m_operationLabel = operationLabel;
    m_nextIndex = 0;
    m_completed = 0;
    m_succeeded = 0;
    m_failed = 0;
    m_canceled = false;
    m_finished = false;
    m_errors.clear();
    m_warnings.clear();
    m_activeProcesses.clear();
    const int jobCount = int(m_jobs.size());
    m_maxWorkers = qMax(1, qMin(maxWorkers, qMax(1, jobCount)));
    m_memoryLimitMiB = parallelMemoryLimitMiB(m_maxWorkers);

    m_progress = new QProgressDialog(operationLabel, tr("Cancel"), 0, jobCount, parent);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    connect(m_progress, &QProgressDialog::canceled, this, &ImageMagickRunner::cancel);

    QTimer::singleShot(0, this, &ImageMagickRunner::pumpQueue);
}

void ImageMagickRunner::pumpQueue()
{
    if (m_finished)
        return;

    if (m_canceled) {
        if (m_activeProcesses.isEmpty())
            finishBatch();
        return;
    }

    updateProgress();

    // QProgressDialog::setValue() may process events for modal dialogs. A Cancel
    // click can therefore re-enter cancel() while updateProgress() is running.
    if (m_finished || m_canceled || !m_progress)
        return;

    while (!m_canceled
           && m_activeProcesses.size() < m_maxWorkers
           && m_nextIndex < m_jobs.size()) {
        const int index = m_nextIndex++;
        startJob(index);

        if (m_finished || m_canceled)
            return;
    }

    updateProgress();

    if (m_finished || m_canceled || !m_progress)
        return;

    if (m_completed >= m_jobs.size() && m_activeProcesses.isEmpty())
        finishBatch();
}

void ImageMagickRunner::startJob(int index)
{
    if (m_finished || m_canceled || index < 0 || index >= m_jobs.size())
        return;

    auto *process = new QProcess(this);
    m_activeProcesses.insert(process, index);

    connect(process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                handleProcessFinished(process, exitCode, exitStatus);
            });
    connect(process,
            &QProcess::errorOccurred,
            this,
            [this, process](QProcess::ProcessError error) {
                handleProcessError(process, error);
            });

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (m_maxWorkers > 1) {
        // Parallelism is managed at process level. Prevent every ImageMagick
        // worker from creating its own OpenMP-sized thread pool.
        environment.insert(QStringLiteral("MAGICK_THREAD_LIMIT"), QStringLiteral("1"));
        if (m_memoryLimitMiB > 0) {
            environment.insert(QStringLiteral("MAGICK_MEMORY_LIMIT"),
                               QStringLiteral("%1MiB").arg(m_memoryLimitMiB));
        }
    }

    const ImageMagickJob &job = m_jobs.at(index);
    process->setProcessEnvironment(environment);
    process->setProgram(executable());
    process->setArguments(job.arguments);
    process->start();
}

static bool copyLinuxExtendedMetadata(const QString &source,
                                      const QString &destination,
                                      QString *errorMessage)
{
#ifdef Q_OS_LINUX
    const QByteArray sourceName = QFile::encodeName(source);
    const QByteArray destinationName = QFile::encodeName(destination);

    errno = 0;
    const ssize_t listSize = ::listxattr(sourceName.constData(), nullptr, 0);
    if (listSize < 0) {
        if (errno == ENOTSUP || errno == ENODATA)
            return true;
        if (errorMessage)
            *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    if (listSize == 0)
        return true;

    std::vector<char> names(static_cast<size_t>(listSize));
    const ssize_t actualListSize = ::listxattr(sourceName.constData(), names.data(), names.size());
    if (actualListSize < 0) {
        if (errorMessage)
            *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }

    const char *cursor = names.data();
    const char *end = names.data() + actualListSize;
    while (cursor < end && *cursor != '\0') {
        const QByteArray attribute(cursor);
        cursor += attribute.size() + 1;

        // Preserve desktop/user metadata and POSIX ACLs. Privileged security.*
        // namespaces are intentionally left to the operating system because an
        // ordinary desktop user usually cannot recreate them on a new inode.
        if (!attribute.startsWith("user.")
            && attribute != QByteArrayLiteral("system.posix_acl_access")
            && attribute != QByteArrayLiteral("system.posix_acl_default")) {
            continue;
        }

        errno = 0;
        const ssize_t valueSize = ::getxattr(sourceName.constData(), attribute.constData(), nullptr, 0);
        if (valueSize < 0) {
            if (errno == ENODATA)
                continue;
            if (errorMessage)
                *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }

        std::vector<char> value(static_cast<size_t>(valueSize));
        if (valueSize > 0) {
            const ssize_t actualValueSize = ::getxattr(
                sourceName.constData(), attribute.constData(), value.data(), value.size());
            if (actualValueSize < 0) {
                if (errno == ENODATA)
                    continue;
                if (errorMessage)
                    *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
                return false;
            }
            value.resize(size_t(actualValueSize));
        }

        errno = 0;
        if (::setxattr(destinationName.constData(),
                       attribute.constData(),
                       value.empty() ? nullptr : value.data(),
                       value.size(),
                       0) != 0) {
            if (errorMessage)
                *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
    }
#else
    Q_UNUSED(source);
    Q_UNUSED(destination);
    Q_UNUSED(errorMessage);
#endif
    return true;
}

bool ImageMagickRunner::commitTemporaryOutput(const ImageMagickJob &job, QString *errorMessage, QStringList *warnings)
{
    if (job.temporaryOutput.isEmpty() || job.finalOutput.isEmpty()) {
        if (errorMessage)
            *errorMessage = tr("Invalid temporary or final output path.");
        return false;
    }

    if (!QFileInfo::exists(job.temporaryOutput)) {
        if (errorMessage)
            *errorMessage = tr("ImageMagick reported success but did not create the output file.");
        return false;
    }

    if (QFileInfo::exists(job.finalOutput)) {
        if (!QFile::setPermissions(job.temporaryOutput, QFile::permissions(job.finalOutput))) {
            if (warnings)
                warnings->append(tr("Could not preserve existing file permissions."));
        }

        QString metadataError;
        if (!copyLinuxExtendedMetadata(job.finalOutput, job.temporaryOutput, &metadataError)) {
            if (warnings)
                warnings->append(tr("Could not preserve existing file metadata: %1").arg(metadataError));
        }
    }

    const QByteArray temporaryName = QFile::encodeName(job.temporaryOutput);
    const QByteArray finalName = QFile::encodeName(job.finalOutput);

    // POSIX rename(2) atomically replaces an existing destination when source
    // and destination are on the same filesystem. Temporary outputs are always
    // created in the final file's directory, so cancellation, disk-full errors
    // and ImageMagick failures leave the original destination untouched.
    errno = 0;
    if (::rename(temporaryName.constData(), finalName.constData()) == 0)
        return true;

    if (errorMessage) {
        *errorMessage = tr("Could not atomically replace the output file: %1")
                            .arg(QString::fromLocal8Bit(std::strerror(errno)));
    }
    return false;
}

void ImageMagickRunner::handleProcessFinished(
    QProcess *process,
    int exitCode,
    QProcess::ExitStatus exitStatus)
{
    if (m_finished || !m_activeProcesses.contains(process))
        return;

    const int index = m_activeProcesses.take(process);
    const ImageMagickJob job = m_jobs.value(index);

    if (m_canceled) {
        cleanupTemporaryOutput(job.temporaryOutput);
    } else {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            QString commitError;
            QStringList commitWarnings;
            if (commitTemporaryOutput(job, &commitError, &commitWarnings)) {
                ++m_succeeded;
                for (const QString &warning : std::as_const(commitWarnings))
                    m_warnings << tr("%1: %2").arg(QFileInfo(job.input).fileName(), warning);
            } else {
                cleanupTemporaryOutput(job.temporaryOutput);
                recordFailure(job.input, commitError);
            }
        } else {
            cleanupTemporaryOutput(job.temporaryOutput);
            const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
            recordFailure(job.input, stderrText.isEmpty() ? tr("ImageMagick failed.") : stderrText);
        }
        ++m_completed;
    }

    process->deleteLater();

    if (m_canceled) {
        if (m_activeProcesses.isEmpty())
            finishBatch();
        return;
    }

    updateProgress();
    if (m_finished || m_canceled || !m_progress)
        return;

    QTimer::singleShot(0, this, &ImageMagickRunner::pumpQueue);
}

void ImageMagickRunner::handleProcessError(QProcess *process, QProcess::ProcessError error)
{
    if (m_finished || error != QProcess::FailedToStart || !m_activeProcesses.contains(process))
        return;

    const int index = m_activeProcesses.take(process);
    const ImageMagickJob job = m_jobs.value(index);
    cleanupTemporaryOutput(job.temporaryOutput);

    if (!m_canceled) {
        recordFailure(job.input, tr("Could not start ImageMagick."));
        ++m_completed;
    }

    process->deleteLater();

    if (m_canceled) {
        if (m_activeProcesses.isEmpty())
            finishBatch();
        return;
    }

    updateProgress();
    if (m_finished || m_canceled || !m_progress)
        return;

    QTimer::singleShot(0, this, &ImageMagickRunner::pumpQueue);
}

void ImageMagickRunner::updateProgress()
{
    if (!m_progress || m_finished)
        return;

    m_progress->setLabelText(
        tr("%1\nCompleted %2 of %3\nRunning jobs: %4")
            .arg(m_operationLabel)
            .arg(m_completed)
            .arg(m_jobs.size())
            .arg(m_activeProcesses.size()));
    m_progress->setValue(m_completed);
}

void ImageMagickRunner::cancel()
{
    if (m_finished || m_canceled)
        return;

    m_canceled = true;

    if (m_progress)
        m_progress->setLabelText(tr("Canceling running jobs..."));

    const auto processes = m_activeProcesses.keys();
    for (QProcess *process : processes) {
        if (!process || process->state() == QProcess::NotRunning)
            continue;

        process->terminate();
        const QPointer<QProcess> guardedProcess(process);
        QTimer::singleShot(1500, this, [guardedProcess]() {
            if (guardedProcess && guardedProcess->state() != QProcess::NotRunning)
                guardedProcess->kill();
        });
    }

    if (m_activeProcesses.isEmpty())
        finishBatch();
}

void ImageMagickRunner::recordFailure(const QString &input, const QString &message)
{
    ++m_failed;
    m_errors << tr("%1: %2").arg(QFileInfo(input).fileName(), message);
}

void ImageMagickRunner::cleanupTemporaryOutput(const QString &path)
{
    if (!path.isEmpty())
        QFile::remove(path);
}

void ImageMagickRunner::cleanupAllTemporaryOutputs()
{
    for (const ImageMagickJob &job : std::as_const(m_jobs))
        cleanupTemporaryOutput(job.temporaryOutput);
}

void ImageMagickRunner::finishBatch()
{
    if (m_finished)
        return;

    // Normal completion requires every active process to have reported its final
    // state. Cancellation follows the same rule so the final report cannot race
    // with a late worker callback.
    if (!m_activeProcesses.isEmpty())
        return;

    m_finished = true;
    cleanupAllTemporaryOutputs();

    if (m_progress) {
        m_progress->setValue(m_completed);
        m_progress->close();
        m_progress->deleteLater();
        m_progress = nullptr;
    }

    emit finished(m_succeeded, m_failed, m_canceled, m_errors, m_warnings);
}
