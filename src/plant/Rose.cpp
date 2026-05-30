#include "plant/Rose.h"

void Rose::grow(int actualSeconds, int totalSeconds)
{
    if (totalSeconds <= 0) return;

    float progress = static_cast<float>(actualSeconds) / totalSeconds;

    if (growthStage_ == 4) return;

    if (progress >= 1.0f) {
        growthStage_ = 3;
        petalCount_ = 24;
        bloomColor_ = "crimson";
    } else if (progress >= 0.66f) {
        growthStage_ = 2;
        petalCount_ = static_cast<uint32_t>(16.0f * progress);
        bloomColor_ = "pink";
    } else if (progress >= 0.33f) {
        growthStage_ = 1;
        petalCount_ = static_cast<uint32_t>(8.0f * progress);
        bloomColor_ = "light pink";
    } else {
        growthStage_ = 0;
        petalCount_ = 0;
        bloomColor_ = "seed";
    }
}

std::string Rose::getPlantName() const { return "Rose"; }
