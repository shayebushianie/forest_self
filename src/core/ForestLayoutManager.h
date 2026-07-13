#ifndef FORESTLAYOUTMANAGER_H
#define FORESTLAYOUTMANAGER_H

#include <QString>
#include <QVector>
#include <cstdint>

struct ForestPlacement {
    int32_t recordId;
    int32_t posX;
    int32_t posY;
};

class ForestLayoutManager {
public:
    explicit ForestLayoutManager(const QString& filePath);

    bool load();
    bool save();

    void place(int32_t recordId, int32_t posX, int32_t posY);
    void movePlacement(int32_t recordId, int32_t posX, int32_t posY);
    void remove(int32_t recordId);
    void clear();

    bool hasPlacement(int32_t recordId) const;
    const ForestPlacement* findPlacement(int32_t recordId) const;
    const QVector<ForestPlacement>& placements() const { return m_placements; }
    QVector<ForestPlacement>& placements() { return m_placements; }

private:
    int indexOf(int32_t recordId) const;

    QString m_filePath;
    QVector<ForestPlacement> m_placements;
};

#endif
