#include "config/UserPreferences.h"

#include "config/PlantCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <limits>

namespace {

constexpr int kSchemaVersion = 1;
const QString kLegacyOwnerKey = QStringLiteral("legacy/ownerUserId");
const QString kLegacyOwnerProofKey = QStringLiteral("legacy/ownerProof");
const QString kLegacyMigrationCompleteKey = QStringLiteral("legacy/accountPreferencesMigrated");
const QStringList kFocusKeys = {
    QStringLiteral("focus/allowPause"),
    QStringLiteral("focus/minutes"),
    QStringLiteral("focus/plantType"),
    QStringLiteral("focus/tagId"),
    QStringLiteral("focus/stopwatch"),
    QStringLiteral("focus/deepFocus"),
    QStringLiteral("focus/autoExtend"),
    QStringLiteral("supervision/blacklist")
};
const QStringList kLegacyDailyKeys = {
    QStringLiteral("challenge/daily/date"),
    QStringLiteral("challenge/daily/tasks"),
    QStringLiteral("challenge/daily/checkins")
};
const QStringList kTaskIds = {
    QStringLiteral("focus-minutes"),
    QStringLiteral("focus-session"),
    QStringLiteral("specified-plant"),
    QStringLiteral("growth")
};

bool isClaimKey(const QString& key)
{
    static const QRegularExpression pattern(
        QStringLiteral("^challenge/(?:checkin/\\d{8}/checkin-\\d+|"
                       "daily/\\d{8}/(?:focus-minutes|focus-session|specified-plant|growth)|"
                       "monthly/\\d{6}/month-focus)$"));
    return pattern.match(key).hasMatch();
}

QStringList legacyClaimKeys(QSettings& settings)
{
    QStringList result;
    const QRegularExpression monthClaim(
        QStringLiteral("^challenge/monthly/(\\d{6})/claimed$"));
    for (const QString& key : settings.allKeys()) {
        if (isClaimKey(key) && settings.value(key).toBool()) result.push_back(key);
        const auto monthMatch = monthClaim.match(key);
        if (monthMatch.hasMatch() && settings.value(key).toBool()) {
            result.push_back(QStringLiteral("challenge/monthly/%1/month-focus")
                                 .arg(monthMatch.captured(1)));
        }
    }

    const QString date = settings.value(QStringLiteral("challenge/daily/date")).toString();
    const QDate parsedDate = QDate::fromString(date, QStringLiteral("yyyyMMdd"));
    if (parsedDate.isValid() && parsedDate.toString(QStringLiteral("yyyyMMdd")) == date) {
        const QString tasks = settings.value(QStringLiteral("challenge/daily/tasks")).toString();
        for (int index = 0; index < kTaskIds.size() && index < tasks.size(); ++index) {
            if (tasks.at(index) == QLatin1Char('1')) {
                result.push_back(QStringLiteral("challenge/daily/%1/%2")
                                     .arg(date, kTaskIds.at(index)));
            }
        }
        const QString checkins = settings.value(QStringLiteral("challenge/daily/checkins")).toString();
        for (int index = 0; index < checkins.size(); ++index) {
            if (checkins.at(index) == QLatin1Char('1')) {
                result.push_back(QStringLiteral("challenge/checkin/%1/checkin-%2")
                                     .arg(date).arg(index));
            }
        }
    }

    result.removeDuplicates();
    result.sort();
    return result;
}

bool hasMalformedDailyClaims(QSettings& settings)
{
    const QString date = settings.value(QStringLiteral("challenge/daily/date")).toString();
    const QDate parsedDate = QDate::fromString(date, QStringLiteral("yyyyMMdd"));
    if (parsedDate.isValid() && parsedDate.toString(QStringLiteral("yyyyMMdd")) == date) return false;
    return settings.value(QStringLiteral("challenge/daily/tasks")).toString().contains(QLatin1Char('1')) ||
           settings.value(QStringLiteral("challenge/daily/checkins")).toString().contains(QLatin1Char('1'));
}

bool hasLegacyRewardEvidence(QSettings& settings)
{
    return !legacyClaimKeys(settings).isEmpty() || hasMalformedDailyClaims(settings);
}

bool hasLegacyAccountData(QSettings& settings)
{
    for (const QString& key : kFocusKeys) {
        if (settings.contains(key)) return true;
    }
    for (const QString& key : kLegacyDailyKeys) {
        if (settings.contains(key)) return true;
    }
    static const QRegularExpression challengeState(
        QStringLiteral("^challenge/(?:checkin/\\d{8}/checkin-\\d+|"
                       "daily/\\d{8}/(?:focus-minutes|focus-session|specified-plant|growth)|"
                       "monthly/\\d{6}/(?:claimed|month-focus))$"));
    for (const QString& key : settings.allKeys()) {
        if (challengeState.match(key).hasMatch()) return true;
    }
    return false;
}

QStringList defaultProcessBlacklist()
{
    return {QStringLiteral("notepad.exe"), QStringLiteral("chrome.exe"), QStringLiteral("msedge.exe"),
            QStringLiteral("steam.exe"), QStringLiteral("spotify.exe"), QStringLiteral("discord.exe")};
}

bool normalizeProcessBlacklist(const QStringList& input, QStringList* output)
{
    if (input.size() > 128) return false;
    static const QRegularExpression processName(QStringLiteral("^[^\\\\/:*?\"<>|\\r\\n]+\\.exe$"),
                                                 QRegularExpression::CaseInsensitiveOption);
    output->clear();
    for (const QString& item : input) {
        const QString name = item.trimmed().toLower();
        if (name.size() > 260 || !processName.match(name).hasMatch()) return false;
        if (!output->contains(name)) output->append(name);
    }
    return true;
}

bool validateSettings(QSettings& settings, const QString& path, QString* error)
{
    if (settings.status() != QSettings::NoError) {
        *error = QStringLiteral("无法读取偏好设置：%1").arg(path);
        return false;
    }
    const QString versionKey = QStringLiteral("schema/version");
    if (settings.contains(versionKey)) {
        bool validVersion = false;
        const int version = settings.value(versionKey).toInt(&validVersion);
        if (!validVersion || version != kSchemaVersion) {
            *error = QStringLiteral("偏好设置版本不受支持，未进行任何写入：%1").arg(path);
            return false;
        }
    }

    const auto invalidField = [error, &path](const QString& key) {
        *error = QStringLiteral("偏好设置字段无效，原文件未修改：%1（%2）").arg(key, path);
        return false;
    };
    QStringList booleanKeys = {
        QStringLiteral("focus/allowPause"), QStringLiteral("focus/stopwatch"),
        QStringLiteral("focus/deepFocus"), QStringLiteral("focus/autoExtend"),
        QStringLiteral("accessibility/reducedMotion"), QStringLiteral("accessibility/highContrast")
    };
    static const QRegularExpression monthlyClaim(QStringLiteral("^challenge/monthly/\\d{6}/claimed$"));
    for (const QString& key : settings.allKeys()) {
        if (isClaimKey(key) || monthlyClaim.match(key).hasMatch()) booleanKeys.append(key);
    }
    static const QRegularExpression claimedBits(QStringLiteral("^[01]*$"));
    for (const QString& key : {QStringLiteral("challenge/daily/tasks"), QStringLiteral("challenge/daily/checkins")}) {
        if (settings.contains(key) && !claimedBits.match(settings.value(key).toString()).hasMatch()) {
            return invalidField(key);
        }
    }
    QStringList processes;
    if (settings.contains(QStringLiteral("supervision/blacklist")) &&
        !normalizeProcessBlacklist(settings.value(QStringLiteral("supervision/blacklist")).toStringList(),
                                   &processes)) return invalidField(QStringLiteral("supervision/blacklist"));
    for (const QString& key : booleanKeys) {
        if (!settings.contains(key)) continue;
        const QString value = settings.value(key).toString().toLower();
        if (value != QStringLiteral("true") && value != QStringLiteral("false") &&
            value != QStringLiteral("1") && value != QStringLiteral("0")) return invalidField(key);
    }
    const QStringList integerKeys = {
        QStringLiteral("focus/minutes"), QStringLiteral("focus/plantType"),
        QStringLiteral("focus/tagId"), QStringLiteral("accessibility/fontScalePercent")
    };
    static const QRegularExpression integerPattern(QStringLiteral("^\\d+$"));
    for (const QString& key : integerKeys) {
        if (!settings.contains(key)) continue;
        const QString text = settings.value(key).toString();
        bool valid = false;
        const qulonglong value = text.toULongLong(&valid);
        if (!valid || !integerPattern.match(text).hasMatch() ||
            value > std::numeric_limits<uint32_t>::max()) return invalidField(key);
        if (key == QStringLiteral("focus/minutes") && (value < 10 || value > 120)) return invalidField(key);
        if (key == QStringLiteral("focus/plantType") && !PlantCatalog::isKnown(static_cast<uint32_t>(value))) {
            return invalidField(key);
        }
        if (key == QStringLiteral("accessibility/fontScalePercent") && (value < 90 || value > 125)) {
            return invalidField(key);
        }
    }
    if (settings.status() != QSettings::NoError) {
        *error = QStringLiteral("无法读取偏好设置：%1").arg(path);
        return false;
    }
    return true;
}

void copyIfMissing(QSettings& source, QSettings& destination, const QString& key)
{
    if (source.contains(key) && !destination.contains(key)) {
        destination.setValue(key, source.value(key));
    }
}

void copyLegacyAccountData(QSettings& source, QSettings& destination)
{
    for (const QString& key : kFocusKeys) copyIfMissing(source, destination, key);
    for (const QString& key : kLegacyDailyKeys) copyIfMissing(source, destination, key);

    const QRegularExpression monthlyState(
        QStringLiteral("^challenge/monthly/\\d{6}/claimed$"));
    for (const QString& key : source.allKeys()) {
        if (isClaimKey(key) && source.value(key).toBool()) {
            destination.setValue(key, true);
        } else if (monthlyState.match(key).hasMatch()) {
            copyIfMissing(source, destination, key);
        }
    }
    for (const QString& key : legacyClaimKeys(source)) destination.setValue(key, true);
}

bool containsUser(const QVector<uint32_t>& userIds, uint32_t userId)
{
    for (uint32_t candidate : userIds) {
        if (candidate == userId) return true;
    }
    return false;
}

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

}

UserPreferences& UserPreferences::instance()
{
    static UserPreferences preferences;
    return preferences;
}

void UserPreferences::configure(const QString& dataDirectory)
{
    deviceDirectory_ = dataDirectory;
    accountDirectory_ = dataDirectory;
    knownUserIds_.clear();
    userId_ = 0;
    accountBound_ = false;
    loaded_ = false;
    unresolvedLegacyRewards_ = false;
    lastError_.clear();
    migrationNotice_.clear();
    allowPause_ = true;
    focusSetup_ = FocusSetup{};
    accessibilityOptions_ = AccessibilityOptions{};
    processBlacklist_ = defaultProcessBlacklist();
}

void UserPreferences::bindAccount(const QString& userDirectory,
                                 uint32_t userId,
                                 const QVector<uint32_t>& knownUserIds)
{
    accountDirectory_ = userDirectory;
    knownUserIds_ = knownUserIds;
    userId_ = userId;
    accountBound_ = true;
    loaded_ = false;
    unresolvedLegacyRewards_ = false;
    lastError_.clear();
    migrationNotice_.clear();
    allowPause_ = true;
    focusSetup_ = FocusSetup{};
    accessibilityOptions_ = AccessibilityOptions{};
    processBlacklist_ = defaultProcessBlacklist();
}

bool UserPreferences::load()
{
    lastError_.clear();
    migrationNotice_.clear();
    unresolvedLegacyRewards_ = false;
    loaded_ = false;
    if (deviceDirectory_.isEmpty() || accountDirectory_.isEmpty()) {
        lastError_ = QStringLiteral("偏好设置目录未配置");
        return false;
    }

    const QString devicePath = deviceSettingsPath();
    const QString accountPath = accountSettingsPath();
    if (accountBound_ && normalizedPath(devicePath) == normalizedPath(accountPath)) {
        lastError_ = QStringLiteral("设备与账户偏好设置目录不能相同");
        return false;
    }

    if (!accountBound_) {
        QSettings settings(accountPath, QSettings::IniFormat);
        if (!validateSettings(settings, accountPath, &lastError_)) return false;

        const QString oldPath = QDir(deviceDirectory_).filePath(QStringLiteral("ui_settings.ini"));
        QSettings oldSettings(oldPath, QSettings::IniFormat);
        if (!validateSettings(oldSettings, oldPath, &lastError_)) return false;
        if (!QDir().mkpath(deviceDirectory_)) {
            lastError_ = QStringLiteral("无法创建偏好设置目录");
            return false;
        }

        settings.setAtomicSyncRequired(true);
        if (!settings.contains(QStringLiteral("schema/version")) &&
            oldSettings.contains(QStringLiteral("focus/allowPause"))) {
            settings.setValue(QStringLiteral("focus/allowPause"),
                              oldSettings.value(QStringLiteral("focus/allowPause")));
        }
        settings.setValue(QStringLiteral("schema/version"), kSchemaVersion);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            lastError_ = QStringLiteral("无法保存偏好设置");
            return false;
        }

        allowPause_ = settings.value(QStringLiteral("focus/allowPause"), true).toBool();
        focusSetup_.minutes = qBound<uint32_t>(10,
            settings.value(QStringLiteral("focus/minutes"), 25).toUInt(), 120);
        focusSetup_.plantType = settings.value(QStringLiteral("focus/plantType"),
                                             PlantCatalog::all().front().type).toUInt();
        focusSetup_.tagId = settings.value(QStringLiteral("focus/tagId"), 0).toUInt();
        focusSetup_.stopwatch = settings.value(QStringLiteral("focus/stopwatch"), false).toBool();
        focusSetup_.deepFocus = settings.value(QStringLiteral("focus/deepFocus"), true).toBool();
        focusSetup_.autoExtend = settings.value(QStringLiteral("focus/autoExtend"), false).toBool();
        accessibilityOptions_.fontScalePercent = qBound(90,
            settings.value(QStringLiteral("accessibility/fontScalePercent"), 100).toInt(), 125);
        accessibilityOptions_.reducedMotion = settings.value(
            QStringLiteral("accessibility/reducedMotion"), false).toBool();
        accessibilityOptions_.highContrast = settings.value(
            QStringLiteral("accessibility/highContrast"), false).toBool();
        if (settings.contains(QStringLiteral("supervision/blacklist"))) {
            normalizeProcessBlacklist(settings.value(QStringLiteral("supervision/blacklist")).toStringList(),
                                      &processBlacklist_);
        }
        loaded_ = true;
        return true;
    }

    if (!containsUser(knownUserIds_, userId_)) {
        lastError_ = QStringLiteral("当前账户不在已加载的账户列表中");
        return false;
    }

    QSettings deviceSettings(devicePath, QSettings::IniFormat);
    QSettings accountSettings(accountPath, QSettings::IniFormat);
    if (!validateSettings(deviceSettings, devicePath, &lastError_) ||
        !validateSettings(accountSettings, accountPath, &lastError_)) {
        return false;
    }
    const QString oldPath = QDir(deviceDirectory_).filePath(QStringLiteral("ui_settings.ini"));
    QSettings oldSettings(oldPath, QSettings::IniFormat);
    if (!validateSettings(oldSettings, oldPath, &lastError_)) return false;

    const bool hasPublicState = hasLegacyAccountData(deviceSettings) ||
        oldSettings.contains(QStringLiteral("focus/allowPause"));
    const bool hasPublicClaims = hasLegacyRewardEvidence(deviceSettings);
    bool ownerKnown = false;
    bool ownerProvenBySingleAccount = false;
    uint32_t legacyOwnerId = 0;
    if (deviceSettings.contains(kLegacyOwnerKey)) {
        bool validOwner = false;
        const uint32_t storedOwner = deviceSettings.value(kLegacyOwnerKey).toUInt(&validOwner);
        if (validOwner && storedOwner != 0 && containsUser(knownUserIds_, storedOwner)) {
            ownerKnown = true;
            legacyOwnerId = storedOwner;
            ownerProvenBySingleAccount =
                deviceSettings.value(kLegacyOwnerProofKey).toString() ==
                QStringLiteral("single-current-account");
        }
    } else if (knownUserIds_.size() == 1 && knownUserIds_.front() == userId_) {
        ownerKnown = true;
        ownerProvenBySingleAccount = true;
        legacyOwnerId = userId_;
    }

    if (hasPublicState && !ownerKnown) {
        unresolvedLegacyRewards_ = hasPublicClaims;
        migrationNotice_ = hasPublicClaims
            ? QStringLiteral("发现无法确认归属的旧挑战领取记录。为避免重复奖励，当前账户的挑战领取已暂停；原始记录仍保留在设备偏好中。请恢复完整的账户数据快照，或确认归属后在设备偏好中登记 legacy/ownerUserId，再重新启动。")
            : QStringLiteral("发现无法确认归属的旧专注偏好，暂未迁入当前账户；原始记录仍保留在设备偏好中。请恢复完整的账户数据快照，或确认归属后在设备偏好中登记 legacy/ownerUserId，再重新启动。");
        if (!QDir().mkpath(accountDirectory_)) {
            lastError_ = QStringLiteral("无法创建账户偏好设置目录");
            return false;
        }
        accountSettings.setAtomicSyncRequired(true);
        if (!accountSettings.contains(QStringLiteral("schema/version"))) {
            accountSettings.setValue(QStringLiteral("schema/version"), kSchemaVersion);
            accountSettings.sync();
            if (accountSettings.status() != QSettings::NoError) {
                lastError_ = QStringLiteral("无法初始化账户偏好设置");
                return false;
            }
        }
    } else if (hasPublicState && ownerKnown && legacyOwnerId == userId_) {
        if (hasMalformedDailyClaims(deviceSettings)) {
            unresolvedLegacyRewards_ = true;
            migrationNotice_ = QStringLiteral("旧日挑战领取记录包含无法解析日期的已领取标记。为避免重复奖励，当前账户的挑战领取已暂停；原始记录仍保留，请恢复完整的账户数据快照或人工核对后再继续。");
        }
        if (!QDir().mkpath(deviceDirectory_) || !QDir().mkpath(accountDirectory_)) {
            lastError_ = QStringLiteral("无法创建偏好设置目录");
            return false;
        }
        deviceSettings.setAtomicSyncRequired(true);
        accountSettings.setAtomicSyncRequired(true);

        if (!deviceSettings.contains(kLegacyOwnerKey)) {
            deviceSettings.setValue(QStringLiteral("schema/version"), kSchemaVersion);
            deviceSettings.setValue(kLegacyOwnerKey, static_cast<qulonglong>(legacyOwnerId));
            if (ownerProvenBySingleAccount) {
                deviceSettings.setValue(kLegacyOwnerProofKey,
                                        QStringLiteral("single-current-account"));
            }
            deviceSettings.sync();
            if (deviceSettings.status() != QSettings::NoError) {
                lastError_ = QStringLiteral("无法记录旧偏好的账户归属");
                return false;
            }
        }
        if (!deviceSettings.value(kLegacyMigrationCompleteKey, false).toBool()) {
            copyLegacyAccountData(deviceSettings, accountSettings);
            if (oldSettings.contains(QStringLiteral("focus/allowPause")) &&
                !accountSettings.contains(QStringLiteral("focus/allowPause"))) {
                accountSettings.setValue(QStringLiteral("focus/allowPause"),
                                         oldSettings.value(QStringLiteral("focus/allowPause")));
            }
            accountSettings.setValue(QStringLiteral("schema/version"), kSchemaVersion);
            accountSettings.sync();
            if (accountSettings.status() != QSettings::NoError) {
                lastError_ = QStringLiteral("无法保存迁移后的账户偏好设置");
                return false;
            }
            deviceSettings.setValue(kLegacyMigrationCompleteKey, true);
            deviceSettings.sync();
            if (deviceSettings.status() != QSettings::NoError) {
                lastError_ = QStringLiteral("无法记录旧偏好迁移完成状态");
                return false;
            }
            migrationNotice_ = QStringLiteral("已将可确认属于当前账户的旧偏好迁入账户目录；设备偏好和原始旧记录均予以保留。");
            if (ownerProvenBySingleAccount) {
                migrationNotice_ += QStringLiteral("本次按当前仅有的账户判定归属，无法证明此前已删除账户的历史归属。");
            }
            if (hasMalformedDailyClaims(deviceSettings)) {
                unresolvedLegacyRewards_ = true;
                migrationNotice_ += QStringLiteral("旧日挑战领取日期无法解析，领取仍保持暂停，原始标记已保留供人工核对。");
            }
        } else if (!accountSettings.contains(QStringLiteral("schema/version"))) {
            accountSettings.setValue(QStringLiteral("schema/version"), kSchemaVersion);
            accountSettings.sync();
            if (accountSettings.status() != QSettings::NoError) {
                lastError_ = QStringLiteral("无法初始化账户偏好设置");
                return false;
            }
        }
    } else {
        if (!QDir().mkpath(accountDirectory_)) {
            lastError_ = QStringLiteral("无法创建账户偏好设置目录");
            return false;
        }
        accountSettings.setAtomicSyncRequired(true);
        if (!accountSettings.contains(QStringLiteral("schema/version"))) {
            accountSettings.setValue(QStringLiteral("schema/version"), kSchemaVersion);
            accountSettings.sync();
            if (accountSettings.status() != QSettings::NoError) {
                lastError_ = QStringLiteral("无法初始化账户偏好设置");
                return false;
            }
        }
    }

    if (!hasPublicState && !QDir().mkpath(deviceDirectory_)) {
        lastError_ = QStringLiteral("无法创建设备偏好设置目录");
        return false;
    }
    if (!hasPublicState && !deviceSettings.contains(QStringLiteral("schema/version"))) {
        deviceSettings.setAtomicSyncRequired(true);
        deviceSettings.setValue(QStringLiteral("schema/version"), kSchemaVersion);
        deviceSettings.sync();
        if (deviceSettings.status() != QSettings::NoError) {
            lastError_ = QStringLiteral("无法初始化设备偏好设置");
            return false;
        }
    }

    allowPause_ = accountSettings.value(QStringLiteral("focus/allowPause"), true).toBool();
    focusSetup_.minutes = qBound<uint32_t>(10,
        accountSettings.value(QStringLiteral("focus/minutes"), 25).toUInt(), 120);
    focusSetup_.plantType = accountSettings.value(QStringLiteral("focus/plantType"),
                                                PlantCatalog::all().front().type).toUInt();
    focusSetup_.tagId = accountSettings.value(QStringLiteral("focus/tagId"), 0).toUInt();
    focusSetup_.stopwatch = accountSettings.value(QStringLiteral("focus/stopwatch"), false).toBool();
    focusSetup_.deepFocus = accountSettings.value(QStringLiteral("focus/deepFocus"), true).toBool();
    focusSetup_.autoExtend = accountSettings.value(QStringLiteral("focus/autoExtend"), false).toBool();
    accessibilityOptions_.fontScalePercent = qBound(90,
        deviceSettings.value(QStringLiteral("accessibility/fontScalePercent"), 100).toInt(), 125);
    accessibilityOptions_.reducedMotion = deviceSettings.value(
        QStringLiteral("accessibility/reducedMotion"), false).toBool();
    accessibilityOptions_.highContrast = deviceSettings.value(
        QStringLiteral("accessibility/highContrast"), false).toBool();
    if (accountSettings.contains(QStringLiteral("supervision/blacklist"))) {
        normalizeProcessBlacklist(accountSettings.value(QStringLiteral("supervision/blacklist")).toStringList(),
                                  &processBlacklist_);
    }
    loaded_ = true;
    return true;
}

bool UserPreferences::setFocusSetup(const FocusSetup& setup)
{
    if (!loaded_ && !load()) return false;
    lastError_.clear();
    FocusSetup next = setup;
    next.minutes = qBound<uint32_t>(10, next.minutes, 120);
    if (!PlantCatalog::isKnown(next.plantType)) {
        lastError_ = QStringLiteral("无法保存专注预设：植物不在注册表中");
        return false;
    }

    QSettings settings(accountSettingsPath(), QSettings::IniFormat);
    settings.sync();
    if (!validateSettings(settings, accountSettingsPath(), &lastError_)) return false;
    settings.setAtomicSyncRequired(true);
    settings.setValue(QStringLiteral("focus/minutes"), next.minutes);
    settings.setValue(QStringLiteral("focus/plantType"), next.plantType);
    settings.setValue(QStringLiteral("focus/tagId"), next.tagId);
    settings.setValue(QStringLiteral("focus/stopwatch"), next.stopwatch);
    settings.setValue(QStringLiteral("focus/deepFocus"), next.deepFocus);
    settings.setValue(QStringLiteral("focus/autoExtend"), next.autoExtend);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("无法保存专注预设");
        return false;
    }
    focusSetup_ = next;
    return true;
}

bool UserPreferences::setProcessBlacklist(const QStringList& processes)
{
    if (!loaded_ && !load()) return false;
    lastError_.clear();
    QStringList next;
    if (!normalizeProcessBlacklist(processes, &next)) {
        lastError_ = QStringLiteral("请输入有效进程名（例如 chrome.exe），最多保存 128 项");
        return false;
    }
    QSettings settings(accountSettingsPath(), QSettings::IniFormat);
    settings.sync();
    if (!validateSettings(settings, accountSettingsPath(), &lastError_)) return false;
    settings.setAtomicSyncRequired(true);
    settings.setValue(QStringLiteral("supervision/blacklist"), next);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("无法保存监督规则");
        return false;
    }
    processBlacklist_ = next;
    return true;
}

bool UserPreferences::setAllowPause(bool enabled)
{
    if (!loaded_ && !load()) return false;
    lastError_.clear();
    QSettings settings(accountSettingsPath(), QSettings::IniFormat);
    settings.sync();
    if (!validateSettings(settings, accountSettingsPath(), &lastError_)) return false;
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
    lastError_.clear();
    AccessibilityOptions next = options;
    next.fontScalePercent = qBound(90, next.fontScalePercent, 125);

    QSettings settings(deviceSettingsPath(), QSettings::IniFormat);
    settings.sync();
    if (!validateSettings(settings, deviceSettingsPath(), &lastError_)) return false;
    settings.setAtomicSyncRequired(true);
    settings.setValue(QStringLiteral("accessibility/fontScalePercent"), next.fontScalePercent);
    settings.setValue(QStringLiteral("accessibility/reducedMotion"), next.reducedMotion);
    settings.setValue(QStringLiteral("accessibility/highContrast"), next.highContrast);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("无法保存无障碍设置");
        return false;
    }
    accessibilityOptions_ = next;
    return true;
}

UserPreferences::ChallengeRewardState UserPreferences::challengeRewardState(const QDate& date,
                                                                               int taskCount,
                                                                               int checkinCount)
{
    ChallengeRewardState state;
    state.date = date;
    state.monthKey = date.toString(QStringLiteral("yyyyMM"));
    state.taskClaimed = QVector<bool>(qMax(0, taskCount), false);
    state.checkinClaimed = QVector<bool>(qMax(0, checkinCount), false);
    if (!loaded_ && !load()) {
        if (unresolvedLegacyRewards_) {
            state.taskClaimed.fill(true);
            state.checkinClaimed.fill(true);
            state.monthlyClaimed = true;
        }
        return state;
    }

    QSettings settings(accountSettingsPath(), QSettings::IniFormat);
    const QString dailyKey = date.toString(QStringLiteral("yyyyMMdd"));
    if (settings.value(QStringLiteral("challenge/daily/date")).toString() == dailyKey) {
        state.taskClaimed = deserialize(
            settings.value(QStringLiteral("challenge/daily/tasks")).toString(), taskCount);
        state.checkinClaimed = deserialize(
            settings.value(QStringLiteral("challenge/daily/checkins")).toString(), checkinCount);
    }
    const QStringList taskIds = kTaskIds;
    for (int index = 0; index < state.taskClaimed.size() && index < taskIds.size(); ++index) {
        const QString key = QStringLiteral("challenge/daily/%1/%2").arg(dailyKey, taskIds.at(index));
        state.taskClaimed[index] = state.taskClaimed.at(index) || settings.value(key, false).toBool();
    }
    for (int index = 0; index < state.checkinClaimed.size(); ++index) {
        const QString key = QStringLiteral("challenge/checkin/%1/checkin-%2").arg(dailyKey).arg(index);
        state.checkinClaimed[index] = state.checkinClaimed.at(index) || settings.value(key, false).toBool();
    }
    const QString monthlyKey = QStringLiteral("challenge/monthly/%1/claimed").arg(state.monthKey);
    const QString monthlyFlag = QStringLiteral("challenge/monthly/%1/month-focus").arg(state.monthKey);
    state.monthlyClaimed = settings.value(monthlyKey, false).toBool() ||
                           settings.value(monthlyFlag, false).toBool();
    if (unresolvedLegacyRewards_) {
        state.taskClaimed.fill(true);
        state.checkinClaimed.fill(true);
        state.monthlyClaimed = true;
    }
    return state;
}

bool UserPreferences::saveChallengeRewardState(const ChallengeRewardState& state)
{
    if (!loaded_ && !load()) return false;
    if (unresolvedLegacyRewards_) return false;
    lastError_.clear();

    if (!state.date.isValid() || state.monthKey != state.date.toString(QStringLiteral("yyyyMM"))) {
        lastError_ = QStringLiteral("无法保存挑战奖励状态：日期与月份无效或不一致");
        return false;
    }
    const QString dailyKey = state.date.toString(QStringLiteral("yyyyMMdd"));
    const QString monthKey = QRegularExpression(QStringLiteral("^\\d{6}$"))
                                 .match(state.monthKey).hasMatch()
        ? state.monthKey : state.date.toString(QStringLiteral("yyyyMM"));
    QSettings settings(accountSettingsPath(), QSettings::IniFormat);
    settings.sync();
    if (!validateSettings(settings, accountSettingsPath(), &lastError_)) return false;
    settings.setAtomicSyncRequired(true);
    settings.setValue(QStringLiteral("challenge/daily/date"), dailyKey);
    settings.setValue(QStringLiteral("challenge/daily/tasks"), serialize(state.taskClaimed));
    settings.setValue(QStringLiteral("challenge/daily/checkins"), serialize(state.checkinClaimed));
    settings.setValue(QStringLiteral("challenge/monthly/%1/claimed").arg(monthKey), state.monthlyClaimed);

    for (int index = 0; index < state.taskClaimed.size() && index < kTaskIds.size(); ++index) {
        if (state.taskClaimed.at(index)) {
            settings.setValue(QStringLiteral("challenge/daily/%1/%2").arg(dailyKey, kTaskIds.at(index)), true);
        }
    }
    for (int index = 0; index < state.checkinClaimed.size(); ++index) {
        if (state.checkinClaimed.at(index)) {
            settings.setValue(QStringLiteral("challenge/checkin/%1/checkin-%2").arg(dailyKey).arg(index), true);
        }
    }
    if (state.monthlyClaimed) {
        settings.setValue(QStringLiteral("challenge/monthly/%1/month-focus").arg(monthKey), true);
    }

    settings.sync();
    if (settings.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("无法保存挑战奖励状态");
        return false;
    }
    return true;
}

QStringList UserPreferences::legacyChallengeClaimKeys() const
{
    if (accountSettingsPath().isEmpty()) return {};
    QSettings settings(accountSettingsPath(), QSettings::IniFormat);
    QStringList keys = legacyClaimKeys(settings);
    if (accountBound_) {
        QSettings deviceSettings(deviceSettingsPath(), QSettings::IniFormat);
        bool validOwner = false;
        const uint32_t ownerId = deviceSettings.value(kLegacyOwnerKey).toUInt(&validOwner);
        if (validOwner && ownerId == userId_) keys.append(legacyClaimKeys(deviceSettings));
    }
    keys.removeDuplicates();
    keys.sort();
    return keys;
}

QString UserPreferences::settingsPath(const QString& directory) const
{
    return QDir(directory).filePath(QStringLiteral("preferences.ini"));
}

QString UserPreferences::deviceSettingsPath() const
{
    return settingsPath(deviceDirectory_);
}

QString UserPreferences::accountSettingsPath() const
{
    return settingsPath(accountDirectory_);
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
    QVector<bool> result(qMax(0, count), false);
    for (int index = 0; index < result.size() && index < value.size(); ++index) {
        result[index] = value.at(index) == QLatin1Char('1');
    }
    return result;
}
