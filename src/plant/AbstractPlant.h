#ifndef ABSTRACTPLANT_H
#define ABSTRACTPLANT_H

#include <cstdint>
#include <string>

class AbstractPlant {
public:
    virtual ~AbstractPlant() = default;

    virtual void grow(int actualSeconds, int totalSeconds) = 0;
    virtual void wither() = 0;

    virtual uint32_t getGrowthStage() const = 0;
    virtual uint32_t getPlantType() const = 0;
    virtual std::string getPlantName() const = 0;
    virtual std::string getStageName() const = 0;

protected:
    uint32_t growthStage_ = 0;
};

#endif // ABSTRACTPLANT_H
