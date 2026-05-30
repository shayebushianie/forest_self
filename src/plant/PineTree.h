#ifndef PINETREE_H
#define PINETREE_H

#include "plant/Tree.h"

class PineTree : public Tree {
public:
    void grow(int actualSeconds, int totalSeconds) override;
    std::string getPlantName() const override;
};

#endif // PINETREE_H
