#include "storage/StartupDataService.h"
#include "storage/DataPathSafety.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <memory>
#include <utility>

namespace {

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

QString migrationMarkerPath(const QString& dataDirectory)
{
    return QDir(dataDirectory).filePath(QStringLiteral("legacy_migration.json"));
}

bool readMigrationMarker(const QString& dataDirectory, bool* complete, QString* error)
{
    *complete = false;
    const QString path = migrationMarkerPath(dataDirectory);
    const QFileInfo info(path);
    if (DataPathSafety::hasLinkedComponent(dataDirectory, path) ||
        !DataPathSafety::contains(dataDirectory, path)) {
        return fail(error, QStringLiteral("旧数据迁移标记路径不安全：%1").arg(path));
    }
    if (!info.exists()) return true;
    if (!info.isFile())
        return fail(error, QStringLiteral("旧数据迁移标记不是普通文件：%1").arg(path));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096)
        return fail(error, QStringLiteral("无法读取旧数据迁移标记：%1").arg(path));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("旧数据迁移标记损坏，已停止迁移：%1").arg(path));

    const QJsonObject marker = document.object();
    const QJsonValue versionValue = marker.value(QStringLiteral("version"));
    if (!versionValue.isDouble())
        return fail(error, QStringLiteral("旧数据迁移标记版本无效：%1").arg(path));
    const double version = versionValue.toDouble();
    if (version > 1.0)
        return fail(error, QStringLiteral("旧数据迁移标记来自未来版本，已停止迁移：%1").arg(path));
    if (version != 1.0)
        return fail(error, QStringLiteral("旧数据迁移标记版本不受支持：%1").arg(path));
    if (!marker.value(QStringLiteral("complete")).isBool() ||
        !marker.value(QStringLiteral("complete")).toBool()) {
        return fail(error, QStringLiteral("旧数据迁移标记格式无效：%1").arg(path));
    }
    *complete = true;
    return true;
}

bool validateCurrentAccountFile(const QString& dataDirectory, QString* error)
{
    const QString users = QDir(dataDirectory).filePath(QStringLiteral("users.dat"));
    const QFileInfo info(users);
    if (DataPathSafety::hasLinkedComponent(dataDirectory, users) ||
        !DataPathSafety::contains(dataDirectory, users)) {
        return fail(error, QStringLiteral("用户数据路径包含链接或越界路径"));
    }
    if (info.exists() && !info.isFile())
        return fail(error, QStringLiteral("当前用户数据路径不是普通文件：%1").arg(users));
    return true;
}

bool isOperationalMigrationFile(const QString& relativePath)
{
    const QString normalized = QDir::fromNativeSeparators(relativePath);
    const QStringList parts = normalized.split('/', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part.compare(QStringLiteral("ForestRestoreSnapshots"), Qt::CaseInsensitive) == 0)
            return true;
    }

    const QString name = QFileInfo(relativePath).fileName();
    return name.endsWith(QStringLiteral(".lock"), Qt::CaseInsensitive) ||
        name.startsWith(QStringLiteral("app.log"), Qt::CaseInsensitive) ||
        name.endsWith(QStringLiteral(".log"), Qt::CaseInsensitive) ||
        name == QStringLiteral("restore_request.txt") ||
        name == QStringLiteral("restore_transaction.json") ||
        name == QStringLiteral("legacy_migration.json");
}

bool copyMissingFile(const QString& source, const QString& target, QString* error)
{
    if (QFileInfo(target).isFile() && !QFileInfo(target).isSymLink()) return true;
    const QString sourceRoot = QFileInfo(source).absoluteDir().rootPath();
    if (DataPathSafety::hasLinkedComponent(sourceRoot, source) ||
        QFileInfo::exists(target) || QFileInfo(target).isSymLink() || QFileInfo(source).isSymLink() ||
        !QDir().mkpath(QFileInfo(target).dir().absolutePath())) {
        if (error) *error = QStringLiteral("旧数据迁移路径不可写或包含链接：%1").arg(target);
        return false;
    }
    QFile input(source);
    QSaveFile output(target);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法迁移旧数据：%1 -> %2").arg(source, target);
        return false;
    }
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(1024 * 1024);
        if (chunk.isEmpty() || output.write(chunk) != chunk.size()) {
            if (error) *error = QStringLiteral("旧数据复制未完成，原文件已保留：%1").arg(source);
            return false;
        }
    }
    if (input.error() != QFileDevice::NoError || !output.commit()) {
        if (error) *error = QStringLiteral("无法提交迁移文件，原文件已保留：%1").arg(source);
        return false;
    }
    return true;
}

} // namespace

StartupDataService::StartupDataService(QString dataDirectory)
    : dataDirectory_(QDir(dataDirectory).absolutePath()), lock_(dataDirectory_)
{
}

bool StartupDataService::acquire(QString* error)
{
    if (targetLockAcquired_) return true;
    if (!QDir().mkpath(dataDirectory_))
        return fail(error, QStringLiteral("无法创建当前数据目录：%1").arg(dataDirectory_));
    if (DataPathSafety::hasLinkedComponent(dataDirectory_, dataDirectory_))
        return fail(error, QStringLiteral("当前数据目录包含链接，无法获取写入锁：%1").arg(dataDirectory_));
    if (!lock_.acquire(error)) return false;
    targetLockAcquired_ = true;
    return true;
}

bool StartupDataService::markMigrationComplete(QString* error)
{
    if (!acquire(error)) return false;
    bool complete = false;
    if (!readMigrationMarker(dataDirectory_, &complete, error)) return false;
    if (complete) return true;

    const QString path = migrationMarkerPath(dataDirectory_);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const QByteArray contents = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1}, {QStringLiteral("complete"), true}})
                                    .toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
        return fail(error, QStringLiteral("无法原子保存旧数据迁移标记：%1").arg(path));
    return true;
}

bool StartupDataService::prepare(const QString& legacyDirectory, const QStringList& legacyAccountFiles,
                                 QString* migratedFrom, QString* error)
{
    if (!acquire(error)) return false;
    bool migrationComplete = false;
    if (!readMigrationMarker(dataDirectory_, &migrationComplete, error)) return false;
    if (migrationComplete) return validateCurrentAccountFile(dataDirectory_, error);

    std::unique_ptr<ApplicationInstanceLock> sourceLock;
    if (!legacyDirectory.isEmpty() && QFileInfo::exists(legacyDirectory)) {
        if (!QFileInfo(legacyDirectory).isDir() ||
            DataPathSafety::hasLinkedComponent(legacyDirectory, legacyDirectory) ||
            DataPathSafety::overlaps(legacyDirectory, dataDirectory_)) {
            return fail(error, QStringLiteral("旧数据目录与当前目录重叠或无效，未执行迁移"));
        }
        sourceLock = std::make_unique<ApplicationInstanceLock>(legacyDirectory);
        QString sourceLockError;
        if (!sourceLock->acquire(&sourceLockError)) {
            return fail(error, QStringLiteral("无法获取旧数据目录锁，迁移已停止：%1")
                                   .arg(sourceLockError));
        }
        QDir source(legacyDirectory);
        QDirIterator files(legacyDirectory, QDir::Files, QDirIterator::Subdirectories);
        bool copiedAny = false;
        while (files.hasNext()) {
            const QString file = files.next();
            const QString relative = source.relativeFilePath(file);
            if (isOperationalMigrationFile(relative)) continue;
            const QString target = QDir(dataDirectory_).filePath(relative);
            if (DataPathSafety::hasLinkedComponent(legacyDirectory, file) ||
                !DataPathSafety::contains(legacyDirectory, file) ||
                DataPathSafety::hasLinkedComponent(dataDirectory_, target) ||
                !DataPathSafety::contains(dataDirectory_, target)) {
                if (error) *error = QStringLiteral("旧数据路径包含越界链接：%1").arg(file);
                return false;
            }
            const bool existed = QFileInfo::exists(target);
            if (!copyMissingFile(file, target, error)) return false;
            copiedAny |= !existed;
        }
        if (copiedAny && migratedFrom) *migratedFrom = source.absolutePath();
    }

    if (!validateCurrentAccountFile(dataDirectory_, error)) return false;
    const QString users = QDir(dataDirectory_).filePath(QStringLiteral("users.dat"));
    if (QFileInfo::exists(users)) {
        return markMigrationComplete(error);
    }
    for (const QString& legacy : legacyAccountFiles) {
        if (!QFileInfo::exists(legacy)) continue;
        std::unique_ptr<ApplicationInstanceLock> accountSourceLock;
        if (!DataPathSafety::contains(dataDirectory_, legacy) &&
            !(sourceLock && DataPathSafety::contains(legacyDirectory, legacy))) {
            const QString parent = QFileInfo(legacy).absolutePath();
            if (DataPathSafety::hasLinkedComponent(QDir(parent).rootPath(), legacy))
                return fail(error, QStringLiteral("旧账户来源包含不安全链接：%1").arg(legacy));
            accountSourceLock = std::make_unique<ApplicationInstanceLock>(parent);
            QString lockError;
            if (!accountSourceLock->acquire(&lockError))
                return fail(error, QStringLiteral("无法获取旧账户来源目录锁：%1").arg(lockError));
        }
        if (!copyMissingFile(legacy, users, error)) return false;
        if (migratedFrom && migratedFrom->isEmpty()) *migratedFrom = legacy;
        break;
    }
    return markMigrationComplete(error);
}
