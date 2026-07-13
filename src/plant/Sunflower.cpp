#include "plant/Sunflower.h"

void Sunflower::grow(int actualSeconds, int totalSeconds)
{
    if (totalSeconds <= 0) return;

    float progress = static_cast<float>(actualSeconds) / totalSeconds;

    if (growthStage_ == 4) return;

    if (progress >= 1.0f) {
        growthStage_ = 3;
        petalCount_ = 40;
        bloomColor_ = "golden yellow";
    } else if (progress >= 0.66f) {
        growthStage_ = 2;
        petalCount_ = static_cast<uint32_t>(28.0f * progress);
        bloomColor_ = "yellow";
    } else if (progress >= 0.33f) {
        growthStage_ = 1;
        petalCount_ = static_cast<uint32_t>(12.0f * progress);
        bloomColor_ = "light green";
    } else {
        growthStage_ = 0;
        petalCount_ = 0;
        bloomColor_ = "seed";
    }
}

std::string Sunflower::getPlantName() const { return "Sunflower"; }

uint32_t Sunflower::getPlantType() const { return 4; }
