#ifndef USERPREFERENCES_H
#define USERPREFERENCES_H

#include <QDate>
#include <QString>
#include <QVector>

class UserPreferences final {
public:
    struct FocusSetup {
        uint32_t minutes = 25;
        uint32_t plantType = 0;
        uint32_t tagId = 0;
        bool stopwatch = false;
        bool deepFocus = true;
        bool autoExtend = false;
    };
    struct ChallengeRewardState {
        QDate date;
        QString monthKey;
        QVector<bool> taskClaimed;
        QVector<bool> checkinClaimed;
        bool monthlyClaimed = false;
    };
    struct AccessibilityOptions {
        int fontScalePercent = 100;
        bool reducedMotion = false;
        bool highContrast = false;
    };

    static UserPreferences& instance();

    void configure(const QString& dataDirectory);
    bool load();
    QString lastError() const { return lastError_; }

    bool allowPause() const { return allowPause_; }
    bool setAllowPause(bool enabled);
    FocusSetup focusSetup() const { return focusSetup_; }
    bool setFocusSetup(const FocusSetup& setup);
    AccessibilityOptions accessibilityOptions() const { return accessibilityOptions_; }
    bool setAccessibilityOptions(const AccessibilityOptions& options);

    ChallengeRewardState challengeRewardState(const QDate& date, int taskCount, int checkinCount);
    bool saveChallengeRewardState(const ChallengeRewardState& state);

private:
    UserPreferences() = default;
    QString settingsPath() const;
    static QString serialize(const QVector<bool>& values);
    static QVector<bool> deserialize(const QString& value, int count);

    QString dataDirectory_;
    QString lastError_;
    bool loaded_ = false;
    bool allowPause_ = true;
    FocusSetup focusSetup_;
    AccessibilityOptions accessibilityOptions_;
};

#endif // USERPREFERENCES_H
