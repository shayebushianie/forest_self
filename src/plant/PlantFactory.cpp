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
    case 0: return std::make_unique<OakTree>();
    case 1: return std::make_unique<PineTree>();
    case 2: return std::make_unique<Rose>();
    case 3: return std::make_unique<GinkgoTree>();
    case 4: return std::make_unique<Sunflower>();
    case 5: return std::make_unique<Cactus>();
    default: return nullptr;
    }
}
