#ifndef ABSTRACTPLANT_H
#define ABSTRACTPLANT_H

#include <cstdint>
#include <string>

// Common interface used by FocusController. Concrete plants map elapsed focus
// time into a growth stage and expose a stable plant type id for persistence.
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
    // 0..3 are growing stages; 4 is used by the UI as withered.
    uint32_t growthStage_ = 0;
};

#endif // ABSTRACTPLANT_H
