#ifndef CACTUS_H
#define CACTUS_H

#include "plant/AbstractPlant.h"
#include <string>

class Cactus : public AbstractPlant {
public:
    void grow(int actualSeconds, int totalSeconds) override;
    void wither() override;

    uint32_t getGrowthStage() const override;
    uint32_t getPlantType() const override;
    std::string getPlantName() const override;
    std::string getStageName() const override;

    uint32_t height() const { return height_; }
    uint32_t spineCount() const { return spineCount_; }

protected:
    uint32_t height_ = 0;
    uint32_t spineCount_ = 0;
};

#endif // CACTUS_H
