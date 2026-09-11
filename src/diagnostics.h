// Local, bounded diagnostics. GPL-3.0-or-later.
#ifndef HACKRFT2_DIAGNOSTICS_H
#define HACKRFT2_DIAGNOSTICS_H
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>
#include <cstdio>

#if __has_include("build_revision.h")
#include "build_revision.h"
#else
#define HACKRFT2_GIT_COMMIT "unrecorded-local-build"
#endif
namespace Diagnostics {
inline const char *version() { return "1.4.2-RC1"; }
inline QString sessionDir()
{
    static const QString path = [] {
        QDir root(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                  + QStringLiteral("/diagnostics"));
        if(!root.mkpath(QStringLiteral("."))) return QString();
        // Each run owns one directory. Never remove a directory from today:
        // another running instance may still be writing there.
        const auto entries = root.entryInfoList({QStringLiteral("session-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for(int i = 0; i < entries.size() - 9; ++i)
            if(entries[i].lastModified().date() < QDate::currentDate())
                QDir(entries[i].absoluteFilePath()).removeRecursively();
        const QString name = QStringLiteral("session-%1-%2")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
                 QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
        return root.mkpath(name) ? root.filePath(name) : QString();
    }();
    return path;
}
inline QString filePath(const QString &name)
{
    const QString dir = sessionDir();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(name);
}
inline void appendBounded(const QString &path, const QByteArray &line, qint64 limit = 4 * 1024 * 1024)
{
    if(path.isEmpty()) return;
    if(QFileInfo(path).size() + line.size() > limit) {
        QFile::remove(path + QStringLiteral(".1"));
        if(!QFile::rename(path, path + QStringLiteral(".1"))) return;
    }
    QFile file(path);
    if(file.open(QIODevice::WriteOnly | QIODevice::Append)) file.write(line.left(int(limit)));
}
inline void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    const QByteArray line = (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QStringLiteral(" [%1] ").arg(int(type)) + message.left(16384) + QLatin1Char('\n')).toUtf8();
    appendBounded(filePath(QStringLiteral("runtime.log")), line);
    if(message.startsWith(QStringLiteral("PERF-")) || message.startsWith(QStringLiteral("FEC-")))
        appendBounded(filePath(QStringLiteral("fec-performance.log")), line);
    if(type == QtFatalMsg || sessionDir().isEmpty()) std::fwrite(line.constData(), 1, size_t(line.size()), stderr);
}
inline QString environment()
{
    return QStringLiteral("Version: %1\nCommit: %8\nBuilt: %2 %3\nOS: %4\nArchitecture: %5\nQt: %6\nDiagnostic session: %7\n")
        .arg(QString::fromLatin1(version()), QStringLiteral(__DATE__), QStringLiteral(__TIME__),
             QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(), QString::fromLatin1(qVersion()), sessionDir(), QString::fromLatin1(HACKRFT2_GIT_COMMIT));
}
}
#endif
