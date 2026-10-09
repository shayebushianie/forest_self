#include "config/PlantCatalog.h"

#include <QCoreApplication>
#include <QImage>
#include <QSet>
#include <QFile>
#include "plant/AbstractPlant.h"
#include "plant/PlantFactory.h"
#include "plant/VariantPlants.h"
#include <array>
#include <limits>

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
    const QStringList legacyNames = {"OakTree", "PineTree", "Rose", "GinkgoTree", "Sunflower", "Cactus"};
    if (!expect(plants.size() >= legacyNames.size(), "catalog retains all legacy plants")) return false;
    for (int type = 0; type < legacyNames.size(); ++type) {
        if (!expect(PlantCatalog::byType(static_cast<uint32_t>(type)).internalName == legacyNames[type],
                    "legacy disk IDs retain the same identities")) return false;
    }

    QSet<uint32_t> types;
    QSet<QString> names;
    for (const PlantDefinition& plant : plants) {
        const auto implementation = PlantFactory::create(plant.type);
        if (!expect(implementation && implementation->getPlantType() == plant.type &&
                    QString::fromStdString(implementation->getPlantName()) == plant.internalName,
                    "every registered plant has matching factory behavior and identity")) return false;
        if (!expect(plant.type < 32, "catalog fits the current persisted ownership mask")) return false;
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
        for (uint32_t stage = 0; stage <= 3; ++stage) {
            if (!expect(QFile::exists(PlantCatalog::stageImagePath(plant.type, stage, false)),
                        "registered growth image exists in resources")) return false;
        }
        if (!expect(QFile::exists(PlantCatalog::stageImagePath(plant.type, 3, true)),
                    "registered withered image exists in resources")) return false;
        types.insert(plant.type);
        names.insert(plant.internalName);
    }
    return expect(types.size() == plants.size() && names.size() == plants.size(),
                  "catalog plant identities are unique") &&
           expect(!PlantCatalog::isKnown(9999) && !PlantFactory::create(9999), "unknown plant type is rejected") &&
           expect(PlantCatalog::byType(9999).type == std::numeric_limits<uint32_t>::max() &&
                  PlantCatalog::byType(9999).internalName.isEmpty() &&
                  PlantCatalog::byType(9999).iconPath.isEmpty() &&
                  PlantCatalog::stageImagePath(9999, 0, false).isEmpty() &&
                  PlantCatalog::stageImagePath(9999, 3, true).isEmpty(),
                  "unknown plant never masquerades as a registered plant or seed asset");
}

bool testVariantImages()
{
    const GoldenOak goldenOak;
    const SilverOak silverOak;
    const RedPine redPine;
    const BluePine bluePine;
    const PinkRose pinkRose;
    const PurpleRose purpleRose;
    const GoldenGinkgo goldenGinkgo;
    const RedGinkgo redGinkgo;
    const GoldenSunflower goldenSunflower;
    const BlackSunflower blackSunflower;
    const GemCactus gemCactus;
    const FlameCactus flameCactus;
    const std::array<const PlantVariantInfo*, 12> variants = {
        &goldenOak, &silverOak, &redPine, &bluePine, &pinkRose, &purpleRose,
        &goldenGinkgo, &redGinkgo, &goldenSunflower, &blackSunflower, &gemCactus, &flameCactus,
    };
    const std::array<QString, 12> expectedIds = {
        QStringLiteral("golden_oak"), QStringLiteral("silver_oak"), QStringLiteral("red_pine"),
        QStringLiteral("blue_pine"), QStringLiteral("pink_rose"), QStringLiteral("purple_rose"),
        QStringLiteral("golden_ginkgo"), QStringLiteral("red_ginkgo"),
        QStringLiteral("golden_sunflower"), QStringLiteral("black_sunflower"),
        QStringLiteral("gem_cactus"), QStringLiteral("flame_cactus"),
    };
    const std::array<uint32_t, 12> expectedBaseTypes = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    QSet<QString> imagePaths;

    for (std::size_t i = 0; i < variants.size(); ++i) {
        const PlantVariantInfo& variant = *variants[i];
        const QString variantId = QString::fromStdString(variant.variantId());
        const QString imagePath = QString::fromStdString(variant.imagePath());
        if (!expect(variantId == expectedIds[i], "variant IDs and order remain stable")) return false;
        if (!expect(variant.basePlantType() == expectedBaseTypes[i] &&
                        PlantCatalog::isKnown(variant.basePlantType()) &&
                        QString::fromStdString(variant.basePlantName()) ==
                            PlantCatalog::byType(expectedBaseTypes[i]).internalName,
                    "variant retains its registered base plant")) return false;
        if (!expect(imagePath == QStringLiteral(":/images/variants/%1.png").arg(variantId) &&
                        !imagePaths.contains(imagePath),
                    "variant image paths are derived from unique stable IDs")) return false;
        imagePaths.insert(imagePath);
        if (!expect(QFile::exists(imagePath), "variant image is registered as a resource")) return false;

        QImage image(imagePath);
        if (!expect(!image.isNull(), "variant PNG resource is readable")) return false;
        image = image.convertToFormat(QImage::Format_ARGB32);
        if (!expect(image.width() == image.height() && image.width() >= 512,
                    "variant PNG is square and has sufficient source resolution")) return false;
        if (!expect(qAlpha(image.pixel(0, 0)) == 0 && qAlpha(image.pixel(image.width() - 1, 0)) == 0 &&
                        qAlpha(image.pixel(0, image.height() - 1)) == 0 &&
                        qAlpha(image.pixel(image.width() - 1, image.height() - 1)) == 0,
                    "variant PNG has transparent corners")) return false;

        qsizetype visiblePixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(line[x]) > 0) ++visiblePixels;
            }
        }
        const qsizetype totalPixels = static_cast<qsizetype>(image.width()) * image.height();
        if (!expect(visiblePixels > 0 && visiblePixels < totalPixels,
                    "variant PNG has visible content and transparent pixels")) return false;
    }
    return expect(imagePaths.size() == static_cast<int>(variants.size()),
                  "all variant image paths are unique");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    return testCatalogCompatibility() && testVariantImages() ? 0 : 1;
}
