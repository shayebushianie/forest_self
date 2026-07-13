#include "plant/Flower.h"

void Flower::grow(int actualSeconds, int totalSeconds)
{
    if (totalSeconds <= 0) return;

    float progress = static_cast<float>(actualSeconds) / totalSeconds;

    if (progress >= 1.0f) {
        growthStage_ = 3;
        petalCount_ = 12;
        bloomColor_ = "vibrant";
    } else if (progress >= 0.66f) {
        growthStage_ = 2;
        petalCount_ = static_cast<uint32_t>(8.0f * progress);
        bloomColor_ = "budding";
    } else if (progress >= 0.33f) {
        growthStage_ = 1;
        petalCount_ = static_cast<uint32_t>(4.0f * progress);
        bloomColor_ = "green";
    } else {
        growthStage_ = 0;
        petalCount_ = 0;
        bloomColor_ = "seed";
    }
}

void Flower::wither()
{
    growthStage_ = 4;
    petalCount_ = 0;
    bloomColor_ = "grey";
}

uint32_t Flower::getGrowthStage() const { return growthStage_; }

uint32_t Flower::getPlantType() const { return 2; }

std::string Flower::getStageName() const
{
    switch (growthStage_) {
    case 0: return "Seed";
    case 1: return "Bud";
    case 2: return "Blooming";
    case 3: return "Full Bloom";
    case 4: return "Withered";
    default: return "Unknown";
    }
}
