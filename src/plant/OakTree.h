#ifndef OAKTREE_H
#define OAKTREE_H

#include "plant/Tree.h"

class OakTree : public Tree {
public:
    void grow(int actualSeconds, int totalSeconds) override;
    std::string getPlantName() const override;
};

#endif // OAKTREE_H
