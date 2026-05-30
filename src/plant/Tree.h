#ifndef TREE_H
#define TREE_H

#include "plant/AbstractPlant.h"

class Tree : public AbstractPlant {
public:
    void grow(int actualSeconds, int totalSeconds) override;
    void wither() override;

    uint32_t getGrowthStage() const override;
    uint32_t getPlantType() const override;
    std::string getStageName() const override;

    uint32_t height() const { return height_; }
    uint32_t fruitCount() const { return fruitCount_; }

protected:
    uint32_t height_ = 0;
    uint32_t fruitCount_ = 0;
};

#endif // TREE_H
