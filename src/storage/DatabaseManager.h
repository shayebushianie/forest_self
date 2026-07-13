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
    std::vector<FocusRecord> getAllRecords();
    uint32_t count() const;

    QString databasePath() const { return databasePath_; }
    QString lastError() const { return lastError_; }
    static constexpr int schemaVersion() { return 1; }

private:
    bool createSchema();
    bool migrateLegacyRecords();
    bool loadLegacyRecords(std::vector<FocusRecord>* records);
    bool recoverFromCrash();
    bool insertRecord(const FocusRecord& record);
    static FocusRecord readRecord(class QSqlQuery& query);
    void setError(const QString& error);

    QString legacyPath_;
    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
    QString lastError_;
};

#endif // DATABASEMANAGER_H
