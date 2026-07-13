#ifndef GUARDIANMANAGER_H
#define GUARDIANMANAGER_H

#include <QString>
#include <QDate>
#include <QSet>
#include <cstdint>

class GuardianManager {
public:
    explicit GuardianManager(const QString& filePath);

    bool load();
    bool save();

    // goal
    uint32_t dailyGoalMinutes() const { return m_dailyGoal; }
    void setDailyGoalMinutes(uint32_t m) { m_dailyGoal = m; }

    // Applies a completed focus duration and persists it as one operation.
    bool addCompletedFocusMinutes(uint32_t recordId, uint32_t minutes);

    // stats
    uint32_t todayMinutes() const { return m_todayMinutes; }
    uint32_t currentStreak() const { return m_currentStreak; }
    uint32_t longestStreak() const { return m_longestStreak; }
    uint32_t totalMinutes() const { return m_totalMinutes; }

    // force re-check (e.g., at app start)
    void checkDayBoundary();

private:
    QString m_filePath;
    uint32_t m_dailyGoal = 30;
    uint32_t m_todayMinutes = 0;
    QDate m_lastActiveDate;
    uint32_t m_currentStreak = 0;
    uint32_t m_longestStreak = 0;
    uint32_t m_totalMinutes = 0;
    QSet<uint32_t> m_appliedFocusRecords;
};

#endif
