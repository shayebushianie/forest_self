#ifndef STATISTICSCALCULATOR_H
#define STATISTICSCALCULATOR_H

#include <map>
#include <cstdint>
#include "storage/DatabaseManager.h"

class StatisticsCalculator {
public:
    explicit StatisticsCalculator(DatabaseManager& db) : db_(db) {}

    std::map<uint32_t, uint64_t> calculateTagDistribution() {
        std::map<uint32_t, uint64_t> distribution;
        for (uint32_t i = 0; i <= 4; ++i) distribution[i] = 0;

        auto records = db_.getAllRecords();
        for (const auto& rec : records) {
            if (rec.status == 0 && rec.tagId <= 4) {
                distribution[rec.tagId] += rec.actualSeconds;
            }
        }
        return distribution;
    }

private:
    DatabaseManager& db_;
};

#endif // STATISTICSCALCULATOR_H
