#ifndef USERPREFERENCES_H
#define USERPREFERENCES_H

#include <cstdint>
#include <QDate>
#include <QString>
#include <QStringList>
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
    void bindAccount(const QString& userDirectory,
                     uint32_t userId,
                     const QVector<uint32_t>& knownUserIds);
    bool load();
    QString lastError() const { return lastError_; }

    bool allowPause() const { return allowPause_; }
    bool setAllowPause(bool enabled);
    FocusSetup focusSetup() const { return focusSetup_; }
    bool setFocusSetup(const FocusSetup& setup);
    AccessibilityOptions accessibilityOptions() const { return accessibilityOptions_; }
    bool setAccessibilityOptions(const AccessibilityOptions& options);
    QStringList processBlacklist() const { return processBlacklist_; }
    bool setProcessBlacklist(const QStringList& processes);

    ChallengeRewardState challengeRewardState(const QDate& date, int taskCount, int checkinCount);
    bool saveChallengeRewardState(const ChallengeRewardState& state);
    QStringList legacyChallengeClaimKeys() const;
    bool unresolvedLegacyRewards() const { return unresolvedLegacyRewards_; }
    QString migrationNotice() const { return migrationNotice_; }

private:
    UserPreferences() = default;
    QString settingsPath(const QString& directory) const;
    QString deviceSettingsPath() const;
    QString accountSettingsPath() const;
    static QString serialize(const QVector<bool>& values);
    static QVector<bool> deserialize(const QString& value, int count);

    QString deviceDirectory_;
    QString accountDirectory_;
    QString lastError_;
    QString migrationNotice_;
    QVector<uint32_t> knownUserIds_;
    uint32_t userId_ = 0;
    bool accountBound_ = false;
    bool unresolvedLegacyRewards_ = false;
    bool loaded_ = false;
    bool allowPause_ = true;
    FocusSetup focusSetup_;
    AccessibilityOptions accessibilityOptions_;
    QStringList processBlacklist_;
};

#endif // USERPREFERENCES_H
