#include "storage/DatabaseManager.h"

#include "storage/StorageEnvelope.h"
#include "utils/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QSaveFile>
#include <QUuid>

#include <cstring>
#include <limits>

namespace {

constexpr quint32 kSessionMagic = 0x46534631u; // FSF1
constexpr quint16 kSessionVersion = 1;
constexpr int kRecordSize = 64;
constexpr int kPreviousSchemaVersion = 1;

QString sqlitePathFor(const QString& legacyOrDatabasePath)
{
    const QFileInfo info(legacyOrDatabasePath);
    if (info.suffix().compare(QStringLiteral("dat"), Qt::CaseInsensitive) == 0) {
        return info.dir().filePath(info.completeBaseName() + QStringLiteral(".sqlite"));
    }
    return info.absoluteFilePath();
}

bool ensureLegacyMigrationBackup(const QString& sourcePath, QString* error)
{
    const QString backupPath = sourcePath + QStringLiteral(".sqlite-migration.bak");
    const QFileInfo backupInfo(backupPath);
    if (backupInfo.exists() || backupInfo.isSymLink()) {
        if (!backupInfo.isFile() || backupInfo.isSymLink()) {
            *error = QStringLiteral("旧专注记录迁移备份不是普通文件");
            return false;
        }
        return true;
    }

    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取旧专注记录以创建迁移备份");
        return false;
    }

    QSaveFile backup(backupPath);
    backup.setDirectWriteFallback(false);
    if (!backup.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("无法创建旧专注记录迁移备份：%1").arg(backup.errorString());
        return false;
    }

    while (!source.atEnd()) {
        const QByteArray chunk = source.read(64 * 1024);
        if (chunk.isEmpty()) {
            if (source.error() != QFileDevice::NoError) {
                *error = QStringLiteral("读取旧专注记录迁移备份失败");
                backup.cancelWriting();
                return false;
            }
            break;
        }

        qint64 written = 0;
        while (written < chunk.size()) {
            const qint64 amount = backup.write(chunk.constData() + written, chunk.size() - written);
            if (amount <= 0) {
                *error = QStringLiteral("写入旧专注记录迁移备份失败：%1").arg(backup.errorString());
                backup.cancelWriting();
                return false;
            }
            written += amount;
        }
    }

    if (source.error() != QFileDevice::NoError) {
        *error = QStringLiteral("读取旧专注记录迁移备份失败");
        backup.cancelWriting();
        return false;
    }
    if (!backup.commit()) {
        *error = QStringLiteral("无法提交旧专注记录迁移备份：%1").arg(backup.errorString());
        return false;
    }
    return true;
}

void bindRecord(QSqlQuery& query, const FocusRecord& record, bool includeId)
{
    if (includeId) query.bindValue(QStringLiteral(":id"), record.recordId);
    query.bindValue(QStringLiteral(":plantType"), record.plantType);
    query.bindValue(QStringLiteral(":plannedMinutes"), record.plannedMinutes);
    query.bindValue(QStringLiteral(":actualSeconds"), record.actualSeconds);
    query.bindValue(QStringLiteral(":startTimestamp"), static_cast<qulonglong>(record.startTimestamp));
    query.bindValue(QStringLiteral(":status"), static_cast<uint>(record.status));
    query.bindValue(QStringLiteral(":violationCount"), record.violationCount);
    query.bindValue(QStringLiteral(":growthStage"), record.growthStage);
    query.bindValue(QStringLiteral(":coinsEarned"), record.coinsEarned);
    query.bindValue(QStringLiteral(":tagId"), record.tagId);
    query.bindValue(QStringLiteral(":focusMode"), static_cast<uint>(record.focusMode));
    query.bindValue(QStringLiteral(":gridIndex"), record.gridIndex);
    query.bindValue(QStringLiteral(":interrupted"), isInterruptedFocus(record) ? 1 : 0);
}

} // namespace

DatabaseManager::DatabaseManager(const std::string& legacyOrDatabasePath)
    : legacyPath_(QString::fromStdString(legacyOrDatabasePath)),
      databasePath_(sqlitePathFor(legacyPath_)),
      connectionName_(QStringLiteral("forest_focus_%1").arg(QUuid::createUuid().toString(QUuid::Id128)))
{
}

DatabaseManager::~DatabaseManager()
{
    close();
}

bool DatabaseManager::open()
{
    lastError_.clear();
    if (database_.isOpen()) return true;
    if (!QDir().mkpath(QFileInfo(databasePath_).dir().absolutePath())) {
        setError(QStringLiteral("无法创建专注数据目录"));
        return false;
    }

    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        setError(QStringLiteral("无法打开专注数据库：%1").arg(database_.lastError().text()));
        close();
        return false;
    }

    const bool initialized = [this]() {
        QSqlQuery pragmas(database_);
        if (!pragmas.exec(QStringLiteral("PRAGMA foreign_keys = ON")) ||
            !pragmas.exec(QStringLiteral("PRAGMA synchronous = FULL"))) {
            setError(QStringLiteral("无法配置专注数据库：%1").arg(pragmas.lastError().text()));
            return false;
        }
        if (!createSchema()) return false;
        if (!pragmas.exec(QStringLiteral("PRAGMA journal_mode = WAL"))) {
            setError(QStringLiteral("无法配置专注数据库：%1").arg(pragmas.lastError().text()));
            return false;
        }
        pragmas.finish();
        if (!migrateLegacyRecords()) return false;
        return recoverTerminalIntents() && recoverFromCrash();
    }();
    if (!initialized) close();
    return initialized;
}

void DatabaseManager::close()
{
    if (!database_.isValid()) return;
    const QString name = connectionName_;
    database_.close();
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

bool DatabaseManager::validateRecordSchema(const QSqlDatabase& database, int version, QString* error)
{
    QSqlQuery query(database);
    QString columns = QStringLiteral("record_id,plant_type,planned_minutes,actual_seconds,start_timestamp,"
        "status,violation_count,growth_stage,coins_earned,tag_id,focus_mode,grid_index");
    if (version >= 4) columns += QStringLiteral(",interrupted");
    if (!query.exec(QStringLiteral("SELECT %1 FROM focus_records LIMIT 0").arg(columns))) {
        if (error) *error = QStringLiteral("专注记录表缺失或结构不完整：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

bool DatabaseManager::createSchema()
{
    bool hasVersionRow = false;
    qlonglong existingVersion = 0;
    {
        QSqlQuery tableQuery(database_);
        if (!tableQuery.exec(QStringLiteral(
                "SELECT 1 FROM sqlite_master WHERE type = 'table' "
                "AND name = 'schema_version' COLLATE NOCASE"))) {
            setError(tableQuery.lastError().text());
            return false;
        }
        if (tableQuery.next()) {
            QSqlQuery versionQuery(database_);
            if (!versionQuery.exec(QStringLiteral("SELECT version FROM schema_version"))) {
                setError(QStringLiteral("无法读取专注数据版本：%1").arg(versionQuery.lastError().text()));
                return false;
            }
            if (versionQuery.next()) {
                bool validVersion = false;
                existingVersion = versionQuery.value(0).toLongLong(&validVersion);
                if (!validVersion || versionQuery.next()) {
                    setError(QStringLiteral("专注数据版本结构异常，无法安全打开"));
                    return false;
                }
                hasVersionRow = true;
                if (existingVersion > schemaVersion()) {
                    setError(QStringLiteral("专注数据版本高于当前程序，无法安全打开"));
                    return false;
                }
                if (existingVersion < kPreviousSchemaVersion || existingVersion > schemaVersion()) {
                    setError(QStringLiteral("专注数据版本不受支持，无法安全打开"));
                    return false;
                }
            }
        }
    }

    if (hasVersionRow && !validateRecordSchema(database_, static_cast<int>(existingVersion), &lastError_))
        return false;

    if (!database_.transaction()) {
        setError(database_.lastError().text());
        return false;
    }

    QSqlQuery query(database_);
    const char* statements[] = {
        "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS focus_records ("
        "record_id INTEGER PRIMARY KEY, plant_type INTEGER NOT NULL, planned_minutes INTEGER NOT NULL, "
        "actual_seconds INTEGER NOT NULL, start_timestamp INTEGER NOT NULL, status INTEGER NOT NULL, "
        "violation_count INTEGER NOT NULL, growth_stage INTEGER NOT NULL, coins_earned INTEGER NOT NULL, "
        "tag_id INTEGER NOT NULL, focus_mode INTEGER NOT NULL, grid_index INTEGER NOT NULL, "
        "interrupted INTEGER NOT NULL DEFAULT 0 CHECK(interrupted IN (0,1)))",
        "CREATE INDEX IF NOT EXISTS idx_focus_records_start ON focus_records(start_timestamp)",
        "CREATE INDEX IF NOT EXISTS idx_focus_records_status ON focus_records(status)",
        "CREATE INDEX IF NOT EXISTS idx_focus_records_tag ON focus_records(tag_id)",
        "CREATE INDEX IF NOT EXISTS idx_focus_records_plant ON focus_records(plant_type)",
        "CREATE TABLE IF NOT EXISTS legacy_import_state (id INTEGER PRIMARY KEY CHECK(id = 1))",
        "CREATE TABLE IF NOT EXISTS focus_terminal_intents ("
        "record_id INTEGER PRIMARY KEY REFERENCES focus_records(record_id), "
        "actual_seconds INTEGER NOT NULL, status INTEGER NOT NULL CHECK(status IN (0,1,2)), "
        "violation_count INTEGER NOT NULL, growth_stage INTEGER NOT NULL, coins_earned INTEGER NOT NULL)"
    };
    for (const char* statement : statements) {
        if (!query.exec(QString::fromLatin1(statement))) {
            setError(QStringLiteral("无法创建专注数据结构：%1").arg(query.lastError().text()));
            database_.rollback();
            return false;
        }
    }

    bool hasInterruptedColumn = false;
    if (!query.exec(QStringLiteral("PRAGMA table_info(focus_records)"))) {
        setError(query.lastError().text());
        database_.rollback();
        return false;
    }
    while (query.next()) hasInterruptedColumn |= query.value(1).toString() == QStringLiteral("interrupted");
    query.finish();
    if (!hasInterruptedColumn && !query.exec(QStringLiteral(
            "ALTER TABLE focus_records ADD COLUMN interrupted INTEGER NOT NULL DEFAULT 0 CHECK(interrupted IN (0,1))"))) {
        setError(QStringLiteral("无法升级中断记录标记：%1").arg(query.lastError().text()));
        database_.rollback();
        return false;
    }

    if (!hasVersionRow) {
        QSqlQuery insert(database_);
        insert.prepare(QStringLiteral("INSERT INTO schema_version(version) VALUES(?)"));
        insert.addBindValue(schemaVersion());
        if (!insert.exec()) {
            setError(insert.lastError().text());
            database_.rollback();
            return false;
        }
    } else if (existingVersion < schemaVersion()) {
        QSqlQuery update(database_);
        update.prepare(QStringLiteral("UPDATE schema_version SET version = ? WHERE version = ?"));
        update.addBindValue(schemaVersion());
        update.addBindValue(existingVersion);
        if (!update.exec() || update.numRowsAffected() != 1) {
            setError(QStringLiteral("无法升级专注数据版本：%1").arg(update.lastError().text()));
            database_.rollback();
            return false;
        }
    }

    if (!database_.commit()) {
        setError(database_.lastError().text());
        database_.rollback();
        return false;
    }
    return true;
}

bool DatabaseManager::migrateLegacyRecords()
{
    if (!database_.transaction()) {
        setError(database_.lastError().text());
        return false;
    }

    const auto rollback = [this]() {
        Logger::instance().error("Legacy record migration rolled back.", "storage", "migrate-records");
        database_.rollback();
        return false;
    };
    const auto commit = [this]() {
        if (database_.commit()) return true;
        Logger::instance().error("Legacy record migration commit failed.", "storage", "migrate-records");
        setError(database_.lastError().text());
        database_.rollback();
        return false;
    };
    const auto markComplete = [this]() {
        QSqlQuery marker(database_);
        if (!marker.exec(QStringLiteral("INSERT INTO legacy_import_state(id) VALUES(1)"))) {
            setError(QStringLiteral("无法保存旧专注记录迁移状态：%1").arg(marker.lastError().text()));
            return false;
        }
        return true;
    };

    bool migrationComplete = false;
    {
        QSqlQuery marker(database_);
        if (!marker.exec(QStringLiteral("SELECT 1 FROM legacy_import_state WHERE id = 1"))) {
            setError(marker.lastError().text());
            return rollback();
        }
        migrationComplete = marker.next();
    }
    if (migrationComplete) return commit();

    int existingRecordCount = 0;
    {
        QSqlQuery countQuery(database_);
        if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM focus_records")) || !countQuery.next()) {
            setError(countQuery.lastError().text());
            return rollback();
        }
        existingRecordCount = countQuery.value(0).toInt();
    }
    if (existingRecordCount > 0) {
        Logger::instance().warn(
            "Unmarked nonempty focus database found; preserving existing records and skipping legacy import. "
            "This records a conservative compatibility skip, not a verified import.", "storage", "migrate-skip");
        if (!markComplete()) return rollback();
        return commit();
    }

    const bool hasLegacySource = QFileInfo::exists(legacyPath_) &&
                                 QFileInfo(legacyPath_).absoluteFilePath() != databasePath_;
    std::vector<FocusRecord> records;
    if (hasLegacySource && !loadLegacyRecords(&records)) return rollback();

    if (hasLegacySource) {
        QString backupError;
        if (!ensureLegacyMigrationBackup(legacyPath_, &backupError)) {
            setError(backupError);
            return rollback();
        }
    }

    for (const FocusRecord& record : records) {
        if (!insertRecord(record)) {
            return rollback();
        }
    }
    if (!markComplete()) return rollback();
    if (!commit()) return false;
    if (hasLegacySource)
        Logger::instance().info("records=" + std::to_string(records.size()) + " legacy records imported.",
                                "storage", "migrate-records");
    return true;
}

bool DatabaseManager::loadLegacyRecords(std::vector<FocusRecord>* records)
{
    const StorageEnvelope::ReadResult result = StorageEnvelope::read(legacyPath_, kSessionMagic);
    QByteArray bytes;
    if (result.hasPayload()) {
        if (result.version != kSessionVersion) {
            setError(QStringLiteral("旧专注记录版本不受支持"));
            return false;
        }
        bytes = result.payload;
    } else if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile file(legacyPath_);
        if (!file.open(QIODevice::ReadOnly)) {
            setError(QStringLiteral("无法读取旧专注记录"));
            return false;
        }
        bytes = file.readAll();
    } else if (result.state == StorageEnvelope::ReadState::Missing) {
        return true;
    } else {
        setError(QStringLiteral("旧专注记录损坏：%1").arg(result.error));
        return false;
    }

    const qsizetype validBytes = bytes.size() - (bytes.size() % kRecordSize);
    records->reserve(static_cast<size_t>(validBytes / kRecordSize));
    for (qsizetype offset = 0; offset < validBytes; offset += kRecordSize) {
        FocusRecord record;
        std::memcpy(&record, bytes.constData() + offset, kRecordSize);
        record.recordId = static_cast<uint32_t>(records->size());
        records->push_back(record);
    }
    return true;
}

uint32_t DatabaseManager::append(const FocusRecord& record)
{
    if (!ensureOpen()) return std::numeric_limits<uint32_t>::max();
    QSqlQuery idQuery(database_);
    if (!idQuery.exec(QStringLiteral("SELECT COALESCE(MAX(record_id) + 1, 0) FROM focus_records")) || !idQuery.next()) {
        setError(idQuery.lastError().text());
        return std::numeric_limits<uint32_t>::max();
    }
    FocusRecord toInsert = record;
    toInsert.recordId = idQuery.value(0).toUInt();
    if (!insertRecord(toInsert)) return std::numeric_limits<uint32_t>::max();
    return toInsert.recordId;
}

bool DatabaseManager::insertRecord(const FocusRecord& record)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO focus_records(record_id, plant_type, planned_minutes, actual_seconds, start_timestamp, status, "
        "violation_count, growth_stage, coins_earned, tag_id, focus_mode, grid_index, interrupted) "
        "VALUES(:id, :plantType, :plannedMinutes, :actualSeconds, :startTimestamp, :status, :violationCount, "
        ":growthStage, :coinsEarned, :tagId, :focusMode, :gridIndex, :interrupted)"));
    bindRecord(query, record, true);
    if (!query.exec()) {
        setError(QStringLiteral("无法写入专注记录：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

std::optional<FocusRecord> DatabaseManager::readById(uint32_t recordId)
{
    if (!ensureOpen()) return std::nullopt;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT * FROM focus_records WHERE record_id = ?"));
    query.addBindValue(recordId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return std::nullopt;
    }
    if (query.next()) return readRecord(query);
    if (query.lastError().isValid()) setError(query.lastError().text());
    return std::nullopt;
}

bool DatabaseManager::updateById(uint32_t recordId, const FocusRecord& record)
{
    if (!ensureOpen()) return false;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE focus_records SET plant_type=:plantType, planned_minutes=:plannedMinutes, actual_seconds=:actualSeconds, "
        "start_timestamp=:startTimestamp, status=:status, violation_count=:violationCount, growth_stage=:growthStage, "
        "coins_earned=:coinsEarned, tag_id=:tagId, focus_mode=:focusMode, grid_index=:gridIndex, "
        "interrupted=:interrupted WHERE record_id=:id"));
    FocusRecord toWrite = record;
    toWrite.recordId = recordId;
    bindRecord(query, toWrite, true);
    if (!query.exec() || query.numRowsAffected() != 1) {
        setError(QStringLiteral("无法更新专注记录：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

std::vector<FocusRecord> DatabaseManager::getAllRecords()
{
    std::vector<FocusRecord> records;
    getAllRecords(records);
    return records;
}

bool DatabaseManager::getAllRecords(std::vector<FocusRecord>& records)
{
    if (!ensureOpen()) return false;
    std::vector<FocusRecord> loaded;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT * FROM focus_records ORDER BY record_id"))) {
        setError(query.lastError().text());
        return false;
    }
    while (query.next()) loaded.push_back(readRecord(query));
    if (query.lastError().isValid()) {
        setError(query.lastError().text());
        return false;
    }
    records = std::move(loaded);
    return true;
}

uint32_t DatabaseManager::count() const
{
    uint32_t result = 0;
    count(result);
    return result;
}

bool DatabaseManager::count(uint32_t& result) const
{
    if (!ensureOpen()) return false;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM focus_records")) || !query.next()) {
        setError(QStringLiteral("无法读取专注记录数量：%1").arg(query.lastError().text()));
        return false;
    }
    result = query.value(0).toUInt();
    return true;
}

bool DatabaseManager::finalizeRecord(uint32_t recordId, const FocusRecord& record)
{
    const auto existing = readById(recordId);
    if (!existing || !isTerminalFocusStatus(record.status) ||
        (record.status != FocusRecordStatus::Success && record.coinsEarned != 0) ||
        existing->plantType != record.plantType || existing->startTimestamp != record.startTimestamp ||
        existing->plannedMinutes != record.plannedMinutes || existing->tagId != record.tagId ||
        existing->focusMode != record.focusMode || existing->gridIndex != record.gridIndex) {
        setError(QStringLiteral("专注终态与原始记录不一致，未改变历史。"));
        return false;
    }
    if (existing->status != FocusRecordStatus::Running &&
        (existing->status != record.status || existing->actualSeconds != record.actualSeconds ||
         existing->violationCount != record.violationCount || existing->growthStage != record.growthStage ||
         existing->coinsEarned != record.coinsEarned)) {
        setError(QStringLiteral("已完成专注不能改判。"));
        return false;
    }
    if (failTerminalIntent_) {
        failTerminalIntent_ = false;
        setError(QStringLiteral("测试注入：终态决定保存失败"));
        return false;
    }
    QSqlQuery intent(database_);
    intent.prepare(QStringLiteral(
        "INSERT INTO focus_terminal_intents(record_id,actual_seconds,status,violation_count,growth_stage,coins_earned) "
        "VALUES(?,?,?,?,?,?) ON CONFLICT(record_id) DO NOTHING"));
    intent.addBindValue(recordId);
    intent.addBindValue(record.actualSeconds);
    intent.addBindValue(static_cast<uint>(record.status));
    intent.addBindValue(record.violationCount);
    intent.addBindValue(record.growthStage);
    intent.addBindValue(record.coinsEarned);
    if (!intent.exec()) {
        setError(QStringLiteral("无法保存专注终态决定：%1").arg(intent.lastError().text()));
        return false;
    }
    intent.finish();
    QSqlQuery saved(database_);
    saved.prepare("SELECT actual_seconds,status,violation_count,growth_stage,coins_earned FROM focus_terminal_intents WHERE record_id=?");
    saved.addBindValue(recordId);
    if (!saved.exec() || !saved.next() || saved.value(0).toUInt() != record.actualSeconds ||
        saved.value(1).toUInt() != static_cast<uint>(record.status) ||
        saved.value(2).toUInt() != record.violationCount || saved.value(3).toUInt() != record.growthStage ||
        saved.value(4).toUInt() != record.coinsEarned) {
        setError(QStringLiteral("已有另一专注终态决定，不能改判。"));
        return false;
    }
    saved.finish();
    if (failTerminalApply_) {
        failTerminalApply_ = false;
        setError(QStringLiteral("测试注入：终态应用失败，决定已保存"));
        return false;
    }
    return applyTerminalIntent(recordId);
}

bool DatabaseManager::applyTerminalIntent(uint32_t recordId)
{
    QSqlQuery intent(database_);
    intent.prepare("SELECT actual_seconds,status,violation_count,growth_stage,coins_earned FROM focus_terminal_intents WHERE record_id=?");
    intent.addBindValue(recordId);
    if (!intent.exec() || !intent.next()) {
        setError(QStringLiteral("无法读取待保存专注终态：%1").arg(intent.lastError().text()));
        return false;
    }
    const auto existing = readById(recordId);
    if (!existing) {
        setError(QStringLiteral("待保存终态缺少原始专注记录。"));
        return false;
    }
    FocusRecord terminal = *existing;
    terminal.actualSeconds = intent.value(0).toUInt();
    terminal.status = static_cast<FocusRecordStatus>(intent.value(1).toUInt());
    terminal.violationCount = intent.value(2).toUInt();
    terminal.growthStage = intent.value(3).toUInt();
    terminal.coinsEarned = intent.value(4).toUInt();
    intent.finish();
    const bool sameTerminal = existing->status == terminal.status && existing->actualSeconds == terminal.actualSeconds &&
        existing->violationCount == terminal.violationCount && existing->growthStage == terminal.growthStage &&
        existing->coinsEarned == terminal.coinsEarned;
    if (!isTerminalFocusStatus(terminal.status) ||
        (terminal.status != FocusRecordStatus::Success && terminal.coinsEarned != 0) ||
        (existing->status != FocusRecordStatus::Running && !sameTerminal)) {
        setError(QStringLiteral("待保存终态与历史冲突，请恢复完整数据快照。"));
        return false;
    }
    if (!database_.transaction()) {
        setError(database_.lastError().text());
        return false;
    }
    if (!sameTerminal && !updateById(recordId, terminal)) {
        database_.rollback();
        return false;
    }
    QSqlQuery clear(database_);
    clear.prepare("DELETE FROM focus_terminal_intents WHERE record_id=?");
    clear.addBindValue(recordId);
    if (!clear.exec() || clear.numRowsAffected() != 1) {
        setError(QStringLiteral("无法完成终态事务：%1").arg(clear.lastError().text()));
        database_.rollback();
        return false;
    }
    clear.finish();
    if (!database_.commit()) {
        setError(database_.lastError().text());
        database_.rollback();
        return false;
    }
    lastError_.clear();
    return true;
}

bool DatabaseManager::recoverTerminalIntents()
{
    QSqlQuery query(database_);
    if (!query.exec("SELECT record_id FROM focus_terminal_intents ORDER BY record_id")) {
        setError(query.lastError().text());
        return false;
    }
    std::vector<uint32_t> pending;
    while (query.next()) pending.push_back(query.value(0).toUInt());
    query.finish();
    for (auto recordId : pending) {
        if (!applyTerminalIntent(recordId)) {
            Logger::instance().error("record=" + std::to_string(recordId) + " terminal intent recovery failed.",
                                     "storage", "recover-terminal");
            return false;
        }
        Logger::instance().info("record=" + std::to_string(recordId) + " terminal intent recovered.",
                                "storage", "recover-terminal");
    }
    return true;
}

bool DatabaseManager::recoverFromCrash()
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE focus_records SET status=?, growth_stage=4, coins_earned=0, interrupted=1 WHERE status=?"));
    query.addBindValue(static_cast<uint>(FocusRecordStatus::Abandoned));
    query.addBindValue(static_cast<uint>(FocusRecordStatus::Running));
    if (!query.exec()) {
        setError(QStringLiteral("无法恢复中断专注：%1").arg(query.lastError().text()));
        Logger::instance().error("Interrupted focus recovery failed.", "storage", "recover-interrupted");
        return false;
    }
    if (query.numRowsAffected() > 0)
        Logger::instance().info("records=" + std::to_string(query.numRowsAffected()) + " interrupted records recovered.",
                                "storage", "recover-interrupted");
    return true;
}

FocusRecord DatabaseManager::readRecord(QSqlQuery& query)
{
    FocusRecord record;
    record.recordId = query.value(QStringLiteral("record_id")).toUInt();
    record.plantType = query.value(QStringLiteral("plant_type")).toUInt();
    record.plannedMinutes = query.value(QStringLiteral("planned_minutes")).toUInt();
    record.actualSeconds = query.value(QStringLiteral("actual_seconds")).toUInt();
    record.startTimestamp = query.value(QStringLiteral("start_timestamp")).toULongLong();
    record.status = static_cast<FocusRecordStatus>(query.value(QStringLiteral("status")).toUInt());
    record.violationCount = query.value(QStringLiteral("violation_count")).toUInt();
    record.growthStage = query.value(QStringLiteral("growth_stage")).toUInt();
    record.coinsEarned = query.value(QStringLiteral("coins_earned")).toUInt();
    record.tagId = query.value(QStringLiteral("tag_id")).toUInt();
    record.focusMode = static_cast<PersistedFocusMode>(query.value(QStringLiteral("focus_mode")).toUInt());
    record.gridIndex = static_cast<uint8_t>(query.value(QStringLiteral("grid_index")).toUInt());
    if (query.value(QStringLiteral("interrupted")).toBool()) std::memcpy(record.reserved, "INT1", 4);
    return record;
}

bool DatabaseManager::ensureOpen() const
{
    lastError_.clear();
    if (database_.isValid() && database_.isOpen()) return true;
    setError(QStringLiteral("专注数据库尚未打开，无法读取或保存记录。"));
    return false;
}

void DatabaseManager::setError(const QString& error) const
{
    lastError_ = error;
}
