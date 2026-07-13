#include "storage/DatabaseManager.h"

#include "storage/StorageEnvelope.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <cstring>
#include <limits>

namespace {

constexpr quint32 kSessionMagic = 0x46534631u; // FSF1
constexpr quint16 kSessionVersion = 1;
constexpr int kRecordSize = 64;

QString sqlitePathFor(const QString& legacyOrDatabasePath)
{
    const QFileInfo info(legacyOrDatabasePath);
    if (info.suffix().compare(QStringLiteral("dat"), Qt::CaseInsensitive) == 0) {
        return info.dir().filePath(info.completeBaseName() + QStringLiteral(".sqlite"));
    }
    return info.absoluteFilePath();
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
    if (!QDir().mkpath(QFileInfo(databasePath_).dir().absolutePath())) {
        setError(QStringLiteral("无法创建专注数据目录"));
        return false;
    }

    const bool databaseAlreadyExists = QFileInfo::exists(databasePath_);
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath_);
    if (!database_.open()) {
        setError(QStringLiteral("无法打开专注数据库：%1").arg(database_.lastError().text()));
        return false;
    }

    QSqlQuery pragmas(database_);
    if (!pragmas.exec(QStringLiteral("PRAGMA foreign_keys = ON")) ||
        !pragmas.exec(QStringLiteral("PRAGMA journal_mode = WAL")) ||
        !pragmas.exec(QStringLiteral("PRAGMA synchronous = FULL"))) {
        setError(QStringLiteral("无法配置专注数据库：%1").arg(pragmas.lastError().text()));
        return false;
    }
    if (!createSchema()) return false;
    if (!databaseAlreadyExists && !migrateLegacyRecords()) return false;
    return recoverFromCrash();
}

void DatabaseManager::close()
{
    if (!database_.isValid()) return;
    const QString name = connectionName_;
    database_.close();
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

bool DatabaseManager::createSchema()
{
    QSqlQuery query(database_);
    const char* statements[] = {
        "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS focus_records ("
        "record_id INTEGER PRIMARY KEY, plant_type INTEGER NOT NULL, planned_minutes INTEGER NOT NULL, "
        "actual_seconds INTEGER NOT NULL, start_timestamp INTEGER NOT NULL, status INTEGER NOT NULL, "
        "violation_count INTEGER NOT NULL, growth_stage INTEGER NOT NULL, coins_earned INTEGER NOT NULL, "
        "tag_id INTEGER NOT NULL, focus_mode INTEGER NOT NULL, grid_index INTEGER NOT NULL)",
        "CREATE INDEX IF NOT EXISTS idx_focus_records_start ON focus_records(start_timestamp)",
        "CREATE INDEX IF NOT EXISTS idx_focus_records_status ON focus_records(status)",
        "CREATE INDEX IF NOT EXISTS idx_focus_records_tag ON focus_records(tag_id)",
        "CREATE INDEX IF NOT EXISTS idx_focus_records_plant ON focus_records(plant_type)"
    };
    for (const char* statement : statements) {
        if (!query.exec(QString::fromLatin1(statement))) {
            setError(QStringLiteral("无法创建专注数据结构：%1").arg(query.lastError().text()));
            return false;
        }
    }
    if (!query.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1"))) {
        setError(query.lastError().text());
        return false;
    }
    if (!query.next()) {
        QSqlQuery insert(database_);
        insert.prepare(QStringLiteral("INSERT INTO schema_version(version) VALUES(?)"));
        insert.addBindValue(schemaVersion());
        if (!insert.exec()) {
            setError(insert.lastError().text());
            return false;
        }
    } else if (query.value(0).toInt() > schemaVersion()) {
        setError(QStringLiteral("专注数据版本高于当前程序，无法安全打开"));
        return false;
    }
    return true;
}

bool DatabaseManager::migrateLegacyRecords()
{
    if (!QFileInfo::exists(legacyPath_) || QFileInfo(legacyPath_).absoluteFilePath() == databasePath_) {
        return true;
    }

    std::vector<FocusRecord> records;
    if (!loadLegacyRecords(&records)) return false;
    if (records.empty()) return true;

    const QString backupPath = legacyPath_ + QStringLiteral(".sqlite-migration.bak");
    if (!QFileInfo::exists(backupPath) && !QFile::copy(legacyPath_, backupPath)) {
        setError(QStringLiteral("无法创建旧专注记录迁移备份"));
        return false;
    }
    if (!database_.transaction()) {
        setError(database_.lastError().text());
        return false;
    }
    for (const FocusRecord& record : records) {
        if (!insertRecord(record)) {
            database_.rollback();
            return false;
        }
    }
    if (!database_.commit()) {
        setError(database_.lastError().text());
        return false;
    }
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
        "violation_count, growth_stage, coins_earned, tag_id, focus_mode, grid_index) "
        "VALUES(:id, :plantType, :plannedMinutes, :actualSeconds, :startTimestamp, :status, :violationCount, "
        ":growthStage, :coinsEarned, :tagId, :focusMode, :gridIndex)"));
    bindRecord(query, record, true);
    if (!query.exec()) {
        setError(QStringLiteral("无法写入专注记录：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

std::optional<FocusRecord> DatabaseManager::readById(uint32_t recordId)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT * FROM focus_records WHERE record_id = ?"));
    query.addBindValue(recordId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return std::nullopt;
    }
    return query.next() ? std::optional<FocusRecord>(readRecord(query)) : std::nullopt;
}

bool DatabaseManager::updateById(uint32_t recordId, const FocusRecord& record)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE focus_records SET plant_type=:plantType, planned_minutes=:plannedMinutes, actual_seconds=:actualSeconds, "
        "start_timestamp=:startTimestamp, status=:status, violation_count=:violationCount, growth_stage=:growthStage, "
        "coins_earned=:coinsEarned, tag_id=:tagId, focus_mode=:focusMode, grid_index=:gridIndex WHERE record_id=:id"));
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
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT * FROM focus_records ORDER BY record_id"))) {
        setError(query.lastError().text());
        return records;
    }
    while (query.next()) records.push_back(readRecord(query));
    return records;
}

uint32_t DatabaseManager::count() const
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM focus_records")) || !query.next()) return 0;
    return query.value(0).toUInt();
}

bool DatabaseManager::recoverFromCrash()
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE focus_records SET status=?, growth_stage=4, coins_earned=0 WHERE status=?"));
    query.addBindValue(static_cast<uint>(FocusRecordStatus::Abandoned));
    query.addBindValue(static_cast<uint>(FocusRecordStatus::Running));
    if (!query.exec()) {
        setError(QStringLiteral("无法恢复中断专注：%1").arg(query.lastError().text()));
        return false;
    }
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
    return record;
}

void DatabaseManager::setError(const QString& error)
{
    lastError_ = error;
}
