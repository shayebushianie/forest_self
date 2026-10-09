#ifndef GUARDIANMANAGER_H
#define GUARDIANMANAGER_H

#include "common/DatabaseCommon.h"

#include <QString>
#include <QDate>
#include <QMap>
#include <QPair>
#include <QSet>
#include <cstdint>
#include <vector>

class GuardianManager {
public:
    explicit GuardianManager(const QString& filePath);

    bool load();
    bool save();
    QString lastError() const { return m_lastError; }
    bool hasCompletedFocus(uint32_t recordId) const { return m_appliedFocusRecords.contains(recordId); }
    bool needsHistoryReconciliation() const { return !m_historyReconciled; }

    // goal
    uint32_t dailyGoalMinutes() const { return m_dailyGoal; }
    bool setDailyGoalMinutes(uint32_t m);

    // Applies a completed focus duration and persists it as one operation.
    bool addCompletedFocusMinutes(uint32_t recordId, uint32_t minutes);
    bool addCompletedFocusSeconds(uint32_t recordId, uint64_t seconds, const QDate& startDate);
    bool reconcileCompletedFocusRecords(const std::vector<FocusRecord>& records);

    // stats
    uint32_t todayMinutes() const;
    uint32_t currentStreak() const;
    uint32_t longestStreak() const { return m_longestStreak; }
    uint32_t totalMinutes() const { return m_totalMinutes; }

    // force re-check (e.g., at app start)
    bool checkDayBoundary();
    bool checkDayBoundary(const QDate& today);

private:
    using DailyProgress = QMap<QDate, QPair<quint64, quint32>>;

    uint32_t minutesForDate(const QDate& date) const;
    bool dateReachedGoal(const QDate& date) const;
    uint32_t streakForDate(const QDate& date) const;
    uint32_t computedLongestStreak() const;
    void refreshSummary(const QDate& today);

    QString m_filePath;
    uint32_t m_dailyGoal = 30;
    uint32_t m_todayMinutes = 0;
    QDate m_lastActiveDate;
    uint32_t m_currentStreak = 0;
    uint32_t m_longestStreak = 0;
    uint32_t m_totalMinutes = 0;
    quint64 m_totalSeconds = 0;
    DailyProgress m_dailyProgress;
    bool m_historyReconciled = true;
    QSet<uint32_t> m_appliedFocusRecords;
    QString m_lastError;
};

#endif
