#include "plant/Tree.h"

void Tree::grow(int actualSeconds, int totalSeconds)
{
    if (totalSeconds <= 0) return;

    float progress = static_cast<float>(actualSeconds) / totalSeconds;

    if (progress >= 1.0f) {
        growthStage_ = 3;
        height_ = 100;
        fruitCount_ = 5;
    } else if (progress >= 0.6f) {
        growthStage_ = 2;
        height_ = static_cast<uint32_t>(60.0f * progress);
    } else if (progress >= 0.3f) {
        growthStage_ = 1;
        height_ = static_cast<uint32_t>(30.0f * progress);
    } else {
        growthStage_ = 0;
        height_ = 1;
    }
}

void Tree::wither()
{
    growthStage_ = 4;
    height_ = 0;
    fruitCount_ = 0;
}

uint32_t Tree::getGrowthStage() const { return growthStage_; }

uint32_t Tree::getPlantType() const { return 0; }

std::string Tree::getStageName() const
{
    switch (growthStage_) {
    case 0: return "Seed";
    case 1: return "Sprout";
    case 2: return "Sapling";
    case 3: return "Mature";
    case 4: return "Withered";
    default: return "Unknown";
    }
}
