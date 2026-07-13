#include "core/TagManager.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>

namespace {

constexpr quint32 kTagMagic = 0x46544731u; // FTG1
constexpr quint16 kTagVersion = 1;
constexpr quint32 kMaxTagCount = 10000;

bool decodeTags(const QByteArray& bytes, QVector<TagDef>& tags)
{
    if (bytes.isEmpty()) {
        tags.clear();
        return true;
    }
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > kMaxTagCount) return false;

    QVector<TagDef> loaded;
    loaded.reserve(static_cast<int>(count));
    for (quint32 i = 0; i < count; ++i) {
        TagDef tag;
        stream >> tag.id >> tag.name >> tag.color;
        if (stream.status() != QDataStream::Ok) return false;
        loaded.append(tag);
    }
    if (!buffer.atEnd()) return false;
    tags = std::move(loaded);
    return true;
}

QByteArray encodeTags(const QVector<TagDef>& tags)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream << static_cast<quint32>(tags.size());
    for (const TagDef& tag : tags) stream << tag.id << tag.name << tag.color;
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

} // namespace

TagManager::TagManager(const QString& filePath) : m_filePath(filePath) {}

bool TagManager::load()
{
    const auto result = StorageEnvelope::read(m_filePath, kTagMagic);
    if (result.state == StorageEnvelope::ReadState::Missing) {
        m_tags.clear();
        return true;
    }

    QByteArray bytes;
    if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile file(m_filePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        bytes = file.readAll();
        file.close();
    } else if (result.hasPayload() && result.version == kTagVersion) {
        bytes = result.payload;
    } else {
        return false;
    }
    if (!decodeTags(bytes, m_tags)) return false;
    return result.state == StorageEnvelope::ReadState::Legacy ? save() : true;
}

bool TagManager::save()
{
    return StorageEnvelope::write(m_filePath, kTagMagic, kTagVersion, encodeTags(m_tags));
}

TagDef TagManager::tag(uint32_t id) const
{
    for (const TagDef& item : m_tags)
        if (item.id == id) return item;
    return {0, QString::fromUtf8("\xE6\x97\xA0\xE6\xA0\x87\xE7\xAD\xBE"), "#999999"};
}

QString TagManager::name(uint32_t id) const
{
    for (const TagDef& item : m_tags)
        if (item.id == id) return item.name;
    return QString::fromUtf8("\xE6\x97\xA0\xE6\xA0\x87\xE7\xAD\xBE");
}

uint32_t TagManager::add(const QString& name, const QString& color)
{
    uint32_t id = 1;
    for (const TagDef& tag : m_tags)
        if (tag.id >= id) id = tag.id + 1;
    m_tags.append({id, name, color});
    return id;
}

bool TagManager::rename(uint32_t id, const QString& name)
{
    for (TagDef& tag : m_tags)
        if (tag.id == id) { tag.name = name; return true; }
    return false;
}

bool TagManager::recolor(uint32_t id, const QString& color)
{
    for (TagDef& tag : m_tags)
        if (tag.id == id) { tag.color = color; return true; }
    return false;
}

bool TagManager::remove(uint32_t id)
{
    for (int i = 0; i < m_tags.size(); ++i)
        if (m_tags[i].id == id) { m_tags.removeAt(i); return true; }
    return false;
}
