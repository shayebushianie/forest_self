#include "core/GachaManager.h"
#include "core/CoinManager.h"
#include "plant/VariantPlants.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QRandomGenerator>

namespace {

constexpr quint32 kVariantMagic = 0x46564131u; // FVA1
constexpr quint16 kVariantVersion = 1;

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.c_str(), static_cast<qsizetype>(value.size()));
}

GachaManager::VariantDef makeVariantDef(const PlantVariantInfo& plant)
{
    GachaManager::VariantDef def;
    def.id = fromUtf8(plant.variantId());
    def.basePlant = fromUtf8(plant.basePlantName());
    def.basePlantType = plant.basePlantType();
    def.displayName = fromUtf8(plant.displayName());
    def.description = fromUtf8(plant.description());
    def.icon = fromUtf8(plant.icon());
    def.rarity = fromUtf8(plant.rarity());
    def.tintColor = fromUtf8(plant.tintColor());
    return def;
}

bool readLegacyUnlocks(const QByteArray& bytes, QVector<GachaManager::VariantDef>& variants)
{
    if (bytes.isEmpty()) return true;
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    if (bytes.size() == static_cast<qsizetype>(sizeof(quint32))) {
        quint32 mask = 0;
        stream >> mask;
        if (stream.status() != QDataStream::Ok) return false;
        for (int i = 0; i < variants.size() && i < 32; ++i) {
            variants[i].unlocked = (mask & (1u << i)) != 0;
        }
        return true;
    }

    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > 1000) return false;
    for (quint32 i = 0; i < count; ++i) {
        QByteArray idBytes;
        quint8 flag = 0;
        stream >> idBytes >> flag;
        if (stream.status() != QDataStream::Ok) return false;
        for (GachaManager::VariantDef& variant : variants) {
            if (variant.id == QString::fromUtf8(idBytes)) {
                variant.unlocked = flag != 0;
                break;
            }
        }
    }
    return buffer.atEnd();
}

} // namespace

GachaManager::GachaManager(const QString& filePath)
    : m_filePath(filePath)
{
    m_variants = {
        makeVariantDef(GoldenOak()), makeVariantDef(SilverOak()),
        makeVariantDef(RedPine()), makeVariantDef(BluePine()),
        makeVariantDef(PinkRose()), makeVariantDef(PurpleRose()),
        makeVariantDef(GoldenGinkgo()), makeVariantDef(RedGinkgo()),
        makeVariantDef(GoldenSunflower()), makeVariantDef(BlackSunflower()),
        makeVariantDef(GemCactus()), makeVariantDef(FlameCactus()),
    };
    load();
}

GachaManager::~GachaManager() = default;

bool GachaManager::load()
{
    const auto result = StorageEnvelope::read(m_filePath, kVariantMagic);
    if (result.state == StorageEnvelope::ReadState::Missing) return save();

    QByteArray bytes;
    if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile file(m_filePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        bytes = file.readAll();
        file.close();
    } else if (result.hasPayload() && result.version == kVariantVersion) {
        bytes = result.payload;
    } else {
        return false;
    }
    if (!readLegacyUnlocks(bytes, m_variants)) return false;
    return result.state == StorageEnvelope::ReadState::Legacy ? save() : true;
}

bool GachaManager::save()
{
    QByteArray payload;
    QBuffer buffer(&payload);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream << static_cast<quint32>(unlockMask());
    return stream.status() == QDataStream::Ok &&
           StorageEnvelope::write(m_filePath, kVariantMagic, kVariantVersion, payload);
}

QStringList GachaManager::unlockedVariantIds() const
{
    QStringList list;
    for (const VariantDef& variant : m_variants)
        if (variant.unlocked) list << variant.id;
    return list;
}

int GachaManager::unlockedCount() const
{
    int count = 0;
    for (const VariantDef& variant : m_variants)
        if (variant.unlocked) ++count;
    return count;
}

GachaManager::PullResult GachaManager::pull(CoinManager& coins)
{
    PullResult result;
    if (!coins.spend(pullCost())) return result;

    const int idx = QRandomGenerator::global()->bounded(m_variants.size());
    const VariantDef chosen = m_variants[idx];
    result.success = true;
    result.variantId = chosen.id;
    result.displayName = chosen.displayName;
    result.description = chosen.description;
    result.icon = chosen.icon;
    result.rarity = chosen.rarity;

    if (chosen.unlocked) {
        result.isNew = false;
        result.coinRefund = duplicateCoinRefund();
        if (!coins.refund(duplicateCoinRefund())) result.success = false;
        return result;
    }

    m_variants[idx].unlocked = true;
    if (!save()) {
        m_variants[idx].unlocked = false;
        coins.refund(pullCost());
        result.success = false;
        return result;
    }
    result.isNew = true;
    return result;
}

bool GachaManager::isUnlocked(const QString& variantId) const
{
    const int idx = indexOf(variantId);
    return idx >= 0 && m_variants[idx].unlocked;
}

bool GachaManager::unlockVariant(const QString& variantId)
{
    const int idx = indexOf(variantId);
    if (idx < 0 || m_variants[idx].unlocked) return false;
    m_variants[idx].unlocked = true;
    if (save()) return true;
    m_variants[idx].unlocked = false;
    return false;
}

int GachaManager::indexOf(const QString& variantId) const
{
    for (int i = 0; i < m_variants.size(); ++i)
        if (m_variants[i].id == variantId) return i;
    return -1;
}

uint32_t GachaManager::unlockMask() const
{
    uint32_t mask = 0;
    for (int i = 0; i < m_variants.size() && i < 32; ++i)
        if (m_variants[i].unlocked) mask |= (1u << i);
    return mask;
}

void GachaManager::applyUnlockMask(uint32_t mask)
{
    for (int i = 0; i < m_variants.size() && i < 32; ++i)
        m_variants[i].unlocked = (mask & (1u << i)) != 0;
}
