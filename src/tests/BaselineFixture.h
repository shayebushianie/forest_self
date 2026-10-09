#ifndef BASELINEFIXTURE_H
#define BASELINEFIXTURE_H

#include "common/DatabaseCommon.h"
#include "storage/DatabaseManager.h"

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <cstdint>
#include <limits>

namespace BaselineFixture {

inline constexpr char kFixtureId[] = "baseline-data-v1";
inline constexpr int kAccountCount = 2;
inline constexpr int kRecordsPerAccount = 500;
inline constexpr int kRecordCount = kAccountCount * kRecordsPerAccount;

inline FocusRecord makeRecord(int accountIndex, int recordIndex, uint32_t tagId)
{
    FocusRecord record;
    record.plantType = static_cast<uint32_t>(recordIndex % 6);
    record.plannedMinutes = 25;
    record.startTimestamp = static_cast<uint64_t>(
        QDateTime(QDate(2024, 1, 1).addDays(accountIndex * kRecordsPerAccount + recordIndex),
                  QTime(9, 0), Qt::UTC).toSecsSinceEpoch());
    switch (recordIndex % 3) {
    case 0:
        record.status = FocusRecordStatus::Success;
        record.actualSeconds = 25 * 60;
        record.growthStage = 4;
        record.coinsEarned = 25;
        break;
    case 1:
        record.status = FocusRecordStatus::Abandoned;
        record.actualSeconds = 15 * 60;
        record.growthStage = 2;
        break;
    default:
        record.status = FocusRecordStatus::Failed;
        record.actualSeconds = 10 * 60;
        record.growthStage = 1;
        break;
    }
    record.tagId = tagId;
    record.focusMode = recordIndex % 2 == 0 ? PersistedFocusMode::Deep
                                            : PersistedFocusMode::GentleLegacy;
    record.gridIndex = static_cast<uint8_t>(recordIndex % 64);
    return record;
}

inline bool appendHistory(DatabaseManager& database, int accountIndex, uint32_t tagId)
{
    for (int recordIndex = 0; recordIndex < kRecordsPerAccount; ++recordIndex) {
        if (database.append(makeRecord(accountIndex, recordIndex, tagId)) ==
            std::numeric_limits<uint32_t>::max()) {
            return false;
        }
    }
    return true;
}

} // namespace BaselineFixture

#endif // BASELINEFIXTURE_H
