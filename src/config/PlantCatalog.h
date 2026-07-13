#ifndef PLANT_CATALOG_H
#define PLANT_CATALOG_H

#include <QColor>
#include <QString>
#include <QVector>
#include <cstdint>

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
    static const PlantDefinition& byType(uint32_t type);
    static bool isKnown(uint32_t type);
    static QString stageImagePath(uint32_t type, uint32_t stage, bool withered);
};

#endif // PLANT_CATALOG_H
