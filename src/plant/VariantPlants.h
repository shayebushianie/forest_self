#ifndef VARIANT_PLANTS_H
#define VARIANT_PLANTS_H

#include "plant/Cactus.h"
#include "plant/GinkgoTree.h"
#include "plant/OakTree.h"
#include "plant/PineTree.h"
#include "plant/Rose.h"
#include "plant/Sunflower.h"

#include <cstdint>
#include <string>
#include <utility>

class PlantVariantInfo {
public:
    const std::string& variantId() const { return variantId_; }
    const std::string& className() const { return className_; }
    const std::string& displayName() const { return displayName_; }
    const std::string& description() const { return description_; }
    const std::string& rarity() const { return rarity_; }
    const std::string& icon() const { return icon_; }
    const std::string& tintColor() const { return tintColor_; }
    const std::string& basePlantName() const { return basePlantName_; }
    uint32_t basePlantType() const { return basePlantType_; }

protected:
    PlantVariantInfo(std::string variantId,
                     std::string className,
                     std::string displayName,
                     std::string description,
                     std::string rarity,
                     std::string icon,
                     std::string tintColor,
                     std::string basePlantName,
                     uint32_t basePlantType)
        : variantId_(std::move(variantId)),
          className_(std::move(className)),
          displayName_(std::move(displayName)),
          description_(std::move(description)),
          rarity_(std::move(rarity)),
          icon_(std::move(icon)),
          tintColor_(std::move(tintColor)),
          basePlantName_(std::move(basePlantName)),
          basePlantType_(basePlantType)
    {
    }

private:
    std::string variantId_;
    std::string className_;
    std::string displayName_;
    std::string description_;
    std::string rarity_;
    std::string icon_;
    std::string tintColor_;
    std::string basePlantName_;
    uint32_t basePlantType_ = 0;
};

template <typename BasePlant>
class PlantVariant : public BasePlant, public PlantVariantInfo {
public:
    std::string getPlantName() const override { return className(); }

protected:
    PlantVariant(std::string variantId,
                 std::string className,
                 std::string displayName,
                 std::string description,
                 std::string rarity,
                 std::string icon,
                 std::string tintColor)
        : PlantVariantInfo(std::move(variantId),
                           std::move(className),
                           std::move(displayName),
                           std::move(description),
                           std::move(rarity),
                           std::move(icon),
                           std::move(tintColor),
                           inheritedPlantName(),
                           inheritedPlantType())
    {
    }

private:
    static std::string inheritedPlantName()
    {
        BasePlant plant;
        return plant.getPlantName();
    }

    static uint32_t inheritedPlantType()
    {
        BasePlant plant;
        return plant.getPlantType();
    }
};

class GoldenOak final : public PlantVariant<OakTree> { public: GoldenOak(); };
class SilverOak final : public PlantVariant<OakTree> { public: SilverOak(); };
class RedPine final : public PlantVariant<PineTree> { public: RedPine(); };
class BluePine final : public PlantVariant<PineTree> { public: BluePine(); };
class PinkRose final : public PlantVariant<Rose> { public: PinkRose(); };
class PurpleRose final : public PlantVariant<Rose> { public: PurpleRose(); };
class GoldenGinkgo final : public PlantVariant<GinkgoTree> { public: GoldenGinkgo(); };
class RedGinkgo final : public PlantVariant<GinkgoTree> { public: RedGinkgo(); };
class GoldenSunflower final : public PlantVariant<Sunflower> { public: GoldenSunflower(); };
class BlackSunflower final : public PlantVariant<Sunflower> { public: BlackSunflower(); };
class GemCactus final : public PlantVariant<Cactus> { public: GemCactus(); };
class FlameCactus final : public PlantVariant<Cactus> { public: FlameCactus(); };

#endif // VARIANT_PLANTS_H
