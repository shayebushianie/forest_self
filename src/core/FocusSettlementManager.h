#ifndef FOCUS_SETTLEMENT_MANAGER_H
#define FOCUS_SETTLEMENT_MANAGER_H

#include <QSet>
#include <QString>
#include <cstdint>

class DatabaseManager;

// Persists focus records that have reached a terminal state but whose
// cross-manager business effects still need to be applied.
class FocusSettlementManager final {
public:
    FocusSettlementManager(const QString& filePath, DatabaseManager& database);

    bool load();
    bool save() const;
    bool markPending(uint32_t recordId);
    bool markSettled(uint32_t recordId);
    bool isPending(uint32_t recordId) const;
    bool isSettled(uint32_t recordId) const;
    QVector<uint32_t> pendingRecordIds() const;

private:
    bool initializeFromExistingRecords();

    QString filePath_;
    DatabaseManager& database_;
    QSet<uint32_t> pending_;
    QSet<uint32_t> settled_;
};

#endif // FOCUS_SETTLEMENT_MANAGER_H
