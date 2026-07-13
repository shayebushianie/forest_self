#include "plant/PineTree.h"

void PineTree::grow(int actualSeconds, int totalSeconds)
{
    if (totalSeconds <= 0) return;

    float progress = static_cast<float>(actualSeconds) / totalSeconds;

    if (progress >= 1.0f) {
        growthStage_ = 3;
        height_ = 90;
        fruitCount_ = 12;
    } else if (progress >= 0.55f) {
        growthStage_ = 2;
        height_ = static_cast<uint32_t>(50.0f * (progress - 0.55f) / 0.45f + 30.0f);
    } else if (progress >= 0.1f) {
        growthStage_ = 1;
        height_ = static_cast<uint32_t>(20.0f * (progress - 0.1f) / 0.45f + 5.0f);
    } else {
        growthStage_ = 0;
        height_ = static_cast<uint32_t>(5.0f * progress / 0.1f);
    }
}

std::string PineTree::getPlantName() const { return "PineTree"; }

uint32_t PineTree::getPlantType() const { return 1; }
