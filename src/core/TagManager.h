#ifndef TAGMANAGER_H
#define TAGMANAGER_H

#include <QString>
#include <QVector>
#include <cstdint>

struct TagDef {
    uint32_t id;
    QString  name;
    QString  color;
};

class TagManager {
public:
    explicit TagManager(const QString& filePath);

    bool load();
    bool save();

    const QVector<TagDef>& all() const { return m_tags; }
    TagDef tag(uint32_t id) const;
    QString name(uint32_t id) const;
    int count() const { return m_tags.size(); }

    uint32_t add(const QString& name, const QString& color);
    bool rename(uint32_t id, const QString& name);
    bool recolor(uint32_t id, const QString& color);
    bool remove(uint32_t id);

private:
    QString m_filePath;
    QVector<TagDef> m_tags;
};

#endif
