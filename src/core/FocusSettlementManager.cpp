#include "core/FocusSettlementManager.h"

#include "storage/DatabaseManager.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QDataStream>

#include <algorithm>

namespace {

constexpr quint32 kSettlementMagic = 0x46535431u; // FST1
constexpr quint16 kSettlementVersion = 2;

QByteArray encodeIds(const QSet<uint32_t>& pending, const QSet<uint32_t>& settled, const QSet<uint32_t>& confirmed)
{
    QList<uint32_t> pendingIds = pending.values();
    QList<uint32_t> settledIds = settled.values();
    std::sort(pendingIds.begin(), pendingIds.end());
    std::sort(settledIds.begin(), settledIds.end());

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream << static_cast<quint32>(pendingIds.size());
    for (uint32_t id : pendingIds) stream << id;
    stream << static_cast<quint32>(settledIds.size());
    for (uint32_t id : settledIds) stream << id;
    auto confirmedIds = confirmed.values();
    std::sort(confirmedIds.begin(), confirmedIds.end());
    stream << static_cast<quint32>(confirmedIds.size());
    for (uint32_t id : confirmedIds) stream << id;
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

bool decodeIds(const QByteArray& bytes, quint16 version, QSet<uint32_t>& pending, QSet<uint32_t>& settled,
               QSet<uint32_t>& confirmed)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > 1000000u) return false;
    QSet<uint32_t> loadedPending;
    for (quint32 i = 0; i < count; ++i) {
        uint32_t id = 0;
        stream >> id;
        if (stream.status() != QDataStream::Ok) return false;
        loadedPending.insert(id);
    }
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > 1000000u) return false;
    QSet<uint32_t> loadedSettled;
    for (quint32 i = 0; i < count; ++i) {
        uint32_t id = 0;
        stream >> id;
        if (stream.status() != QDataStream::Ok) return false;
        loadedSettled.insert(id);
    }
    QSet<uint32_t> loadedConfirmed;
    if (version == kSettlementVersion) {
        stream >> count;
        if (stream.status() != QDataStream::Ok || count > 1000000u) return false;
        for (quint32 i = 0; i < count; ++i) {
            uint32_t id = 0;
            stream >> id;
            if (stream.status() != QDataStream::Ok || !loadedSettled.contains(id) || loadedConfirmed.contains(id)) return false;
            loadedConfirmed.insert(id);
        }
    }
    if (!buffer.atEnd()) return false;
    pending = std::move(loadedPending);
    settled = std::move(loadedSettled);
    confirmed = std::move(loadedConfirmed);
    return true;
}

}

FocusSettlementManager::FocusSettlementManager(const QString& filePath, DatabaseManager& database)
    : filePath_(filePath), database_(database)
{
}

bool FocusSettlementManager::load()
{
    std::vector<FocusRecord> records;
    if (!database_.getAllRecords(records)) return false;
    const auto result = StorageEnvelope::read(filePath_, kSettlementMagic);
    if (result.state == StorageEnvelope::ReadState::Missing) {
        return initializeFromExistingRecords();
    }
    if (!result.hasPayload() || result.version < 1 || result.version > kSettlementVersion) return false;
    if (!decodeIds(result.payload, result.version, pending_, settled_, confirmed_)) return false;
    bool changed = result.version != kSettlementVersion;
    for (const auto& record : records) {
        if (isTerminalFocusStatus(record.status) && !settled_.contains(record.recordId) && !pending_.contains(record.recordId)) {
            pending_.insert(record.recordId);
            changed = true;
        }
    }
    return !changed || save();
}

bool FocusSettlementManager::save() const
{
    return StorageEnvelope::write(filePath_, kSettlementMagic, kSettlementVersion,
                                  encodeIds(pending_, settled_, confirmed_));
}

bool FocusSettlementManager::markPending(uint32_t recordId)
{
    if (settled_.contains(recordId) || pending_.contains(recordId)) return true;
    pending_.insert(recordId);
    if (save()) return true;
    pending_.remove(recordId);
    return false;
}

bool FocusSettlementManager::markSettled(uint32_t recordId)
{
    if (settled_.contains(recordId)) return true;
    const bool wasPending = pending_.remove(recordId);
    settled_.insert(recordId);
    confirmed_.insert(recordId);
    if (save()) return true;
    settled_.remove(recordId);
    confirmed_.remove(recordId);
    if (wasPending) pending_.insert(recordId);
    return false;
}

bool FocusSettlementManager::isPending(uint32_t recordId) const
{
    return pending_.contains(recordId);
}

bool FocusSettlementManager::isSettled(uint32_t recordId) const
{
    return settled_.contains(recordId);
}

QVector<uint32_t> FocusSettlementManager::pendingRecordIds() const
{
    QVector<uint32_t> ids = pending_.values().toVector();
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool FocusSettlementManager::initializeFromExistingRecords()
{
    std::vector<FocusRecord> records;
    if (!database_.getAllRecords(records)) return false;
    pending_.clear();
    settled_.clear();
    confirmed_.clear();
    for (const FocusRecord& record : records) {
        if (record.status != FocusRecordStatus::Running) settled_.insert(record.recordId);
    }
    return save();
}

QVector<uint32_t> FocusSettlementManager::confirmedRecordIds() const
{
    auto ids = confirmed_.values().toVector();
    std::sort(ids.begin(), ids.end());
    return ids;
}
