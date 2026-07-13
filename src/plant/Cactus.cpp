#include "plant/Cactus.h"

void Cactus::grow(int actualSeconds, int totalSeconds)
{
    if (totalSeconds <= 0) return;

    float progress = static_cast<float>(actualSeconds) / totalSeconds;

    if (progress >= 1.0f) {
        growthStage_ = 3;
        height_ = 60;
        spineCount_ = 16;
    } else if (progress >= 0.55f) {
        growthStage_ = 2;
        height_ = static_cast<uint32_t>(35.0f * (progress - 0.55f) / 0.45f + 20.0f);
        spineCount_ = static_cast<uint32_t>(10.0f * progress);
    } else if (progress >= 0.25f) {
        growthStage_ = 1;
        height_ = static_cast<uint32_t>(15.0f * (progress - 0.25f) / 0.30f + 5.0f);
        spineCount_ = static_cast<uint32_t>(4.0f * progress);
    } else {
        growthStage_ = 0;
        height_ = static_cast<uint32_t>(5.0f * progress / 0.25f);
        spineCount_ = 0;
    }
}

void Cactus::wither()
{
    growthStage_ = 4;
    height_ = 0;
    spineCount_ = 0;
}

uint32_t Cactus::getGrowthStage() const { return growthStage_; }

uint32_t Cactus::getPlantType() const { return 5; }

std::string Cactus::getPlantName() const { return "Cactus"; }

std::string Cactus::getStageName() const
{
    switch (growthStage_) {
    case 0: return "Seed";
    case 1: return "Sprout";
    case 2: return "Growing";
    case 3: return "Mature";
    case 4: return "Withered";
    default: return "Unknown";
    }
}
