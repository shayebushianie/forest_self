#ifndef FLOWER_H
#define FLOWER_H

#include "plant/AbstractPlant.h"
#include <string>

class Flower : public AbstractPlant {
public:
    void grow(int actualSeconds, int totalSeconds) override;
    void wither() override;

    uint32_t getGrowthStage() const override;
    uint32_t getPlantType() const override;
    std::string getStageName() const override;

    uint32_t petalCount() const { return petalCount_; }
    std::string bloomColor() const { return bloomColor_; }

protected:
    uint32_t petalCount_ = 0;
    std::string bloomColor_ = "unknown";
};

#endif // FLOWER_H
