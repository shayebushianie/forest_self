#include "plant/PlantFactory.h"

#include "config/PlantCatalog.h"
#include "plant/Cactus.h"
#include "plant/GinkgoTree.h"
#include "plant/OakTree.h"
#include "plant/PineTree.h"
#include "plant/Rose.h"
#include "plant/Sunflower.h"

std::unique_ptr<AbstractPlant> PlantFactory::create(uint32_t type)
{
    if (!PlantCatalog::isKnown(type)) return nullptr;

    switch (type) {
    case PlantTypeId::Oak: return std::make_unique<OakTree>();
    case PlantTypeId::Pine: return std::make_unique<PineTree>();
    case PlantTypeId::Rose: return std::make_unique<Rose>();
    case PlantTypeId::Ginkgo: return std::make_unique<GinkgoTree>();
    case PlantTypeId::Sunflower: return std::make_unique<Sunflower>();
    case PlantTypeId::Cactus: return std::make_unique<Cactus>();
    default: return nullptr;
    }
}
