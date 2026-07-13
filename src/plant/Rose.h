#ifndef ROSE_H
#define ROSE_H

#include "plant/Flower.h"

class Rose : public Flower {
public:
    void grow(int actualSeconds, int totalSeconds) override;
    std::string getPlantName() const override;
};

#endif // ROSE_H
