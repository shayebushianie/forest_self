#include "ForestLayoutManager.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>

namespace {

constexpr quint32 kForestLayoutMagic = 0x46464C31u; // FFL1
constexpr quint16 kForestLayoutVersion = 1;
constexpr quint32 kMaxPlacementCount = 100000;

bool decodePlacements(const QByteArray& bytes, QVector<ForestPlacement>& placements)
{
    if (bytes.isEmpty()) {
        placements.clear();
        return true;
    }
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > kMaxPlacementCount) return false;

    QVector<ForestPlacement> loaded;
    loaded.reserve(static_cast<int>(count));
    for (quint32 i = 0; i < count; ++i) {
        ForestPlacement placement;
        stream >> placement.recordId >> placement.posX >> placement.posY;
        if (stream.status() != QDataStream::Ok) return false;
        loaded.append(placement);
    }
    if (!buffer.atEnd()) return false;
    placements = std::move(loaded);
    return true;
}

QByteArray encodePlacements(const QVector<ForestPlacement>& placements)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream << static_cast<quint32>(placements.size());
    for (const ForestPlacement& placement : placements) {
        stream << placement.recordId << placement.posX << placement.posY;
    }
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

} // namespace

ForestLayoutManager::ForestLayoutManager(const QString& filePath)
    : m_filePath(filePath) {}

bool ForestLayoutManager::load()
{
    const auto result = StorageEnvelope::read(m_filePath, kForestLayoutMagic);
    if (result.state == StorageEnvelope::ReadState::Missing) return true;

    QByteArray bytes;
    if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile file(m_filePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        bytes = file.readAll();
        file.close();
    } else if (result.hasPayload() && result.version == kForestLayoutVersion) {
        bytes = result.payload;
    } else {
        return false;
    }
    if (!decodePlacements(bytes, m_placements)) return false;
    return result.state == StorageEnvelope::ReadState::Legacy ? save() : true;
}

bool ForestLayoutManager::save()
{
    return StorageEnvelope::write(m_filePath, kForestLayoutMagic, kForestLayoutVersion,
                                  encodePlacements(m_placements));
}

void ForestLayoutManager::place(int32_t recordId, int32_t posX, int32_t posY)
{
    const int idx = indexOf(recordId);
    if (idx >= 0) {
        m_placements[idx].posX = posX;
        m_placements[idx].posY = posY;
    } else {
        m_placements.append({recordId, posX, posY});
    }
}

void ForestLayoutManager::movePlacement(int32_t recordId, int32_t posX, int32_t posY)
{
    const int idx = indexOf(recordId);
    if (idx >= 0) {
        m_placements[idx].posX = posX;
        m_placements[idx].posY = posY;
    }
}

void ForestLayoutManager::remove(int32_t recordId)
{
    const int idx = indexOf(recordId);
    if (idx >= 0) m_placements.removeAt(idx);
}

void ForestLayoutManager::clear()
{
    m_placements.clear();
}

bool ForestLayoutManager::hasPlacement(int32_t recordId) const
{
    return indexOf(recordId) >= 0;
}

const ForestPlacement* ForestLayoutManager::findPlacement(int32_t recordId) const
{
    for (const ForestPlacement& placement : m_placements) {
        if (placement.recordId == recordId) return &placement;
    }
    return nullptr;
}

int ForestLayoutManager::indexOf(int32_t recordId) const
{
    for (int i = 0; i < m_placements.size(); ++i) {
        if (m_placements[i].recordId == recordId) return i;
    }
    return -1;
}
