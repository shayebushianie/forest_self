#include "storage/BackupService.h"

#include "storage/DatabaseManager.h"
#include "storage/DataPathSafety.h"
#include "storage/BackupSnapshot.h"
#include "storage/RestoreTransaction.h"
#include "storage/StartupDataService.h"
#include "utils/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QUuid>

#include <vector>

namespace {

bool validateRestore(const QString& data, const QString& backup, QString* error)
{
    if (data.isEmpty() || backup.isEmpty() || DataPathSafety::overlaps(data, backup)) {
        if (error) *error = QStringLiteral("备份与当前数据目录不能相同或互相包含，请选择独立的备份目录");
        return false;
    }
    BackupManifest manifest;
    return BackupSnapshot::validate(backup, &manifest, error);
}

} // namespace

bool BackupService::createBackup(const QString& dataDirectory, const QString& destinationRoot,
                                 QString* backupDirectory, QString* error)
{
    const QString source = QDir(dataDirectory).absolutePath();
    const QString root = QDir(destinationRoot).absolutePath();
    if (dataDirectory.isEmpty() || destinationRoot.isEmpty() || DataPathSafety::contains(source, root)) {
        Logger::instance().warn("Unsafe backup location rejected.", "backup", "create-validate");
        if (error) *error = QStringLiteral("备份位置不能位于当前数据目录内");
        return false;
    }
    const QString name = QStringLiteral("ForestBackup-%1-%2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
             QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString target = QDir(root).filePath(name);
    if (DataPathSafety::overlaps(source, target) || QFileInfo::exists(target)) {
        Logger::instance().warn("Unsafe or existing backup target rejected.", "backup", "create-validate");
        if (error) *error = QStringLiteral("备份目标不安全或已存在");
        return false;
    }
    if (!BackupSnapshot::create(source, target, error)) {
        Logger::instance().error("Snapshot creation or validation failed.", "backup", "create");
        return false;
    }
    if (backupDirectory) *backupDirectory = target;
    Logger::instance().info("Validated snapshot published.", "backup", "create");
    return true;
}

bool BackupService::requestRestore(const QString& dataDirectory, const QString& backupDirectory,
                                   QString* error)
{
    if (!validateRestore(dataDirectory, backupDirectory, error)) {
        Logger::instance().warn("Restore request validation failed.", "backup", "restore-request");
        return false;
    }
    if (QFileInfo::exists(QDir(dataDirectory).filePath("restore_transaction.json"))) {
        Logger::instance().warn("Pending restore transaction blocks new request.", "backup", "restore-request");
        if (error) *error = QStringLiteral("有尚未完成的恢复事务，请先重启完成恢复");
        return false;
    }
    QSaveFile request(QDir(dataDirectory).filePath(QStringLiteral("restore_request.txt")));
    if (!request.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::instance().error("Restore request could not be opened.", "backup", "restore-request");
        if (error) *error = QStringLiteral("无法创建恢复请求");
        return false;
    }
    const QByteArray path = QDir(backupDirectory).absolutePath().toUtf8();
    if (request.write(path) != path.size() || !request.commit()) {
        Logger::instance().error("Restore request save failed.", "backup", "restore-request");
        if (error) *error = QStringLiteral("无法保存恢复请求");
        return false;
    }
    Logger::instance().info("Restore request saved; restart required.", "backup", "restore-request");
    return true;
}

bool BackupService::restoreIfRequested(const QString& dataDirectory, QString* restoredFrom, QString* error,
                                       QString* recoveryNotice, StartupDataService* startupData)
{
    if (startupData && QFileInfo::exists(QDir(dataDirectory).filePath("restore_transaction.json")) &&
        !startupData->markMigrationComplete(error)) {
        Logger::instance().error("Restore migration marker save failed.", "backup", "restore-recover");
        return false;
    }
    if (!RestoreTransaction::recover(dataDirectory, restoredFrom, error, recoveryNotice)) {
        Logger::instance().error("Interrupted restore recovery failed.", "backup", "restore-recover");
        return false;
    }
    const QString requestPath = QDir(dataDirectory).filePath(QStringLiteral("restore_request.txt"));
    QFile request(requestPath);
    if (!request.exists()) return true;
    if (request.size() > 32768 || QFileInfo(requestPath).isSymLink() ||
        !request.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Logger::instance().error("Restore request unreadable or unsafe.", "backup", "restore-validate");
        if (error) *error = QStringLiteral("无法读取恢复请求");
        return false;
    }
    const QString source = QString::fromUtf8(request.readAll()).trimmed();
    request.close();
    if (!validateRestore(dataDirectory, source, error)) {
        Logger::instance().error("Requested snapshot validation failed.", "backup", "restore-validate");
        return false;
    }
    // Only a validated request may supersede legacy input. Persist before any restore writes.
    if (startupData && !startupData->markMigrationComplete(error)) {
        Logger::instance().error("Restore migration marker save failed.", "backup", "restore-apply");
        return false;
    }
    const bool restored = RestoreTransaction::apply(dataDirectory, source, restoredFrom, error);
    if (restored) Logger::instance().info("Validated restore transaction committed.", "backup", "restore-apply");
    else Logger::instance().error("Restore transaction failed; startup stopped.", "backup", "restore-apply");
    return restored;
}

bool BackupService::exportFocusCsv(DatabaseManager& database, const QString& filePath, QString* error)
{
    std::vector<FocusRecord> records;
    if (!database.getAllRecords(records)) {
        Logger::instance().error("History read failed; export not written.", "backup", "export-read");
        if (error) {
            const QString databaseError = database.lastError();
            *error = databaseError.isEmpty()
                ? QStringLiteral("无法读取专注记录")
                : QStringLiteral("无法读取专注记录：%1").arg(databaseError);
        }
        return false;
    }

    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::instance().error("Export output could not be opened.", "backup", "export-write");
        if (error) *error = QStringLiteral("无法创建导出文件");
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "record_id,start_timestamp,plant_type,planned_minutes,actual_seconds,status,tag_id,coins_earned,interrupted\n";
    for (const FocusRecord& record : records) {
        stream << record.recordId << ',' << record.startTimestamp << ',' << record.plantType << ','
               << record.plannedMinutes << ',' << record.actualSeconds << ','
               << static_cast<uint32_t>(record.status) << ',' << record.tagId << ',' << record.coinsEarned << ','
               << (isInterruptedFocus(record) ? 1 : 0) << '\n';
    }
    stream.flush();
    const bool streamFlushed = stream.status() == QTextStream::Ok;
    const bool fileFlushed = file.flush();
    if (!streamFlushed || !fileFlushed) {
        Logger::instance().error("Export flush failed; output not committed.", "backup", "export-write");
        if (error) *error = QStringLiteral("无法完成导出文件写入");
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        Logger::instance().error("Export commit failed.", "backup", "export-write");
        if (error) *error = QStringLiteral("无法完成导出文件保存");
        return false;
    }
    if (error) error->clear();
    Logger::instance().info("records=" + std::to_string(records.size()) + " exported.", "backup", "export-complete");
    return true;
}
