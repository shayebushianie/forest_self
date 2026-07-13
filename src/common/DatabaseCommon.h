#ifndef DATABASECOMMON_H
#define DATABASECOMMON_H

#include <cstdint>
#include <cstring>

// Numeric values are part of the legacy binary record format and SQLite
// migration contract. Never reorder or renumber them.
enum class FocusRecordStatus : uint32_t {
    Success = 0,
    Failed = 1,
    Abandoned = 2,
    Running = 3
};

enum class PersistedFocusMode : uint8_t {
    Deep = 0,
    GentleLegacy = 1
};

inline bool isTerminalFocusStatus(FocusRecordStatus status)
{
    return status == FocusRecordStatus::Success || status == FocusRecordStatus::Failed ||
           status == FocusRecordStatus::Abandoned;
}

/**
 * Fixed-size focus session record.
 *
 * The storage layer writes this struct directly to a binary file, so this
 * layout is part of the on-disk format. Keep every field fixed-width and
 * update the static_assert if the format ever changes intentionally.
 */
#pragma pack(push, 1)
struct FocusRecord {
    uint32_t recordId;        // 0-3: primary key, assigned from file offset.
    uint32_t plantType;       // 4-7: stable PlantCatalog type.
    uint32_t plannedMinutes;  // 8-11: planned countdown length; 0 for stopwatch.
    uint32_t actualSeconds;   // 12-15: persisted effective focus seconds.
    uint64_t startTimestamp;  // 16-23: Unix epoch seconds at session start.
    FocusRecordStatus status; // 24-27: persisted terminal/running state.
    uint32_t violationCount;  // 28-31: number of detected focus violations.
    uint32_t growthStage;     // 32-35: final visual plant growth stage.
    uint32_t coinsEarned;     // 36-39: coins granted by this session.
    uint32_t tagId;           // 40-43: category tag id.
    PersistedFocusMode focusMode; // 44: 0=deep, 1=legacy gentle.
    uint8_t  gridIndex;       // 45: garden grid cell, 0-63 when assigned.
    char     reserved[18];    // 46-63: reserved padding for future fields.

    // Zero the whole record so reserved bytes never contain random stack data.
    FocusRecord()
    {
        std::memset(this, 0, sizeof(FocusRecord));
    }
};
#pragma pack(pop)

static_assert(sizeof(FocusRecord) == 64,
              "FocusRecord must be exactly 64 bytes for fixed-length random access");

// Local account record. Passwords are stored as fixed-size hashes, not raw text.
#pragma pack(push, 1)
struct UserRecord {
    uint32_t userId;
    char     username[24];
    char     passwordHash[32];
    char     reserved[4];

    UserRecord() { std::memset(this, 0, sizeof(UserRecord)); }

    void pack(char buffer[64]) const {
        std::memcpy(buffer, this, 64);
    }

    void unpack(const char buffer[64]) {
        std::memcpy(this, buffer, 64);
    }
};
#pragma pack(pop)

static_assert(sizeof(UserRecord) == 64,
              "UserRecord must be exactly 64 bytes");

#endif // DATABASECOMMON_H
