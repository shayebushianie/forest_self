#ifndef PLANT_CATALOG_H
#define PLANT_CATALOG_H

#include <QColor>
#include <QString>
#include <QVector>
#include <cstdint>

// Persistent IDs must not be renumbered when catalog entries are added or reordered.
namespace PlantTypeId {
inline constexpr uint32_t Oak = 0;
inline constexpr uint32_t Pine = 1;
inline constexpr uint32_t Rose = 2;
inline constexpr uint32_t Ginkgo = 3;
inline constexpr uint32_t Sunflower = 4;
inline constexpr uint32_t Cactus = 5;
}

struct PlantDefinition {
    uint32_t type = 0;
    QString displayName;
    QString internalName;
    QString assetKey;
    QString iconPath;
    uint32_t storeCost = 0;
    QColor chartColor;
};

// Stable plant registry shared by store, selection, statistics, and rendering.
class PlantCatalog final {
public:
    static const QVector<PlantDefinition>& all();
    // Unknown IDs resolve to a placeholder without plant assets, never another catalog plant.
    static const PlantDefinition& byType(uint32_t type);
    static bool isKnown(uint32_t type);
    static QString stageImagePath(uint32_t type, uint32_t stage, bool withered);
};

#endif // PLANT_CATALOG_H
