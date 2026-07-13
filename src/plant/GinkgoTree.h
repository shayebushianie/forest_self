#ifndef GINKGOTREE_H
#define GINKGOTREE_H

#include "plant/Tree.h"

class GinkgoTree : public Tree {
public:
    void grow(int actualSeconds, int totalSeconds) override;
    std::string getPlantName() const override;
    uint32_t getPlantType() const override;
};

#endif // GINKGOTREE_H
