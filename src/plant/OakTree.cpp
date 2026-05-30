#include "plant/OakTree.h"

void OakTree::grow(int actualSeconds, int totalSeconds)
{
    if (totalSeconds <= 0) return;

    float progress = static_cast<float>(actualSeconds) / totalSeconds;

    if (progress >= 1.0f) {
        growthStage_ = 3;
        height_ = 120;
        fruitCount_ = 8;
    } else if (progress >= 0.6f) {
        growthStage_ = 2;
        height_ = static_cast<uint32_t>(70.0f * (progress - 0.6f) / 0.4f + 40.0f);
    } else if (progress >= 0.3f) {
        growthStage_ = 1;
        height_ = static_cast<uint32_t>(30.0f * (progress - 0.3f) / 0.3f + 10.0f);
    } else {
        growthStage_ = 0;
        height_ = static_cast<uint32_t>(10.0f * progress / 0.3f);
    }
}

std::string OakTree::getPlantName() const { return "OakTree"; }
