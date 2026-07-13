#ifndef SUNFLOWER_H
#define SUNFLOWER_H

#include "plant/Flower.h"

class Sunflower : public Flower {
public:
    void grow(int actualSeconds, int totalSeconds) override;
    std::string getPlantName() const override;
    uint32_t getPlantType() const override;
};

#endif // SUNFLOWER_H
