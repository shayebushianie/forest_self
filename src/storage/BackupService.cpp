#include "storage/BackupService.h"

#include "storage/DatabaseManager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

namespace {

bool copyTree(const QString& source, const QString& target, QString* error)
{
    QDir sourceDir(source);
    if (!sourceDir.exists() || !QDir().mkpath(target)) {
        if (error) *error = QStringLiteral("无法访问备份目录");
        return false;
    }
    QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourceFile = it.next();
        const QString relative = sourceDir.relativeFilePath(sourceFile);
        if (relative == QStringLiteral("forest.lock") || relative.startsWith(QStringLiteral("app.log")) ||
            relative == QStringLiteral("restore_request.txt")) continue;
        const QString targetFile = QDir(target).filePath(relative);
        if (!QDir().mkpath(QFileInfo(targetFile).dir().absolutePath())) {
            if (error) *error = QStringLiteral("无法创建备份子目录");
            return false;
        }
        QFile::remove(targetFile);
        if (!QFile::copy(sourceFile, targetFile)) {
            if (error) *error = QStringLiteral("无法复制备份文件：%1").arg(relative);
            return false;
        }
    }
    return true;
}

} // namespace

bool BackupService::createBackup(const QString& dataDirectory, const QString& destinationRoot,
                                 QString* backupDirectory, QString* error)
{
    const QString source = QDir(dataDirectory).absolutePath();
    const QString root = QDir(destinationRoot).absolutePath();
    if (root.startsWith(source + QDir::separator())) {
        if (error) *error = QStringLiteral("备份位置不能位于当前数据目录内");
        return false;
    }
    const QString name = QStringLiteral("ForestBackup-%1")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString target = QDir(root).filePath(name);
    if (!copyTree(source, target, error)) return false;
    QSaveFile manifest(QDir(target).filePath(QStringLiteral("backup_manifest.txt")));
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法写入备份清单");
        return false;
    }
    QTextStream stream(&manifest);
    stream << "ForestBackup\nversion=1\ncreated=" << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
    if (!manifest.commit()) {
        if (error) *error = QStringLiteral("无法完成备份清单保存");
        return false;
    }
    if (backupDirectory) *backupDirectory = target;
    return true;
}

bool BackupService::requestRestore(const QString& dataDirectory, const QString& backupDirectory,
                                   QString* error)
{
    if (!QFileInfo::exists(QDir(backupDirectory).filePath(QStringLiteral("backup_manifest.txt")))) {
        if (error) *error = QStringLiteral("所选目录不是有效的 Forest 备份");
        return false;
    }
    QSaveFile request(QDir(dataDirectory).filePath(QStringLiteral("restore_request.txt")));
    if (!request.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法创建恢复请求");
        return false;
    }
    request.write(QDir(backupDirectory).absolutePath().toUtf8());
    if (!request.commit()) {
        if (error) *error = QStringLiteral("无法保存恢复请求");
        return false;
    }
    return true;
}

bool BackupService::restoreIfRequested(const QString& dataDirectory, QString* restoredFrom, QString* error)
{
    const QString requestPath = QDir(dataDirectory).filePath(QStringLiteral("restore_request.txt"));
    QFile request(requestPath);
    if (!request.exists()) return true;
    if (!request.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法读取恢复请求");
        return false;
    }
    const QString source = QString::fromUtf8(request.readAll()).trimmed();
    if (!QFileInfo::exists(QDir(source).filePath(QStringLiteral("backup_manifest.txt")))) {
        if (error) *error = QStringLiteral("恢复备份无效");
        return false;
    }
    const QString safetyRoot = QFileInfo(dataDirectory).dir().filePath(QStringLiteral("ForestRestoreSnapshots"));
    QString safetyBackup;
    if (!createBackup(dataDirectory, safetyRoot, &safetyBackup, error)) return false;
    if (!copyTree(source, dataDirectory, error)) return false;
    request.remove();
    if (restoredFrom) *restoredFrom = source;
    return true;
}

bool BackupService::exportFocusCsv(DatabaseManager& database, const QString& filePath, QString* error)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法创建导出文件");
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "record_id,start_timestamp,plant_type,planned_minutes,actual_seconds,status,tag_id,coins_earned\n";
    for (const FocusRecord& record : database.getAllRecords()) {
        stream << record.recordId << ',' << record.startTimestamp << ',' << record.plantType << ','
               << record.plannedMinutes << ',' << record.actualSeconds << ','
               << static_cast<uint32_t>(record.status) << ',' << record.tagId << ',' << record.coinsEarned << '\n';
    }
    if (!file.commit()) {
        if (error) *error = QStringLiteral("无法完成导出文件保存");
        return false;
    }
    return true;
}
