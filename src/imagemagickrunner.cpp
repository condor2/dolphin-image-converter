// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagemagickrunner.h"
#include "jpegorientation.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPointer>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QTemporaryDir>
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
    // QObject children are destroyed after this destructor body. Stop every
    // child image processor first so it cannot recreate or continue writing a
    // staging file after cleanup has already run.
    const auto processes = m_activeProcesses.keys();
    for (QProcess *process : processes) {
        if (!process)
            continue;
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(3000);
        }
    }
    m_activeProcesses.clear();
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

bool ImageMagickRunner::canWriteFormat(const QString &format)
{
    const QString program = executable();
    if (program.isEmpty())
        return false;

    QString coder = format.trimmed().toUpper();
    if (coder == QStringLiteral("JPG")
        || coder == QStringLiteral("JPE")
        || coder == QStringLiteral("JFIF")
        || coder == QStringLiteral("JIF"))
        coder = QStringLiteral("JPEG");
    else if (coder == QStringLiteral("TIF"))
        coder = QStringLiteral("TIFF");
    else if (coder == QStringLiteral("HEIF"))
        coder = QStringLiteral("HEIC");

    static QHash<QString, bool> cache;
    if (cache.contains(coder))
        return cache.value(coder);

    QString extension = coder.toLower();
    if (coder == QStringLiteral("JPEG"))
        extension = QStringLiteral("jpg");
    else if (coder == QStringLiteral("TIFF"))
        extension = QStringLiteral("tif");
    else if (coder == QStringLiteral("HEIC"))
        extension = QStringLiteral("heic");

    QTemporaryDir directory;
    if (!directory.isValid()) {
        cache.insert(coder, false);
        return false;
    }

    const QString output = directory.filePath(QStringLiteral("probe.") + extension);
    QProcess process;
    process.setProgram(program);
    process.setArguments({QStringLiteral("-size"),
                          QStringLiteral("16x16"),
                          QStringLiteral("xc:white"),
                          coder + QStringLiteral(":") + output});
    process.start();

    const bool started = process.waitForStarted(3000);
    const bool finished = started && process.waitForFinished(10000);
    const bool writable = finished
                       && process.exitStatus() == QProcess::NormalExit
                       && process.exitCode() == 0
                       && QFileInfo::exists(output);
    cache.insert(coder, writable);
    return writable;
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
    // Only files carrying this application's PID + UUID staging prefix are
    // eligible. The optional -N suffix covers ImageMagick sequence side files
    // from a worker that was interrupted before cleanup.
    static const QRegularExpression currentPattern(
        QStringLiteral(R"(^\.dolphin-image-converter-(\d+)-[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}(?:-\d+)?(?:\.[^/]+)?$)"));

    const QDateTime now = QDateTime::currentDateTimeUtc();
    constexpr qint64 currentMinAgeSeconds = 12 * 60 * 60;

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
            const qint64 ageSeconds = entry.lastModified().toUTC().secsTo(now);
            if (ageSeconds < currentMinAgeSeconds)
                continue;

            const QRegularExpressionMatch match = currentPattern.match(entry.fileName());
            if (!match.hasMatch())
                continue;

            const qint64 pid = match.captured(1).toLongLong();
            if (!processIdIsAlive(pid))
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
    m_activeMetadataJobs = 0;
    const int jobCount = int(m_jobs.size());
    m_maxWorkers = qMax(1, qMin(maxWorkers, qMax(1, jobCount)));
    m_memoryLimitMiB = parallelMemoryLimitMiB(m_maxWorkers);

    m_progress = new QProgressDialog(operationLabel, tr("Cancel"), 0, jobCount, parent);
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    connect(m_progress, &QProgressDialog::canceled, this, &ImageMagickRunner::cancel);
    m_progress->installEventFilter(this);

    QTimer::singleShot(0, this, &ImageMagickRunner::pumpQueue);
}

void ImageMagickRunner::pumpQueue()
{
    if (m_finished)
        return;

    if (m_canceled) {
        if (m_activeProcesses.isEmpty() && m_activeMetadataJobs == 0)
            finishBatch();
        return;
    }

    updateProgress();

    // QProgressDialog::setValue() may process events for modal dialogs. A Cancel
    // click can therefore re-enter cancel() while updateProgress() is running.
    if (m_finished || m_canceled || !m_progress)
        return;

    while (!m_canceled
           && (m_activeProcesses.size() + m_activeMetadataJobs) < m_maxWorkers
           && m_nextIndex < m_jobs.size()) {
        const int index = m_nextIndex++;
        startJob(index);

        if (m_finished || m_canceled)
            return;
    }

    updateProgress();

    if (m_finished || m_canceled || !m_progress)
        return;

    if (m_completed >= m_jobs.size()
        && m_activeProcesses.isEmpty()
        && m_activeMetadataJobs == 0)
        finishBatch();
}

void ImageMagickRunner::startJob(int index, bool fallback)
{
    if (m_finished || m_canceled || index < 0 || index >= m_jobs.size())
        return;

    const ImageMagickJob &job = m_jobs.at(index);
    if (!fallback && job.metadataOnly) {
        ++m_activeMetadataJobs;
        QTimer::singleShot(0, this, [this, index]() {
            runMetadataOnlyJob(index);
        });
        return;
    }

    auto *process = new QProcess(this);
    ActiveProcessInfo processInfo;
    processInfo.index = index;
    processInfo.fallback = fallback;
    m_activeProcesses.insert(process, processInfo);

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

    const QString program = fallback ? job.fallbackProgram
                                     : (job.program.isEmpty() ? executable() : job.program);
    const QStringList arguments = fallback ? job.fallbackArguments : job.arguments;

    process->setProcessEnvironment(environment);
    process->setProgram(program);
    process->setArguments(arguments);
    process->start();
}

void ImageMagickRunner::runMetadataOnlyJob(int index)
{
    if (m_activeMetadataJobs > 0)
        --m_activeMetadataJobs;

    if (m_finished || index < 0 || index >= m_jobs.size())
        return;

    const ImageMagickJob job = m_jobs.at(index);
    if (m_canceled) {
        cleanupTemporaryOutput(job.temporaryOutput);
        if (m_activeProcesses.isEmpty() && m_activeMetadataJobs == 0)
            finishBatch();
        return;
    }

    QFile source(job.input);
    if (!source.open(QIODevice::ReadOnly)) {
        recordFailure(job.input, tr("Could not read JPEG data."));
        ++m_completed;
    } else {
        QByteArray data = source.readAll();
        source.close();

        const int oldOrientation = rewriteJpegOrientation(data, job.metadataRotateClockwise);
        if (oldOrientation == 0) {
            cleanupTemporaryOutput(job.temporaryOutput);
            if (!job.fallbackProgram.isEmpty()) {
                startJob(index, true);
                QTimer::singleShot(0, this, &ImageMagickRunner::pumpQueue);
                return;
            }
            recordFailure(job.input, tr("JPEG EXIF Orientation could not be updated losslessly."));
            ++m_completed;
        } else {
            QFile output(job.temporaryOutput);
            const bool opened = output.open(QIODevice::WriteOnly | QIODevice::Truncate);
            const qint64 written = opened ? output.write(data) : -1;
            const bool flushed = opened && written == data.size() && output.flush();
            output.close();

            if (!flushed) {
                cleanupTemporaryOutput(job.temporaryOutput);
                recordFailure(job.input, tr("Could not write temporary JPEG data."));
                ++m_completed;
            } else {
                QString commitError;
                QStringList commitWarnings;
                if (commitTemporaryOutput(job, &commitError, &commitWarnings)) {
                    ++m_succeeded;
                    for (const QString &warning : std::as_const(commitWarnings))
                        m_warnings << tr("%1: %2").arg(QFileInfo(job.input).fileName(), warning);
                    if (!job.successWarning.isEmpty())
                        m_warnings << tr("%1: %2").arg(QFileInfo(job.input).fileName(), job.successWarning);
                } else {
                    cleanupTemporaryOutput(job.temporaryOutput);
                    recordFailure(job.input, commitError);
                }
                ++m_completed;
            }
        }
    }

    if (m_canceled) {
        if (m_activeProcesses.isEmpty() && m_activeMetadataJobs == 0)
            finishBatch();
        return;
    }

    updateProgress();
    if (m_finished || m_canceled || !m_progress)
        return;

    QTimer::singleShot(0, this, &ImageMagickRunner::pumpQueue);
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
            *errorMessage = tr("The image processor reported success but did not create the output file.");
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

    const ActiveProcessInfo processInfo = m_activeProcesses.take(process);
    const int index = processInfo.index;
    const ImageMagickJob job = m_jobs.value(index);

    if (m_canceled) {
        cleanupTemporaryOutput(job.temporaryOutput);
    } else if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        QString commitError;
        QStringList commitWarnings;
        if (commitTemporaryOutput(job, &commitError, &commitWarnings)) {
            ++m_succeeded;
            for (const QString &warning : std::as_const(commitWarnings))
                m_warnings << tr("%1: %2").arg(QFileInfo(job.input).fileName(), warning);

            const QString jobWarning = processInfo.fallback ? job.fallbackWarning
                                                            : job.successWarning;
            if (!jobWarning.isEmpty())
                m_warnings << tr("%1: %2").arg(QFileInfo(job.input).fileName(), jobWarning);
        } else {
            cleanupTemporaryOutput(job.temporaryOutput);
            recordFailure(job.input, commitError);
        }
        ++m_completed;
    } else if (!processInfo.fallback && !job.fallbackProgram.isEmpty()) {
        // jpegtran -perfect intentionally fails for JPEG dimensions that cannot
        // be transformed exactly. Remove any partial staging file and retry the
        // same job with the lossless-safe fallback configured by the dialog.
        cleanupTemporaryOutput(job.temporaryOutput);
        process->deleteLater();
        startJob(index, true);
        return;
    } else {
        cleanupTemporaryOutput(job.temporaryOutput);
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        recordFailure(job.input, stderrText.isEmpty() ? tr("ImageMagick failed.") : stderrText);
        ++m_completed;
    }

    process->deleteLater();

    if (m_canceled) {
        if (m_activeProcesses.isEmpty() && m_activeMetadataJobs == 0)
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

    const ActiveProcessInfo processInfo = m_activeProcesses.take(process);
    const int index = processInfo.index;
    const ImageMagickJob job = m_jobs.value(index);
    cleanupTemporaryOutput(job.temporaryOutput);

    process->deleteLater();

    if (!m_canceled && !processInfo.fallback && !job.fallbackProgram.isEmpty()) {
        startJob(index, true);
        return;
    }

    if (!m_canceled) {
        recordFailure(job.input, tr("Could not start ImageMagick."));
        ++m_completed;
    }

    if (m_canceled) {
        if (m_activeProcesses.isEmpty() && m_activeMetadataJobs == 0)
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
            .arg(m_activeProcesses.size() + m_activeMetadataJobs));
    m_progress->setValue(m_completed);
}

bool ImageMagickRunner::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_progress && m_progress && !m_finished) {
        const bool escapePressed = event->type() == QEvent::KeyPress
                                && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape;
        if (event->type() == QEvent::Close || escapePressed) {
            event->ignore();
            cancel();
            return true;
        }
    }

    return QObject::eventFilter(watched, event);
}

void ImageMagickRunner::cancel()
{
    if (m_finished)
        return;

    if (m_canceled) {
        // A repeated close/cancel can make QProgressDialog hide itself again.
        // Keep the modal status window visible until the final worker exits.
        if (m_progress) {
            m_progress->setLabelText(tr("Canceling running jobs..."));
            m_progress->show();
            m_progress->raise();
        }
        return;
    }

    m_canceled = true;

    if (m_progress) {
        // QProgressDialog::cancel() normally hides itself via reset(). Keep it
        // visible and modal until every worker has stopped. Disable the cancel
        // button instead of deleting it while it may still be emitting clicked().
        m_progress->setLabelText(tr("Canceling running jobs..."));
        const auto buttons = m_progress->findChildren<QPushButton *>();
        for (QPushButton *button : buttons)
            button->setEnabled(false);
        m_progress->show();
        m_progress->raise();

        // Re-show on the next event-loop turn as well. This runs after the
        // internal QProgressDialog cancel/reset path has finished hiding it.
        QTimer::singleShot(0, this, [this]() {
            if (m_progress && m_canceled && !m_finished) {
                m_progress->setLabelText(tr("Canceling running jobs..."));
                m_progress->show();
                m_progress->raise();
            }
        });
    }

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

    if (m_activeProcesses.isEmpty() && m_activeMetadataJobs == 0)
        finishBatch();
}

void ImageMagickRunner::recordFailure(const QString &input, const QString &message)
{
    ++m_failed;
    m_errors << tr("%1: %2").arg(QFileInfo(input).fileName(), message);
}

void ImageMagickRunner::cleanupTemporaryOutput(const QString &path)
{
    if (path.isEmpty())
        return;

    QFile::remove(path);

    // ImageMagick may create sequence side files such as ...-0.avif when a
    // multi-frame input is sent to a coder that emits separate files. The
    // staging basename contains a UUID, so matching numeric siblings are safe
    // to remove without touching user files.
    const QFileInfo info(path);
    const QString fileName = info.fileName();
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? fileName.left(dot) : fileName;
    const QString extension = dot > 0 ? fileName.mid(dot) : QString();
    const QRegularExpression siblingPattern(
        QStringLiteral("^%1-\\d+%2$")
            .arg(QRegularExpression::escape(stem), QRegularExpression::escape(extension)));

    QDir directory = info.dir();
    const QFileInfoList entries = directory.entryInfoList(
        QStringList{stem + QStringLiteral("-*") + extension},
        QDir::Files | QDir::Hidden | QDir::NoSymLinks,
        QDir::NoSort);
    for (const QFileInfo &entry : entries) {
        if (siblingPattern.match(entry.fileName()).hasMatch())
            QFile::remove(entry.absoluteFilePath());
    }
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
    if (!m_activeProcesses.isEmpty() || m_activeMetadataJobs > 0)
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
