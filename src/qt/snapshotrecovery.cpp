// Copyright (c) 2024 The Agouti developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "snapshotrecovery.h"

#include "crypto/sha256.h"
#include "util.h"

#include <QByteArray>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QtGlobal>

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace fs = boost::filesystem;

static const char* SNAPSHOT_URL = "https://agouti.io/agoutisnapshot.zip";
static const qint64 MIN_SNAPSHOT_BYTES = 1900LL * 1024 * 1024;
static const qint64 MAX_DOWNLOAD_BYTES = 8LL * 1024 * 1024 * 1024;
static const qint64 DOWNLOAD_STALL_MS = 60000;
static const int MAX_RETRIES = 1;
static const char* BLOCKCHAIN_DIRS[] = {"blocks", "chainstate", "sporks", NULL};
static const char* BLOCKCHAIN_DELETE_DIRS[] = {"database", NULL};
static const char* OPTIONAL_BLOCKCHAIN_DIRS[] = {"zerocoin", NULL};

static const unsigned char SNAPSHOT_SHA256[CSHA256::OUTPUT_SIZE] = {
    0x4a, 0x9e, 0x0b, 0x61, 0xec, 0xd9, 0x1c, 0xa6,
    0x32, 0x09, 0xa4, 0x3d, 0x89, 0x48, 0xe4, 0x94,
    0x77, 0xb2, 0x32, 0xd4, 0xc0, 0xb7, 0x7c, 0xb3,
    0x0c, 0xd7, 0xf5, 0xfb, 0xdf, 0x67, 0x67, 0x97
};

static QString SRTr(const char* sourceText)
{
    return QCoreApplication::translate("SnapshotRecoveryDialog", sourceText);
}

static QEvent::Type SnapshotRecoveryEventType()
{
    static int type = QEvent::registerEventType();
    return static_cast<QEvent::Type>(type);
}

class SnapshotRecoveryEvent : public QEvent
{
public:
    enum Kind {
        Status,
        Progress,
        CancelLocked,
        Finished
    };

    SnapshotRecoveryEvent(Kind kind, const QString& message, int progress, bool success, bool cancelled)
        : QEvent(SnapshotRecoveryEventType()),
          kind(kind),
          message(message),
          progress(progress),
          success(success),
          cancelled(cancelled)
    {
    }

    Kind kind;
    QString message;
    int progress;
    bool success;
    bool cancelled;
};

static quint16 ReadLE16(const unsigned char* p)
{
    return static_cast<quint16>(p[0]) | (static_cast<quint16>(p[1]) << 8);
}

static quint32 ReadLE32(const unsigned char* p)
{
    return static_cast<quint32>(p[0]) |
           (static_cast<quint32>(p[1]) << 8) |
           (static_cast<quint32>(p[2]) << 16) |
           (static_cast<quint32>(p[3]) << 24);
}

static unsigned char LowerByte(unsigned char c)
{
    return static_cast<unsigned char>(std::tolower(c));
}

static std::string PathForCompare(const fs::path& path)
{
    std::string out = path.generic_string();
    while (out.size() > 1 && out[out.size() - 1] == '/')
        out.erase(out.size() - 1);
#if defined(WIN32)
    std::transform(out.begin(), out.end(), out.begin(), LowerByte);
#endif
    return out;
}

static bool IsPathInsideOrEqual(const fs::path& base, const fs::path& candidate)
{
    std::string baseStr = PathForCompare(base);
    std::string candidateStr = PathForCompare(candidate);
    if (candidateStr == baseStr)
        return true;
    if (baseStr.empty())
        return false;
    baseStr += "/";
    return candidateStr.compare(0, baseStr.size(), baseStr) == 0;
}

static bool CanonicalDirectory(const fs::path& path, fs::path& canonical, QString& error)
{
    boost::system::error_code ec;
    canonical = fs::canonical(path, ec);
    if (ec) {
        error = SRTr("Cannot resolve data directory: %1").arg(QString::fromStdString(ec.message()));
        return false;
    }
    if (!fs::is_directory(canonical, ec) || ec) {
        error = SRTr("Data directory is not a directory.");
        return false;
    }
    return true;
}

static bool IsAllowedTopLevel(const QByteArray& top)
{
    for (int i = 0; BLOCKCHAIN_DIRS[i] != NULL; ++i) {
        if (top == BLOCKCHAIN_DIRS[i])
            return true;
    }
    for (int i = 0; BLOCKCHAIN_DELETE_DIRS[i] != NULL; ++i) {
        if (top == BLOCKCHAIN_DELETE_DIRS[i])
            return true;
    }
    for (int i = 0; OPTIONAL_BLOCKCHAIN_DIRS[i] != NULL; ++i) {
        if (top == OPTIONAL_BLOCKCHAIN_DIRS[i])
            return true;
    }
    return false;
}

static bool ValidateArchiveEntryName(const QByteArray& name, QString& error)
{
    if (name.isEmpty()) {
        error = SRTr("Snapshot archive contains an empty path.");
        return false;
    }
    if (name.indexOf('\0') >= 0 || name.indexOf('\r') >= 0 || name.indexOf('\n') >= 0) {
        error = SRTr("Snapshot archive contains an invalid path.");
        return false;
    }
    if (name.startsWith("/") || name.startsWith("\\") || name.indexOf('\\') >= 0 || name.indexOf(':') >= 0) {
        error = SRTr("Snapshot archive contains an absolute or platform-specific path: %1")
                    .arg(QString::fromLocal8Bit(name));
        return false;
    }

    const bool trailingDirectory = name.endsWith("/");
    QList<QByteArray> parts = name.split('/');
    QList<QByteArray> cleanParts;
    for (int i = 0; i < parts.size(); ++i) {
        if (parts[i].isEmpty()) {
            if (trailingDirectory && i == parts.size() - 1)
                continue;
            error = SRTr("Snapshot archive contains an invalid path segment: %1")
                        .arg(QString::fromLocal8Bit(name));
            return false;
        }
        if (parts[i] == "." || parts[i] == "..") {
            error = SRTr("Snapshot archive contains path traversal: %1").arg(QString::fromLocal8Bit(name));
            return false;
        }
        cleanParts.push_back(parts[i]);
    }

    if (cleanParts.isEmpty() || !IsAllowedTopLevel(cleanParts[0])) {
        error = SRTr("Snapshot archive contains an unexpected top-level path: %1")
                    .arg(QString::fromLocal8Bit(name));
        return false;
    }
    if (cleanParts.size() == 1 && !trailingDirectory) {
        error = SRTr("Snapshot archive replaces a blockchain directory with a file: %1")
                    .arg(QString::fromLocal8Bit(name));
        return false;
    }
    return true;
}

static bool ValidateZipFileTypes(quint16 versionMadeBy, quint32 externalAttributes, const QByteArray& name, QString& error)
{
    const int hostSystem = versionMadeBy >> 8;
    if (hostSystem != 3 && hostSystem != 19)
        return true;

    const quint32 mode = externalAttributes >> 16;
    const quint32 type = mode & 0170000;
    if (type == 0)
        return true;
    if (type == 0120000) {
        error = SRTr("Snapshot archive contains a symlink: %1").arg(QString::fromLocal8Bit(name));
        return false;
    }
    if (type != 0100000 && type != 0040000) {
        error = SRTr("Snapshot archive contains a special filesystem entry: %1")
                    .arg(QString::fromLocal8Bit(name));
        return false;
    }
    return true;
}

static bool ValidateZipArchive(const QString& zipPath, const fs::path& extractionRoot, QString& error)
{
    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = SRTr("Cannot open snapshot archive for validation.");
        return false;
    }

    const qint64 fileSize = file.size();
    if (fileSize < 22) {
        error = SRTr("Snapshot archive is too small.");
        return false;
    }

    const qint64 tailSize = std::min<qint64>(fileSize, 65557);
    if (!file.seek(fileSize - tailSize)) {
        error = SRTr("Cannot read snapshot archive directory.");
        return false;
    }
    QByteArray tail = file.read(tailSize);
    if (tail.size() != tailSize) {
        error = SRTr("Cannot read snapshot archive directory.");
        return false;
    }

    int eocd = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        if (ReadLE32(reinterpret_cast<const unsigned char*>(tail.constData() + i)) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        error = SRTr("Snapshot archive central directory not found.");
        return false;
    }

    const unsigned char* eocdPtr = reinterpret_cast<const unsigned char*>(tail.constData() + eocd);
    const quint16 diskNumber = ReadLE16(eocdPtr + 4);
    const quint16 centralDisk = ReadLE16(eocdPtr + 6);
    const quint16 diskEntries = ReadLE16(eocdPtr + 8);
    const quint16 totalEntries = ReadLE16(eocdPtr + 10);
    const quint32 centralSize = ReadLE32(eocdPtr + 12);
    const quint32 centralOffset = ReadLE32(eocdPtr + 16);
    const quint16 zipCommentLength = ReadLE16(eocdPtr + 20);

    if (eocd + 22 + zipCommentLength != tail.size()) {
        error = SRTr("Snapshot archive has trailing data after the central directory.");
        return false;
    }
    if (diskNumber != 0 || centralDisk != 0 || diskEntries != totalEntries) {
        error = SRTr("Multi-disk snapshot archives are not supported.");
        return false;
    }
    if (totalEntries == 0 || totalEntries == 0xffff || centralSize == 0xffffffff || centralOffset == 0xffffffff) {
        error = SRTr("Unsupported snapshot archive format.");
        return false;
    }
    if (static_cast<quint64>(centralOffset) + centralSize > static_cast<quint64>(fileSize)) {
        error = SRTr("Snapshot archive central directory is outside the file.");
        return false;
    }

    if (!file.seek(centralOffset)) {
        error = SRTr("Cannot seek to snapshot archive central directory.");
        return false;
    }

    boost::system::error_code ec;
    const fs::path canonicalRoot = fs::canonical(extractionRoot, ec);
    if (ec) {
        error = SRTr("Cannot resolve snapshot staging directory: %1").arg(QString::fromStdString(ec.message()));
        return false;
    }

    for (quint16 entry = 0; entry < totalEntries; ++entry) {
        QByteArray fixed = file.read(46);
        if (fixed.size() != 46 ||
            ReadLE32(reinterpret_cast<const unsigned char*>(fixed.constData())) != 0x02014b50) {
            error = SRTr("Snapshot archive central directory is corrupt.");
            return false;
        }

        const unsigned char* ptr = reinterpret_cast<const unsigned char*>(fixed.constData());
        const quint16 versionMadeBy = ReadLE16(ptr + 4);
        const quint16 flags = ReadLE16(ptr + 8);
        const quint16 nameLength = ReadLE16(ptr + 28);
        const quint16 extraLength = ReadLE16(ptr + 30);
        const quint16 entryCommentLength = ReadLE16(ptr + 32);
        const quint32 externalAttributes = ReadLE32(ptr + 38);

        QByteArray name = file.read(nameLength);
        if (name.size() != nameLength) {
            error = SRTr("Snapshot archive entry name is truncated.");
            return false;
        }
        if ((flags & 1) != 0) {
            error = SRTr("Encrypted snapshot archive entries are not supported.");
            return false;
        }
        if (!ValidateArchiveEntryName(name, error) || !ValidateZipFileTypes(versionMadeBy, externalAttributes, name, error))
            return false;

        fs::path destination = canonicalRoot;
        QList<QByteArray> parts = name.split('/');
        for (int i = 0; i < parts.size(); ++i) {
            if (!parts[i].isEmpty())
                destination /= parts[i].constData();
        }
        fs::path absoluteDestination = fs::absolute(destination, canonicalRoot);
        if (!IsPathInsideOrEqual(canonicalRoot, absoluteDestination)) {
            error = SRTr("Snapshot archive entry escapes the staging directory: %1")
                        .arg(QString::fromLocal8Bit(name));
            return false;
        }

        if (!file.seek(file.pos() + extraLength + entryCommentLength)) {
            error = SRTr("Snapshot archive central directory is truncated.");
            return false;
        }
    }

    return true;
}

static bool ValidateExistingPathInside(const fs::path& base, const fs::path& path, bool missingOk, QString& error)
{
    boost::system::error_code ec;
    fs::file_status st = fs::symlink_status(path, ec);
    if (st.type() == fs::file_not_found ||
        (ec && ec == boost::system::errc::no_such_file_or_directory)) {
        if (missingOk)
            return true;
        error = SRTr("Required path is missing: %1").arg(QString::fromStdString(path.string()));
        return false;
    }
    if (ec) {
        error = SRTr("Cannot inspect %1: %2").arg(QString::fromStdString(path.string()),
                                                  QString::fromStdString(ec.message()));
        return false;
    }
    if (!fs::exists(st))
        return missingOk;
    if (fs::is_symlink(st)) {
        error = SRTr("Refusing to operate on symlink: %1").arg(QString::fromStdString(path.string()));
        return false;
    }

    fs::path canonical = fs::canonical(path, ec);
    if (ec) {
        error = SRTr("Cannot resolve %1: %2").arg(QString::fromStdString(path.string()),
                                                  QString::fromStdString(ec.message()));
        return false;
    }
    if (!IsPathInsideOrEqual(base, canonical)) {
        error = SRTr("Path escapes the data directory: %1").arg(QString::fromStdString(path.string()));
        return false;
    }
    return true;
}

static bool ValidateTreeInside(const fs::path& base, const fs::path& path, bool missingOk, QString& error)
{
    if (!ValidateExistingPathInside(base, path, missingOk, error))
        return false;

    boost::system::error_code ec;
    fs::file_status st = fs::symlink_status(path, ec);
    if (ec || !fs::exists(st))
        return missingOk;
    if (!fs::is_directory(st))
        return true;

    fs::directory_iterator it(path, ec);
    fs::directory_iterator end;
    if (ec) {
        error = SRTr("Cannot read directory %1: %2").arg(QString::fromStdString(path.string()),
                                                        QString::fromStdString(ec.message()));
        return false;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) {
            error = SRTr("Cannot iterate directory %1: %2").arg(QString::fromStdString(path.string()),
                                                               QString::fromStdString(ec.message()));
            return false;
        }
        if (!ValidateTreeInside(base, it->path(), false, error))
            return false;
    }
    return true;
}

static bool SafeRemovePath(const fs::path& base, const fs::path& path, bool missingOk, QString& error)
{
    if (!ValidateExistingPathInside(base, path, missingOk, error))
        return false;

    boost::system::error_code ec;
    fs::file_status st = fs::symlink_status(path, ec);
    if (ec || !fs::exists(st))
        return missingOk;

    if (fs::is_directory(st)) {
        fs::directory_iterator it(path, ec);
        fs::directory_iterator end;
        if (ec) {
            error = SRTr("Cannot read directory %1: %2").arg(QString::fromStdString(path.string()),
                                                            QString::fromStdString(ec.message()));
            return false;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) {
                error = SRTr("Cannot iterate directory %1: %2").arg(QString::fromStdString(path.string()),
                                                                   QString::fromStdString(ec.message()));
                return false;
            }
            if (!SafeRemovePath(base, it->path(), false, error))
                return false;
        }
    }

    if (!ValidateExistingPathInside(base, path, false, error))
        return false;
    fs::remove(path, ec);
    if (ec) {
        error = SRTr("Cannot remove %1: %2").arg(QString::fromStdString(path.string()),
                                                QString::fromStdString(ec.message()));
        return false;
    }
    return true;
}

static bool DeleteBlockchainDirs(const fs::path& canonicalDataDir, QString& error)
{
    for (int i = 0; BLOCKCHAIN_DIRS[i] != NULL; ++i) {
        if (!ValidateTreeInside(canonicalDataDir, canonicalDataDir / BLOCKCHAIN_DIRS[i], true, error))
            return false;
    }
    for (int i = 0; BLOCKCHAIN_DELETE_DIRS[i] != NULL; ++i) {
        if (!ValidateTreeInside(canonicalDataDir, canonicalDataDir / BLOCKCHAIN_DELETE_DIRS[i], true, error))
            return false;
    }
    for (int i = 0; BLOCKCHAIN_DIRS[i] != NULL; ++i) {
        if (!SafeRemovePath(canonicalDataDir, canonicalDataDir / BLOCKCHAIN_DIRS[i], true, error))
            return false;
    }
    for (int i = 0; BLOCKCHAIN_DELETE_DIRS[i] != NULL; ++i) {
        if (!SafeRemovePath(canonicalDataDir, canonicalDataDir / BLOCKCHAIN_DELETE_DIRS[i], true, error))
            return false;
    }
    return true;
}

static bool FindUnzip(QString& unzipBin, QString& error)
{
    QStringList candidates;
    candidates << "/usr/bin/unzip" << "/usr/local/bin/unzip";
    for (int i = 0; i < candidates.size(); ++i) {
        QFileInfo fi(candidates[i]);
        if (fi.isFile() && fi.isExecutable()) {
            unzipBin = candidates[i];
            return true;
        }
    }
    unzipBin = QString("unzip");
    QProcess proc;
    proc.start(unzipBin, QStringList() << "-v");
    if (!proc.waitForStarted(5000) || !proc.waitForFinished(5000)) {
        error = SRTr("Cannot find unzip executable.");
        return false;
    }
    return true;
}

class SnapshotRecoveryThread : public QThread
{
public:
    SnapshotRecoveryThread(const fs::path& dataDir, QAtomicInt* cancelFlag, QObject* receiver)
        : QThread(0),
          m_dataDir(dataDir),
          m_cancelFlag(cancelFlag),
          m_receiver(receiver)
    {
    }

    bool isCancelRequested() const
    {
        return m_cancelFlag && m_cancelFlag->loadAcquire() != 0;
    }

protected:
    void run()
    {
        QString error;
        if (isCancelRequested()) {
            finishCancelled();
            return;
        }
        fs::path canonicalDataDir;
        if (!CanonicalDirectory(m_dataDir, canonicalDataDir, error)) {
            finish(false, error);
            return;
        }

        const QString persistentZipPath = QString::fromStdString((canonicalDataDir / "agoutisnapshot.zip").string());
        QString zipPath;
        bool reusedExisting = false;

        // Always work from a temporary copy; persistent zip is input cache only (TOCTOU guard).
        // Use the data dir so the copy stays on the same partition (avoids /tmp space issues).
        QTemporaryDir workDir(QString::fromStdString((canonicalDataDir / ".snapshot-work-XXXXXX").string()));
        if (!workDir.isValid()) {
            finish(false, SRTr("Cannot create secure temporary download directory."));
            return;
        }

        if (QFileInfo(persistentZipPath).isFile()) {
            const QString tempCopyPath = workDir.path() + QLatin1String("/snapshot-copy.zip");
            postStatus(SRTr("Copying cached snapshot..."));
            if (QFile::copy(persistentZipPath, tempCopyPath)) {
                postStatus(SRTr("Verifying cached snapshot..."));
                postProgress(40);
                QString verifyError;
                if (verifyDownload(tempCopyPath, verifyError)) {
                    zipPath = tempCopyPath;
                    reusedExisting = true;
                } else {
                    LogPrintf("SnapshotRecovery: cached snapshot rejected: %s\n", verifyError.toStdString());
                    QFile::remove(tempCopyPath);
                    QFile::remove(persistentZipPath);
                }
            } else {
                LogPrintf("SnapshotRecovery: cannot copy cached snapshot, re-downloading\n");
                QFile::remove(persistentZipPath);
            }
        }

        if (!reusedExisting) {
            if (isCancelRequested()) {
                finishCancelled();
                return;
            }
            if (!downloadSnapshot(workDir.path(), zipPath, error)) {
                if (isCancelRequested())
                    finishCancelled();
                else
                    finish(false, error);
                return;
            }
            postStatus(SRTr("Verifying checksum..."));
            postProgress(80);
            if (!verifyDownload(zipPath, error)) {
                QFile::remove(zipPath);
                finish(false, error);
                return;
            }
        }

        // Beyond this point operations are filesystem-mutating and not safely cancellable.
        postCancelLocked();

        QTemporaryDir stagingDir(QString::fromStdString((canonicalDataDir / ".snapshot-recovery-XXXXXX").string()));
        if (!stagingDir.isValid()) {
            QFile::remove(zipPath);
            finish(false, SRTr("Cannot create secure snapshot staging directory."));
            return;
        }

        fs::path canonicalStagingDir;
        if (!CanonicalDirectory(fs::path(stagingDir.path().toStdString()), canonicalStagingDir, error) ||
            !IsPathInsideOrEqual(canonicalDataDir, canonicalStagingDir)) {
            QFile::remove(zipPath);
            finish(false, SRTr("Snapshot staging directory escapes the data directory."));
            return;
        }

        postStatus(SRTr("Validating snapshot archive..."));
        postProgress(84);
        if (!ValidateZipArchive(zipPath, canonicalStagingDir, error)) {
            QFile::remove(zipPath);
            finish(false, error);
            return;
        }

        postStatus(SRTr("Extracting snapshot..."));
        postProgress(88);
        if (!extractSnapshot(zipPath, QString::fromStdString(canonicalStagingDir.string()), error)) {
            QFile::remove(zipPath);
            finish(false, error);
            return;
        }

        if (!validateExtractedSnapshot(canonicalStagingDir, error)) {
            QFile::remove(zipPath);
            finish(false, error);
            return;
        }

        postStatus(SRTr("Removing existing blockchain directories..."));
        postProgress(94);
        if (!DeleteBlockchainDirs(canonicalDataDir, error)) {
            QFile::remove(zipPath);
            finish(false, error);
            return;
        }

        postStatus(SRTr("Installing snapshot..."));
        postProgress(97);
        if (!installSnapshot(canonicalDataDir, canonicalStagingDir, error)) {
            QString cleanupError;
            if (!DeleteBlockchainDirs(canonicalDataDir, cleanupError))
                LogPrintf("SnapshotRecovery: cleanup after failed install also failed: %s\n", cleanupError.toStdString());
            QFile::remove(zipPath);
            finish(false, error);
            return;
        }

        if (!reusedExisting) {
            QFile::remove(persistentZipPath);
            bool persisted = QFile::rename(zipPath, persistentZipPath);
            if (!persisted) {
                LogPrintf("SnapshotRecovery: cannot move snapshot to %s, copying instead\n",
                          persistentZipPath.toStdString());
                persisted = QFile::copy(zipPath, persistentZipPath);
                if (persisted)
                    QFile::remove(zipPath);
            }
            finish(true, persisted ? persistentZipPath : QString());
        } else {
            finish(true, persistentZipPath);
        }
    }

private:
    void postStatus(const QString& message)
    {
        QCoreApplication::postEvent(m_receiver, new SnapshotRecoveryEvent(SnapshotRecoveryEvent::Status, message, -1, false, false));
    }

    void postProgress(int progress)
    {
        QCoreApplication::postEvent(m_receiver, new SnapshotRecoveryEvent(SnapshotRecoveryEvent::Progress, QString(), progress, false, false));
    }

    void postCancelLocked()
    {
        QCoreApplication::postEvent(m_receiver, new SnapshotRecoveryEvent(SnapshotRecoveryEvent::CancelLocked, QString(), -1, false, false));
    }

    void finish(bool success, const QString& message)
    {
        QCoreApplication::postEvent(m_receiver, new SnapshotRecoveryEvent(SnapshotRecoveryEvent::Finished, message, success ? 100 : -1, success, false));
    }

    void finishCancelled()
    {
        QCoreApplication::postEvent(m_receiver, new SnapshotRecoveryEvent(SnapshotRecoveryEvent::Finished, QString(), -1, false, true));
    }

    bool downloadSnapshot(const QString& tempDir, QString& zipPath, QString& error)
    {
        for (int attempt = 0; attempt <= MAX_RETRIES; ++attempt) {
            if (isCancelRequested()) {
                error = SRTr("Cancelled.");
                return false;
            }
            QTemporaryFile file(tempDir + QLatin1String("/snapshot-XXXXXX.zip"));
            file.setAutoRemove(false);
            if (!file.open()) {
                error = SRTr("Cannot create secure temporary snapshot file.");
                return false;
            }
            zipPath = file.fileName();

            postStatus(attempt == 0 ? SRTr("Downloading snapshot...")
                                    : SRTr("Download failed, retrying..."));
            postProgress(0);

            QNetworkAccessManager nam;
            QNetworkRequest req(QUrl(QString::fromLatin1(SNAPSHOT_URL)));
#if QT_VERSION >= 0x050600
            req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
            QNetworkReply* reply = nam.get(req);
            qint64 written = 0;
            qint64 total = -1;
            qint64 stallBaseline = 0;
            QElapsedTimer stallTimer;
            stallTimer.start();

            while (!reply->isFinished()) {
                QEventLoop loop;
                QTimer timer;
                timer.setSingleShot(true);
                QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
                QObject::connect(reply, SIGNAL(readyRead()), &loop, SLOT(quit()));
                QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
                timer.start(250);
                loop.exec();

                QByteArray chunk = reply->readAll();
                if (!chunk.isEmpty()) {
                    if (file.write(chunk) != chunk.size()) {
                        error = SRTr("Cannot write snapshot file: %1").arg(file.errorString());
                        reply->abort();
                        delete reply;
                        file.close();
                        QFile::remove(zipPath);
                        return false;
                    }
                    written += chunk.size();
                }

                if (isCancelRequested()) {
                    error = SRTr("Cancelled.");
                    reply->abort();
                    delete reply;
                    file.close();
                    QFile::remove(zipPath);
                    return false;
                }

                if (written != stallBaseline) {
                    stallBaseline = written;
                    stallTimer.restart();
                } else if (stallTimer.elapsed() > DOWNLOAD_STALL_MS) {
                    error = SRTr("Download stalled (no data received for %1 seconds).")
                                .arg(DOWNLOAD_STALL_MS / 1000);
                    reply->abort();
                    delete reply;
                    file.close();
                    QFile::remove(zipPath);
                    return false;
                }

                if (written > MAX_DOWNLOAD_BYTES) {
                    error = SRTr("Download exceeded the maximum allowed size.");
                    reply->abort();
                    delete reply;
                    file.close();
                    QFile::remove(zipPath);
                    return false;
                }

                QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
                if (length.isValid()) {
                    total = length.toLongLong();
                    if (total > MAX_DOWNLOAD_BYTES) {
                        error = SRTr("Server reported snapshot size exceeds the maximum allowed.");
                        reply->abort();
                        delete reply;
                        file.close();
                        QFile::remove(zipPath);
                        return false;
                    }
                }
                if (total > 0) {
                    postProgress(static_cast<int>((written * 75LL) / total));
                    postStatus(SRTr("Downloading snapshot: %1 MB / %2 MB")
                                   .arg(written / (1024 * 1024))
                                   .arg(total / (1024 * 1024)));
                } else if (written > 0) {
                    postStatus(SRTr("Downloading snapshot: %1 MB").arg(written / (1024 * 1024)));
                }
            }

            QByteArray finalChunk = reply->readAll();
            if (!finalChunk.isEmpty()) {
                if (file.write(finalChunk) != finalChunk.size()) {
                    error = SRTr("Cannot write snapshot file: %1").arg(file.errorString());
                    delete reply;
                    file.close();
                    QFile::remove(zipPath);
                    return false;
                }
                written += finalChunk.size();
            }
            file.close();

            const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const QNetworkReply::NetworkError networkError = reply->error();
            const QString networkErrorString = reply->errorString();
            delete reply;

            if (networkError == QNetworkReply::NoError &&
                (!statusCode.isValid() || statusCode.toInt() < 400))
                return true;

            QFile::remove(zipPath);
            error = SRTr("Download failed: %1").arg(networkErrorString);
        }
        return false;
    }

    bool verifyDownload(const QString& zipPath, QString& error)
    {
        QFileInfo fi(zipPath);
        if (!fi.exists() || fi.size() <= MIN_SNAPSHOT_BYTES) {
            error = SRTr("Snapshot verification failed: unexpected size.");
            return false;
        }

        QFile f(zipPath);
        if (!f.open(QIODevice::ReadOnly)) {
            error = SRTr("Cannot open snapshot file for verification.");
            return false;
        }

        CSHA256 ctx;
        char buf[65536];
        qint64 n;
        while ((n = f.read(buf, sizeof(buf))) > 0)
            ctx.Write(reinterpret_cast<const unsigned char*>(buf), static_cast<size_t>(n));
        if (f.error() != QFile::NoError) {
            error = SRTr("Cannot read snapshot file for verification: %1").arg(f.errorString());
            return false;
        }

        unsigned char digest[CSHA256::OUTPUT_SIZE];
        ctx.Finalize(digest);
        if (std::memcmp(digest, SNAPSHOT_SHA256, CSHA256::OUTPUT_SIZE) != 0) {
            LogPrintf("SnapshotRecovery: SHA256 mismatch\n");
            error = SRTr("Snapshot verification failed: checksum mismatch.");
            return false;
        }
        return true;
    }

    bool extractSnapshot(const QString& zipPath, const QString& stagingPath, QString& error)
    {
        QString unzipBin;
        if (!FindUnzip(unzipBin, error))
            return false;

        QProcess proc;
        proc.start(unzipBin, QStringList() << "-q" << zipPath << "-d" << stagingPath);
        if (!proc.waitForStarted(30000)) {
            error = SRTr("Cannot start unzip.");
            return false;
        }
        if (!proc.waitForFinished(600000)) {
            proc.kill();
            proc.waitForFinished(30000);
            error = SRTr("Snapshot extraction timed out.");
            LogPrintf("SnapshotRecovery: unzip timed out\n");
            return false;
        }
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            const QString stderrText = QString::fromLocal8Bit(proc.readAllStandardError());
            error = SRTr("Snapshot extraction failed: %1").arg(stderrText);
            LogPrintf("SnapshotRecovery: unzip exit %d stderr: %s\n", proc.exitCode(), stderrText.toStdString());
            return false;
        }
        return true;
    }

    bool validateExtractedSnapshot(const fs::path& canonicalStagingDir, QString& error)
    {
        if (!ValidateTreeInside(canonicalStagingDir, canonicalStagingDir, false, error))
            return false;

        for (int i = 0; BLOCKCHAIN_DIRS[i] != NULL; ++i) {
            fs::path dir = canonicalStagingDir / BLOCKCHAIN_DIRS[i];
            if (!ValidateExistingPathInside(canonicalStagingDir, dir, false, error))
                return false;
            boost::system::error_code ec;
            if (!fs::is_directory(dir, ec) || ec) {
                error = SRTr("Snapshot is missing required directory: %1").arg(BLOCKCHAIN_DIRS[i]);
                return false;
            }
        }
        return true;
    }

    bool installSnapshot(const fs::path& canonicalDataDir, const fs::path& canonicalStagingDir, QString& error)
    {
        for (int i = 0; BLOCKCHAIN_DIRS[i] != NULL; ++i) {
            fs::path source = canonicalStagingDir / BLOCKCHAIN_DIRS[i];
            fs::path destination = canonicalDataDir / BLOCKCHAIN_DIRS[i];
            if (!ValidateTreeInside(canonicalStagingDir, source, false, error))
                return false;
            boost::system::error_code ec;
            if (fs::exists(destination, ec)) {
                error = SRTr("Destination already exists during snapshot install: %1")
                            .arg(QString::fromStdString(destination.string()));
                return false;
            }
            fs::rename(source, destination, ec);
            if (ec) {
                error = SRTr("Cannot install snapshot directory %1: %2")
                            .arg(BLOCKCHAIN_DIRS[i], QString::fromStdString(ec.message()));
                return false;
            }
            if (!ValidateTreeInside(canonicalDataDir, destination, false, error))
                return false;
        }
        return true;
    }

    fs::path m_dataDir;
    QAtomicInt* m_cancelFlag;
    QObject* m_receiver;
};

SnapshotRecoveryDialog::SnapshotRecoveryDialog(const boost::filesystem::path& dataDir, QWidget* parent)
    : QDialog(parent, Qt::Dialog),
      m_dataDir(dataDir),
      m_statusLabel(0),
      m_progressBar(0),
      m_cancelButton(0),
      m_worker(0),
      m_cancelRequested(0),
      m_success(false),
      m_cancelled(false)
{
    setWindowTitle(SRTr("Blockchain Recovery"));
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    setMinimumWidth(440);
    setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(this);

    m_statusLabel = new QLabel(SRTr("Blockchain data incomplete. Downloading snapshot..."), this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);

    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    m_cancelButton = new QPushButton(SRTr("Cancel"), this);
    m_cancelButton->setAutoDefault(false);
    connect(m_cancelButton, SIGNAL(clicked()), this, SLOT(reject()));
    buttonRow->addWidget(m_cancelButton);
    layout->addLayout(buttonRow);

    m_worker = new SnapshotRecoveryThread(m_dataDir, &m_cancelRequested, this);
    m_worker->start();
}

SnapshotRecoveryDialog::~SnapshotRecoveryDialog()
{
    if (m_worker) {
        m_cancelRequested.storeRelease(1);
        m_worker->wait();
        delete m_worker;
        m_worker = 0;
    }
}

void SnapshotRecoveryDialog::closeEvent(QCloseEvent* event)
{
    if (m_worker) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void SnapshotRecoveryDialog::keyPressEvent(QKeyEvent* event)
{
    if (event && event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}

void SnapshotRecoveryDialog::reject()
{
    if (!m_worker) {
        QDialog::reject();
        return;
    }
    if (!m_cancelButton || !m_cancelButton->isEnabled())
        return;
    m_cancelRequested.storeRelease(1);
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText(SRTr("Cancelling..."));
    m_statusLabel->setText(SRTr("Cancelling, please wait..."));
}

bool SnapshotRecoveryDialog::event(QEvent* event)
{
    if (event->type() != SnapshotRecoveryEventType())
        return QDialog::event(event);

    SnapshotRecoveryEvent* recoveryEvent = static_cast<SnapshotRecoveryEvent*>(event);
    if (recoveryEvent->kind == SnapshotRecoveryEvent::Status) {
        if (!m_cancelRequested.loadAcquire())
            m_statusLabel->setText(recoveryEvent->message);
    } else if (recoveryEvent->kind == SnapshotRecoveryEvent::Progress) {
        if (recoveryEvent->progress >= 0)
            m_progressBar->setValue(recoveryEvent->progress);
    } else if (recoveryEvent->kind == SnapshotRecoveryEvent::CancelLocked) {
        if (m_cancelButton) {
            m_cancelButton->setEnabled(false);
            m_cancelButton->hide();
        }
    } else if (recoveryEvent->kind == SnapshotRecoveryEvent::Finished) {
        m_success = recoveryEvent->success;
        m_cancelled = recoveryEvent->cancelled;
        QString persistentZipPath;
        if (m_success) {
            persistentZipPath = recoveryEvent->message;
            m_failureReason.clear();
            m_progressBar->setValue(100);
            m_statusLabel->setText(SRTr("Snapshot extracted. Starting node..."));
        } else if (m_cancelled) {
            m_failureReason.clear();
            m_statusLabel->setText(SRTr("Snapshot download cancelled."));
        } else {
            m_failureReason = recoveryEvent->message;
            m_statusLabel->setText(SRTr("Snapshot recovery failed: %1").arg(m_failureReason));
        }

        if (m_worker) {
            m_worker->wait();
            delete m_worker;
            m_worker = 0;
        }

        if (m_success && !persistentZipPath.isEmpty() && QFileInfo(persistentZipPath).isFile()) {
            const qint64 sizeMB = QFileInfo(persistentZipPath).size() / (1024 * 1024);
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                SRTr("Keep snapshot?"),
                SRTr("Keep the downloaded snapshot file (%1 MB) at:\n\n%2\n\n"
                     "If kept, future recovery will reuse it after SHA256 verification "
                     "instead of downloading again.").arg(sizeMB).arg(persistentZipPath),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (reply == QMessageBox::No)
                QFile::remove(persistentZipPath);
        }

        accept();
    }
    return true;
}
