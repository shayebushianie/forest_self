#include "config/PlantCatalog.h"

#include <QCoreApplication>
#include <QSet>

#include <iostream>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        return false;
    }
    return true;
}

bool testCatalogCompatibility()
{
    const auto& plants = PlantCatalog::all();
    if (!expect(plants.size() == 6, "plant catalog retains the six store plants")) return false;

    QSet<uint32_t> types;
    QSet<QString> names;
    for (const PlantDefinition& plant : plants) {
        if (!expect(PlantCatalog::isKnown(plant.type), "catalog type resolves")) return false;
        if (!expect(PlantCatalog::byType(plant.type).internalName == plant.internalName,
                    "catalog lookup preserves plant identity")) return false;
        if (!expect(!plant.displayName.isEmpty() && !plant.internalName.isEmpty() &&
                    !plant.assetKey.isEmpty() && !plant.iconPath.isEmpty() && plant.chartColor.isValid(),
                    "catalog entries provide complete presentation data")) return false;
        if (!expect(PlantCatalog::stageImagePath(plant.type, 3, false) == plant.iconPath &&
                    PlantCatalog::stageImagePath(plant.type, 1, false).contains(plant.assetKey) &&
                    PlantCatalog::stageImagePath(plant.type, 2, false).contains(plant.assetKey) &&
                    PlantCatalog::stageImagePath(plant.type, 1, true).contains(plant.assetKey),
                    "catalog owns each plant stage image path")) return false;
        types.insert(plant.type);
        names.insert(plant.internalName);
    }
    return expect(types.size() == plants.size() && names.size() == plants.size(),
                  "catalog plant identities are unique") &&
           expect(!PlantCatalog::isKnown(9999), "unknown plant type is rejected");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    return testCatalogCompatibility() ? 0 : 1;
}
