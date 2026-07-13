#ifndef PLANT_FACTORY_H
#define PLANT_FACTORY_H

#include <cstdint>
#include <memory>

class AbstractPlant;

// The only bridge from persistent PlantCatalog types to concrete plant behavior.
class PlantFactory final {
public:
    static std::unique_ptr<AbstractPlant> create(uint32_t type);
};

#endif // PLANT_FACTORY_H
