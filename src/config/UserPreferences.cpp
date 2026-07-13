#include "config/UserPreferences.h"

#include <QDir>
#include <QFile>
#include <QSettings>

UserPreferences& UserPreferences::instance()
{
    static UserPreferences preferences;
    return preferences;
}

void UserPreferences::configure(const QString& dataDirectory)
{
    dataDirectory_ = dataDirectory;
    loaded_ = false;
}

bool UserPreferences::load()
{
    lastError_.clear();
    if (dataDirectory_.isEmpty() || !QDir().mkpath(dataDirectory_)) {
        lastError_ = QStringLiteral("无法创建偏好设置目录");
        return false;
    }
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    if (settings.status() == QSettings::AccessError) {
        lastError_ = QStringLiteral("无法读取偏好设置");
        return false;
    }

    const QString oldPath = QDir(dataDirectory_).filePath(QStringLiteral("ui_settings.ini"));
    if (!settings.contains(QStringLiteral("schema/version")) && QFile::exists(oldPath)) {
        QSettings old(oldPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("focus/allowPause"),
                          old.value(QStringLiteral("focus/allowPause"), true));
    }
    settings.setValue(QStringLiteral("schema/version"), 1);
    allowPause_ = settings.value(QStringLiteral("focus/allowPause"), true).toBool();
    focusSetup_.minutes = qBound<uint32_t>(10, settings.value(QStringLiteral("focus/minutes"), 25).toUInt(), 120);
    focusSetup_.plantType = qBound<uint32_t>(0, settings.value(QStringLiteral("focus/plantType"), 0).toUInt(), 5);
    focusSetup_.tagId = settings.value(QStringLiteral("focus/tagId"), 0).toUInt();
    focusSetup_.stopwatch = settings.value(QStringLiteral("focus/stopwatch"), false).toBool();
    focusSetup_.deepFocus = settings.value(QStringLiteral("focus/deepFocus"), true).toBool();
    focusSetup_.autoExtend = settings.value(QStringLiteral("focus/autoExtend"), false).toBool();
    accessibilityOptions_.fontScalePercent = qBound(90, settings.value(QStringLiteral("accessibility/fontScalePercent"), 100).toInt(), 125);
    accessibilityOptions_.reducedMotion = settings.value(QStringLiteral("accessibility/reducedMotion"), false).toBool();
    accessibilityOptions_.highContrast = settings.value(QStringLiteral("accessibility/highContrast"), false).toBool();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("无法保存偏好设置");
        return false;
    }
    loaded_ = true;
    return true;
}

bool UserPreferences::setFocusSetup(const FocusSetup& setup)
{
    if (!loaded_ && !load()) return false;
    focusSetup_ = setup;
    focusSetup_.minutes = qBound<uint32_t>(10, focusSetup_.minutes, 120);
    focusSetup_.plantType = qBound<uint32_t>(0, focusSetup_.plantType, 5);
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    settings.setValue(QStringLiteral("focus/minutes"), focusSetup_.minutes);
    settings.setValue(QStringLiteral("focus/plantType"), focusSetup_.plantType);
    settings.setValue(QStringLiteral("focus/tagId"), focusSetup_.tagId);
    settings.setValue(QStringLiteral("focus/stopwatch"), focusSetup_.stopwatch);
    settings.setValue(QStringLiteral("focus/deepFocus"), focusSetup_.deepFocus);
    settings.setValue(QStringLiteral("focus/autoExtend"), focusSetup_.autoExtend);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("无法保存专注预设");
        return false;
    }
    return true;
}

bool UserPreferences::setAllowPause(bool enabled)
{
    if (!loaded_ && !load()) return false;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    settings.setValue(QStringLiteral("focus/allowPause"), enabled);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("无法保存暂停设置");
        return false;
    }
    allowPause_ = enabled;
    return true;
}

bool UserPreferences::setAccessibilityOptions(const AccessibilityOptions& options)
{
    if (!loaded_ && !load()) return false;
    accessibilityOptions_ = options;
    accessibilityOptions_.fontScalePercent = qBound(90, accessibilityOptions_.fontScalePercent, 125);
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    settings.setValue(QStringLiteral("accessibility/fontScalePercent"), accessibilityOptions_.fontScalePercent);
    settings.setValue(QStringLiteral("accessibility/reducedMotion"), accessibilityOptions_.reducedMotion);
    settings.setValue(QStringLiteral("accessibility/highContrast"), accessibilityOptions_.highContrast);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("无法保存无障碍设置");
        return false;
    }
    return true;
}

UserPreferences::ChallengeRewardState UserPreferences::challengeRewardState(const QDate& date,
                                                                               int taskCount,
                                                                               int checkinCount)
{
    if (!loaded_) load();
    ChallengeRewardState state;
    state.date = date;
    state.monthKey = date.toString(QStringLiteral("yyyyMM"));
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    const QString dailyKey = date.toString(QStringLiteral("yyyyMMdd"));
    if (settings.value(QStringLiteral("challenge/daily/date")).toString() == dailyKey) {
        state.taskClaimed = deserialize(settings.value(QStringLiteral("challenge/daily/tasks")).toString(), taskCount);
        state.checkinClaimed = deserialize(settings.value(QStringLiteral("challenge/daily/checkins")).toString(), checkinCount);
    } else {
        state.taskClaimed = QVector<bool>(taskCount, false);
        state.checkinClaimed = QVector<bool>(checkinCount, false);
    }
    state.monthlyClaimed = settings.value(QStringLiteral("challenge/monthly/%1/claimed").arg(state.monthKey), false).toBool();
    return state;
}

bool UserPreferences::saveChallengeRewardState(const ChallengeRewardState& state)
{
    if (!loaded_ && !load()) return false;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    settings.setValue(QStringLiteral("challenge/daily/date"), state.date.toString(QStringLiteral("yyyyMMdd")));
    settings.setValue(QStringLiteral("challenge/daily/tasks"), serialize(state.taskClaimed));
    settings.setValue(QStringLiteral("challenge/daily/checkins"), serialize(state.checkinClaimed));
    settings.setValue(QStringLiteral("challenge/monthly/%1/claimed").arg(state.monthKey), state.monthlyClaimed);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("无法保存挑战奖励状态");
        return false;
    }
    return true;
}

QString UserPreferences::settingsPath() const
{
    return QDir(dataDirectory_).filePath(QStringLiteral("preferences.ini"));
}

QString UserPreferences::serialize(const QVector<bool>& values)
{
    QString value;
    value.reserve(values.size());
    for (bool item : values) value.append(item ? QLatin1Char('1') : QLatin1Char('0'));
    return value;
}

QVector<bool> UserPreferences::deserialize(const QString& value, int count)
{
    QVector<bool> result(count, false);
    for (int index = 0; index < count && index < value.size(); ++index) {
        result[index] = value.at(index) == QLatin1Char('1');
    }
    return result;
}
