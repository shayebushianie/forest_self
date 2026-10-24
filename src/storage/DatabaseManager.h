#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include "common/DatabaseCommon.h"

#include <QString>
#include <QSqlDatabase>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// SQLite-backed focus record repository. The constructor accepts the old
// sessions.dat path for compatibility and imports it once into sessions.sqlite.
class DatabaseManager {
public:
    explicit DatabaseManager(const std::string& legacyOrDatabasePath);
    ~DatabaseManager();

    bool open();
    void close();

    uint32_t append(const FocusRecord& record);
    std::optional<FocusRecord> readById(uint32_t recordId);
    bool updateById(uint32_t recordId, const FocusRecord& record);
    // Persists the terminal decision before applying it to the history row.
    bool finalizeRecord(uint32_t recordId, const FocusRecord& record);
    void failNextTerminalApplyForTesting() { failTerminalApply_ = true; }
    void failNextTerminalIntentForTesting() { failTerminalIntent_ = true; }
    std::vector<FocusRecord> getAllRecords();
    // On failure, keep the caller's previous value unchanged.
    bool getAllRecords(std::vector<FocusRecord>& records);
    uint32_t count() const;
    bool count(uint32_t& result) const;

    QString databasePath() const { return databasePath_; }
    QString lastError() const { return lastError_; }
    static constexpr int schemaVersion() { return 4; }
    static bool validateRecordSchema(const QSqlDatabase& database, int version, QString* error);

private:
    bool createSchema();
    bool migrateLegacyRecords();
    bool loadLegacyRecords(std::vector<FocusRecord>* records);
    bool recoverFromCrash();
    bool recoverTerminalIntents();
    bool applyTerminalIntent(uint32_t recordId);
    bool insertRecord(const FocusRecord& record);
    static FocusRecord readRecord(class QSqlQuery& query);
    void setError(const QString& error) const;
    bool ensureOpen() const;

    QString legacyPath_;
    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
    mutable QString lastError_;
    bool failTerminalApply_ = false;
    bool failTerminalIntent_ = false;
};

#endif // DATABASEMANAGER_H
