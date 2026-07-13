#include "core/AchievementEngine.h"
#include "core/CoinManager.h"
#include "core/ChallengeManager.h"
#include "core/FocusController.h"
#include "core/FocusResultService.h"
#include "core/DashboardSnapshotService.h"
#include "core/FocusStatisticsQuery.h"
#include "config/PlantCatalog.h"
#include "core/GachaManager.h"
#include "core/GuardianManager.h"
#include "core/TagManager.h"
#include "plant/VariantPlants.h"
#include "plant/PlantFactory.h"
#include "plant/AbstractPlant.h"
#include "storage/DatabaseManager.h"
#include "storage/BackupService.h"
#include "storage/StorageEnvelope.h"
#include "storage/UserManager.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTime>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <type_traits>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        return false;
    }
    return true;
}

bool testCrashRecovery(const QString& path)
{
    FocusRecord running;
    running.status = FocusRecordStatus::Running;
    {
        std::ofstream out(path.toStdString(), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&running), sizeof(FocusRecord));
    }

    DatabaseManager db(path.toStdString());
    if (!expect(db.open(), "database opens for crash recovery")) return false;
    auto recovered = db.readById(0);
    return expect(recovered.has_value(), "recovered record is readable") &&
           expect(recovered->status == FocusRecordStatus::Abandoned, "running record becomes abandoned") &&
           expect(recovered->growthStage == 4, "recovered abandoned record withers");
}

bool testPartialRecordRepair(const QString& path)
{
    FocusRecord complete;
    {
        std::ofstream out(path.toStdString(), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&complete), sizeof(FocusRecord));
        const char trailing[] = "bad";
        out.write(trailing, sizeof(trailing));
    }

    DatabaseManager db(path.toStdString());
    if (!expect(db.open(), "database opens after trimming partial record")) return false;
    return expect(QFileInfo::exists(db.databasePath()), "legacy records are migrated into SQLite") &&
           expect(db.readById(0).has_value(), "partial trailing bytes are trimmed during import") &&
           expect(db.count() == 1, "valid record count is preserved");
}

bool testBackupRecovery(const QString& path)
{
    {
        DatabaseManager db(path.toStdString());
        if (!expect(db.open(), "backup database opens")) return false;
        FocusRecord first;
        first.status = FocusRecordStatus::Success;
        if (!expect(db.append(first) == 0, "first record is saved")) return false;
        FocusRecord second;
        second.status = FocusRecordStatus::Abandoned;
        if (!expect(db.append(second) == 1, "second record creates a backup")) return false;
    }

    QFile damaged(QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName() + QStringLiteral(".sqlite")));
    if (!damaged.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    damaged.write("broken");
    damaged.close();

    DatabaseManager db(path.toStdString());
    return expect(!db.open(), "corrupt SQLite database is rejected without silent data loss") &&
           expect(!db.lastError().isEmpty(), "SQLite corruption produces a diagnostic error");
}

bool testFocusCheckpointRecovery(const QString& path)
{
    DatabaseManager activeDb(path.toStdString());
    if (!expect(activeDb.open(), "checkpoint database opens")) return false;
    FocusController controller(activeDb);
    controller.startFocus(0, 10, FocusController::TimerMode::COUNTDOWN,
                          FocusController::FocusMode::STRICT_MODE, 0, false, false);
    for (int i = 0; i < 30; ++i) controller.tick();

    DatabaseManager recoveredDb(path.toStdString());
    if (!expect(recoveredDb.open(), "interrupted session database reopens")) return false;
    const auto record = recoveredDb.readById(0);
    return expect(record.has_value(), "interrupted record remains available") &&
           expect(record->status == FocusRecordStatus::Abandoned, "interrupted record becomes abandoned") &&
           expect(record->actualSeconds == 30, "interrupted record preserves its checkpoint");
}

bool testLegacyWalletLoad(const QString& path)
{
    const uint32_t balance = 42;
    {
        std::ofstream out(path.toStdString(), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&balance), sizeof(balance));
    }

    CoinManager wallet;
    wallet.setDataPath(path.toStdString());
    return expect(wallet.load(), "legacy wallet loads") &&
           expect(wallet.balance() == balance, "legacy balance is preserved") &&
           expect(wallet.getTotalCoins() == balance, "legacy total coins are inferred") &&
           expect(wallet.getUnlockedPlantMask() == 1u, "oak is unlocked for legacy wallet");
}

bool testBackupRestore(const QString& root)
{
    const QString dataDir = QDir(root).filePath(QStringLiteral("backup_data"));
    const QString backups = QDir(root).filePath(QStringLiteral("backup_output"));
    QDir().mkpath(dataDir);
    const QString dataFile = QDir(dataDir).filePath(QStringLiteral("sample.dat"));
    {
        QFile file(dataFile);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        file.write("before");
    }
    QString backupDir, error;
    if (!expect(BackupService::createBackup(dataDir, backups, &backupDir, &error), "backup is created")) return false;
    {
        QFile file(dataFile);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
        file.write("after");
    }
    if (!expect(BackupService::requestRestore(dataDir, backupDir, &error), "restore request is stored")) return false;
    QString restoredFrom;
    if (!expect(BackupService::restoreIfRequested(dataDir, &restoredFrom, &error), "restore request is applied")) return false;
    QFile restored(dataFile);
    return expect(restored.open(QIODevice::ReadOnly | QIODevice::Text) && restored.readAll() == "before",
                  "restore replaces changed data with the backup") &&
           expect(restoredFrom == backupDir, "restore reports the source backup");
}

bool testNewWalletUnlocksDefaultPlant(const QString& path)
{
    CoinManager wallet;
    wallet.setDataPath(path.toStdString());
    return expect(wallet.load(), "new wallet loads without an existing file") &&
           expect(wallet.balance() == 0, "new wallet starts with zero coins") &&
           expect(wallet.getUnlockedPlantMask() == 1u, "new wallet unlocks oak by default") &&
           expect(wallet.isPlantUnlocked(0), "new wallet can use oak immediately");
}

bool testWalletBackupRecovery(const QString& path)
{
    {
        CoinManager wallet;
        wallet.setDataPath(path.toStdString());
        if (!expect(wallet.load(), "wallet opens for backup recovery")) return false;
        if (!expect(wallet.earn(10), "wallet writes first balance")) return false;
        if (!expect(wallet.earn(20), "wallet writes second balance")) return false;
    }

    QFile damaged(path);
    if (!damaged.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    damaged.write("broken");
    damaged.close();

    CoinManager wallet;
    wallet.setDataPath(path.toStdString());
    return expect(wallet.load(), "wallet restores from backup") &&
           expect(wallet.balance() == 10, "wallet uses the last valid backup balance");
}

bool testLegacyUserMigration(const QString& path)
{
    UserRecord legacy;
    legacy.userId = 1;
    std::strncpy(legacy.username, "legacy_user", sizeof(legacy.username) - 1);
    {
        std::ofstream out(path.toStdString(), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&legacy), sizeof(legacy));
    }

    UserManager users(path.toStdString());
    if (!expect(users.open(), "legacy user file migrates")) return false;
    const auto stored = StorageEnvelope::read(path, 0x46555331u);
    return expect(stored.hasPayload(), "user records use a versioned envelope after migration") &&
           expect(users.registerUser("new_user", "test_password"), "new user persists atomically") &&
           expect(users.getAllUsers().size() == 2, "migrated and new users are retained");
}

bool testAchievementMaskMapping(const QString& sessionPath, const QString& walletPath)
{
    DatabaseManager db(sessionPath.toStdString());
    if (!expect(db.open(), "achievement database opens")) return false;

    CoinManager wallet;
    wallet.setDataPath(walletPath.toStdString());
    if (!expect(wallet.load(), "achievement wallet loads")) return false;

    AchievementEngine achievements(db, wallet);
    AchievementEngine::Context ctx;
    ctx.lastSessionSuccess = true;
    ctx.lastPlantType = QStringLiteral("OakTree");
    ctx.totalSessions = 1;

    achievements.checkAll(ctx);
    const uint32_t mask = wallet.getUnlockedAchievementMask();
    return expect((mask & (1u << 6)) != 0, "first_oak uses bit 6") &&
           expect((mask & (1u << 11)) != 0, "first_focus uses bit 11") &&
           expect((mask & (1u << 0)) == 0, "first_oak does not overwrite old bit 0") &&
           expect(wallet.balance() == 70, "achievement rewards are paid once on unlock");
}

bool testFocusControllerCompletionKeepsDashboardFields(const QString& path)
{
    DatabaseManager db(path.toStdString());
    if (!expect(db.open(), "focus controller database opens")) return false;

    FocusController controller(db);
    const qint64 beforeStart = QDateTime::currentDateTime().toSecsSinceEpoch();
    controller.startFocus(3, 10,
                          FocusController::TimerMode::COUNTDOWN,
                          FocusController::FocusMode::STRICT_MODE,
                          7,
                          true,
                          false);

    auto running = db.readById(0);
    if (!expect(running.has_value(), "running focus record is created")) return false;
    if (!expect(running->status == FocusRecordStatus::Running, "running focus record uses running status")) return false;
    if (!expect(running->plantType == 3, "running focus record stores selected plant")) return false;
    if (!expect(running->tagId == 7, "running focus record stores selected tag")) return false;
    if (!expect(running->startTimestamp >= static_cast<uint64_t>(beforeStart),
                "running focus record stores session start timestamp")) return false;
    const QDateTime runningLocalStart = QDateTime::fromSecsSinceEpoch(
        static_cast<qint64>(running->startTimestamp), Qt::LocalTime);
    if (!expect(runningLocalStart.date() == QDate::currentDate(),
                "running focus record binds to current system local date")) return false;

    for (int i = 0; i < 600; ++i) {
        controller.tick();
    }

    auto completed = db.readById(0);
    return expect(completed.has_value(), "completed focus record is readable") &&
           expect(completed->status == FocusRecordStatus::Success, "completed focus record uses success status") &&
           expect(completed->plantType == 3, "completed focus record keeps selected plant") &&
           expect(completed->tagId == 7, "completed focus record keeps selected tag") &&
           expect(completed->plannedMinutes == 10, "completed focus record keeps planned minutes") &&
           expect(completed->actualSeconds == 600, "completed focus record stores real duration") &&
           expect(completed->startTimestamp == running->startTimestamp,
                   "completed focus record keeps session start timestamp for dashboard filters");
}

bool testGachaDefinitionsAndPersistence(const QString& path)
{
    static_assert(std::is_base_of_v<OakTree, GoldenOak>);
    static_assert(std::is_base_of_v<OakTree, SilverOak>);
    static_assert(std::is_base_of_v<PineTree, RedPine>);
    static_assert(std::is_base_of_v<PineTree, BluePine>);
    static_assert(std::is_base_of_v<Rose, PinkRose>);
    static_assert(std::is_base_of_v<Rose, PurpleRose>);
    static_assert(std::is_base_of_v<GinkgoTree, GoldenGinkgo>);
    static_assert(std::is_base_of_v<GinkgoTree, RedGinkgo>);
    static_assert(std::is_base_of_v<Sunflower, GoldenSunflower>);
    static_assert(std::is_base_of_v<Sunflower, BlackSunflower>);
    static_assert(std::is_base_of_v<Cactus, GemCactus>);
    static_assert(std::is_base_of_v<Cactus, FlameCactus>);

    {
        GachaManager gacha(path);
        if (!expect(gacha.variantCount() == 12, "gacha keeps twelve predefined variants")) return false;
        const auto& variants = gacha.allVariants();
        if (!expect(!variants.isEmpty(), "gacha variant definitions are available")) return false;
        if (!expect(variants.first().displayName == QStringLiteral("金橡树"),
                    "gacha variant names are valid Chinese text")) return false;
        if (!expect(variants.first().basePlantType == 0,
                     "gacha variant inherits the expected shop plant type")) return false;
        for (const auto& variant : variants) {
            if (!expect(!variant.description.isEmpty(),
                        "every discovered variant has prepared introduction text")) return false;
        }
        RedPine redPine;
        if (!expect(redPine.getPlantType() == 1,
                    "pine variant inherits the pine shop plant type")) return false;
        redPine.grow(600, 600);
        if (!expect(redPine.getGrowthStage() == 3,
                    "variant inherits base plant growth behavior")) return false;
        if (!expect(gacha.unlockVariant(QStringLiteral("golden_oak")), "gacha unlocks first variant")) return false;
        if (!expect(gacha.unlockVariant(QStringLiteral("flame_cactus")), "gacha unlocks last variant")) return false;
    }

    GachaManager reloaded(path);
    return expect(reloaded.unlockedCount() == 2, "gacha bitmask persistence keeps unlocked count") &&
           expect(reloaded.isUnlocked(QStringLiteral("golden_oak")), "gacha bitmask keeps first variant") &&
           expect(reloaded.isUnlocked(QStringLiteral("flame_cactus")), "gacha bitmask keeps last variant");
}

bool testGachaCoinFlow(const QString& variantPath, const QString& walletPath)
{
    CoinManager wallet;
    wallet.setDataPath(walletPath.toStdString());
    if (!expect(wallet.load(), "gacha wallet loads")) return false;

    GachaManager gacha(variantPath);
    auto insufficient = gacha.pull(wallet);
    if (!expect(!insufficient.success, "gacha pull fails without enough coins")) return false;
    if (!expect(wallet.balance() == 0, "failed gacha pull keeps balance")) return false;
    if (!expect(gacha.unlockedCount() == 0, "failed gacha pull does not unlock variants")) return false;

    wallet.earn(GachaManager::pullCost());
    const uint32_t totalBeforeDuplicate = wallet.getTotalCoins();
    for (const auto& variant : gacha.allVariants()) {
        gacha.unlockVariant(variant.id);
    }

    auto duplicate = gacha.pull(wallet);
    return expect(duplicate.success, "duplicate gacha pull succeeds with enough coins") &&
           expect(!duplicate.isNew, "all-unlocked gacha pull is duplicate") &&
           expect(duplicate.coinRefund == GachaManager::duplicateCoinRefund(), "duplicate gacha pull reports coin refund") &&
           expect(wallet.balance() == static_cast<uint32_t>(GachaManager::duplicateCoinRefund()),
                  "duplicate gacha pull refunds coins to balance") &&
           expect(wallet.getTotalCoins() == totalBeforeDuplicate,
                  "duplicate gacha refund does not inflate total earned coins");
}

bool testManagerFirstLaunchLoads(const QString& dirPath)
{
    DatabaseManager db(QDir(dirPath).filePath("manager_first_launch_sessions.dat").toStdString());
    if (!expect(db.open(), "first launch manager database opens")) return false;

    ChallengeManager challenges(QDir(dirPath).filePath("missing_challenges.dat"), &db, 1);
    TagManager tags(QDir(dirPath).filePath("missing_tags.dat"));
    GuardianManager guardian(QDir(dirPath).filePath("missing_guardian.dat"));

    return expect(challenges.load(), "missing challenges file is treated as first launch") &&
           expect(tags.load(), "missing tags file is treated as first launch") &&
           expect(guardian.load(), "missing guardian file is treated as first launch");
}

bool testChallengeActionsPersist(const QString& sessionPath, const QString& challengePath)
{
    DatabaseManager db(sessionPath.toStdString());
    if (!expect(db.open(), "challenge persistence database opens")) return false;

    {
        ChallengeManager creator(challengePath, &db, 1);
        if (!expect(creator.load(), "challenge manager loads initially")) return false;
        if (!expect(creator.createChallenge(2, 60, QDate::currentDate().addDays(3)),
                    "created challenge is saved immediately")) return false;
    }

    {
        ChallengeManager target(challengePath, &db, 2);
        if (!expect(target.load(), "target challenge manager reloads created challenge")) return false;
        const auto incoming = target.incoming();
        if (!expect(incoming.size() == 1, "incoming challenge survives manager restart")) return false;
        if (!expect(target.acceptChallenge(incoming[0].id),
                    "accepted challenge is saved immediately")) return false;
    }

    ChallengeManager creatorReloaded(challengePath, &db, 1);
    if (!expect(creatorReloaded.load(), "creator challenge manager reloads accepted challenge")) return false;
    const auto outgoing = creatorReloaded.outgoing();
    return expect(outgoing.size() == 1, "outgoing challenge survives accept restart") &&
           expect(outgoing[0].status == ChallengeStatus::ACCEPTED,
                  "accepted challenge status persists");
}

bool testChallengeProgressUsesLocalUserOnly(const QString& sessionPath, const QString& challengePath)
{
    DatabaseManager db(sessionPath.toStdString());
    if (!expect(db.open(), "challenge progress database opens")) return false;

    FocusRecord record;
    record.recordId = 0;
    record.status = FocusRecordStatus::Success;
    record.actualSeconds = 20 * 60;
    record.startTimestamp = static_cast<uint64_t>(QDateTime::currentDateTime().toSecsSinceEpoch());
    db.append(record);

    ChallengeManager creator(challengePath, &db, 1);
    if (!expect(creator.load(), "challenge progress manager loads")) return false;
    if (!expect(creator.createChallenge(2, 60, QDate::currentDate().addDays(3)),
                "challenge progress challenge is created")) return false;

    ChallengeManager target(challengePath, &db, 2);
    if (!expect(target.load(), "target progress manager loads")) return false;
    const auto incoming = target.incoming();
    if (!expect(incoming.size() == 1, "target has challenge to accept")) return false;
    if (!expect(target.acceptChallenge(incoming[0].id), "target accepts challenge for progress test")) return false;

    ChallengeManager creatorReloaded(challengePath, &db, 1);
    if (!expect(creatorReloaded.load(), "creator reloads progress challenge")) return false;
    const auto active = creatorReloaded.active();
    return expect(active.size() == 1, "creator sees active challenge") &&
           expect(active[0].creatorProgress == 20, "local creator progress uses local focus records") &&
           expect(active[0].targetProgress == 0, "remote target progress is not filled from local records");
}

bool testFocusResultPipeline(const QString& dirPath)
{
    DatabaseManager db(QDir(dirPath).filePath("pipeline_sessions.dat").toStdString());
    if (!expect(db.open(), "pipeline database opens")) return false;

    CoinManager wallet;
    wallet.setDataPath(QDir(dirPath).filePath("pipeline_coins.dat").toStdString());
    if (!expect(wallet.load(), "pipeline wallet opens")) return false;

    GuardianManager guardian(QDir(dirPath).filePath("pipeline_guardian.dat"));
    if (!expect(guardian.load(), "pipeline guardian opens")) return false;

    const QString challengePath = QDir(dirPath).filePath("pipeline_challenges.dat");
    {
        ChallengeManager creator(challengePath, &db, 1);
        if (!expect(creator.load(), "pipeline creator challenge opens")) return false;
        if (!expect(creator.createChallenge(2, 10, QDate::currentDate().addDays(1)),
                    "pipeline challenge is created")) return false;
    }
    {
        ChallengeManager target(challengePath, &db, 2);
        if (!expect(target.load(), "pipeline target challenge opens")) return false;
        const auto incoming = target.incoming();
        if (!expect(incoming.size() == 1, "pipeline challenge is received")) return false;
        if (!expect(target.acceptChallenge(incoming.first().id),
                    "pipeline challenge is accepted")) return false;
    }
    ChallengeManager challenges(challengePath, &db, 1);
    if (!expect(challenges.load(), "pipeline creator challenge reloads")) return false;

    FocusRecord completed;
    completed.status = FocusRecordStatus::Success;
    completed.plantType = 0;
    completed.actualSeconds = 10 * 60;
    completed.coinsEarned = 2;
    completed.focusMode = PersistedFocusMode::Deep;
    completed.startTimestamp = static_cast<uint64_t>(QDateTime::currentDateTime().toSecsSinceEpoch());
    if (!expect(db.append(completed) == 0, "pipeline completed focus is stored")) return false;

    AchievementEngine achievements(db, wallet);
    FocusResultService service(db, wallet, achievements, nullptr, nullptr, &guardian, &challenges);
    const auto outcome = service.applyTerminalRecord(0);
    if (!expect(outcome.applied && outcome.completed, "pipeline applies completed focus once")) return false;
    if (!expect(outcome.focusCoins == 2, "pipeline uses record reward amount")) return false;
    if (!expect(wallet.balance() == 252, "pipeline settles focus and chained achievement coins")) return false;
    if (!expect(guardian.todayMinutes() == 10, "pipeline updates guardian minutes")) return false;
    const auto history = challenges.history();
    if (!expect(history.size() == 1 && history.first().status == ChallengeStatus::COMPLETED,
                "pipeline updates challenge completion")) return false;

    const uint32_t balanceAfterFirstApply = wallet.balance();
    const uint32_t guardianAfterFirstApply = guardian.todayMinutes();
    const auto repeated = service.applyTerminalRecord(0);
    if (!expect(repeated.applied && repeated.alreadyApplied,
                "pipeline ignores a duplicate terminal event")) return false;
    if (!expect(wallet.balance() == balanceAfterFirstApply &&
                guardian.todayMinutes() == guardianAfterFirstApply,
                "duplicate terminal event does not duplicate side effects")) return false;

    FocusRecord abandoned;
    abandoned.status = FocusRecordStatus::Abandoned;
    abandoned.plantType = 1;
    abandoned.actualSeconds = 60;
    abandoned.startTimestamp = static_cast<uint64_t>(QDateTime::currentDateTime().toSecsSinceEpoch());
    if (!expect(db.append(abandoned) == 1, "pipeline abandoned focus is stored")) return false;
    const auto failed = service.applyTerminalRecord(1);
    if (!expect(failed.applied && !failed.completed, "pipeline applies abandoned focus") ||
        !expect(wallet.balance() == balanceAfterFirstApply,
                "abandoned focus does not award focus coins") ||
        !expect(guardian.todayMinutes() == guardianAfterFirstApply,
                "abandoned focus does not add guardian minutes")) {
        return false;
    }

    DashboardSnapshotService snapshots(db, wallet, nullptr, &guardian, &challenges);
    const auto forest = snapshots.forest();
    const auto challenge = snapshots.challenges();
    const auto guardianSnapshot = snapshots.guardian();
    return expect(forest.records.size() == 2, "forest snapshot reads all focus records") &&
           expect(challenge.records.size() == 2 && challenge.coinBalance == wallet.balance(),
                  "challenge snapshot uses current wallet and records") &&
           expect(challenge.challenges.isEmpty(), "completed challenge leaves active snapshot") &&
           expect(guardianSnapshot.todayMinutes == guardianAfterFirstApply,
                  "guardian snapshot uses persisted guardian state");
}

bool testStatisticsQueryAndPlantCatalog()
{
    const QDate today = QDate::currentDate();
    auto makeRecord = [&](uint32_t id, FocusRecordStatus status, uint32_t seconds, int hour,
                          uint32_t tagId, uint32_t plantType, int dayOffset) {
        FocusRecord record;
        record.recordId = id;
        record.status = status;
        record.actualSeconds = seconds;
        record.tagId = tagId;
        record.plantType = plantType;
        record.startTimestamp = static_cast<uint64_t>(QDateTime(today.addDays(dayOffset),
            QTime(hour, 0), Qt::LocalTime).toSecsSinceEpoch());
        return record;
    };
    const std::vector<FocusRecord> records = {
        makeRecord(0, FocusRecordStatus::Success, 30 * 60, 8, 1, 0, 0),
        makeRecord(1, FocusRecordStatus::Abandoned, 5 * 60, 14, 1, 1, 0),
        makeRecord(2, FocusRecordStatus::Success, 20 * 60, 10, 2, 2, -2),
    };
    const QMap<uint32_t, QString> tags = {{1, QStringLiteral("学习")}, {2, QStringLiteral("阅读")}};
    const auto day = FocusStatisticsQuery::query(records, tags, today, FocusStatisticsQuery::Period::Day);
    if (!expect(day.focusCount == 2 && day.successCount == 1 && day.abandonedCount == 1,
                "day statistics filter focus outcomes")) return false;
    if (!expect(day.totalMinutes == 30 && day.timeBuckets[2] == 30,
                "day statistics use successful duration in the correct time bucket")) return false;
    if (!expect(day.projects.size() == 1 && day.projects.first().totalCount == 2 &&
                day.projects.first().successCount == 1 && day.projects.first().abandonedCount == 1,
                "project distribution counts success and abandonment by tag")) return false;
    const auto week = FocusStatisticsQuery::query(records, tags, today, FocusStatisticsQuery::Period::Week);
    if (!expect(week.focusCount >= 2 && week.timeBuckets.size() == 7,
                "week statistics use seven local-date buckets")) return false;
    const auto month = FocusStatisticsQuery::query(records, tags, today, FocusStatisticsQuery::Period::Month);
    if (!expect(month.timeBuckets.size() == today.daysInMonth(),
                "month statistics match the selected month length")) return false;
    const auto year = FocusStatisticsQuery::query(records, tags, today, FocusStatisticsQuery::Period::Year);
    if (!expect(year.timeBuckets.size() == 12 && year.focusCount >= 2,
                "year statistics use twelve month buckets")) return false;

    const auto& plants = PlantCatalog::all();
    QSet<uint32_t> types;
    for (const PlantDefinition& plant : plants) {
        types.insert(plant.type);
        if (!expect(!plant.displayName.isEmpty() && !plant.internalName.isEmpty() &&
                    !plant.iconPath.isEmpty() && plant.storeCost > 0,
                    "plant registry definitions are complete")) return false;
    }
    return expect(plants.size() == 6 && types.size() == plants.size(),
                  "plant registry keeps six unique shop plants") &&
           expect(PlantCatalog::byType(0).internalName == QStringLiteral("OakTree") &&
                  PlantCatalog::byType(5).storeCost == 1500,
                  "plant registry keeps existing type and price contracts");
}

bool testPlantFactoryCompatibility()
{
    for (const PlantDefinition& plant : PlantCatalog::all()) {
        const std::unique_ptr<AbstractPlant> instance = PlantFactory::create(plant.type);
        if (!expect(instance != nullptr, "registered plant creates a concrete implementation")) return false;
        if (!expect(instance->getPlantType() == plant.type &&
                    QString::fromStdString(instance->getPlantName()) == plant.internalName,
                    "factory implementation stays aligned with the plant registry")) return false;
    }
    return expect(PlantFactory::create(9999) == nullptr,
                  "factory rejects an unregistered plant type");
}

bool testStatisticsQueryPeriodBoundaries()
{
    const QDate selectedDate(2024, 3, 1);
    auto makeRecord = [](uint32_t id, QDate date, int hour, FocusRecordStatus status,
                         uint32_t seconds, uint32_t tagId, uint32_t plantType) {
        FocusRecord record;
        record.recordId = id;
        record.status = status;
        record.actualSeconds = seconds;
        record.tagId = tagId;
        record.plantType = plantType;
        record.startTimestamp = static_cast<uint64_t>(QDateTime(date, QTime(hour, 0), Qt::LocalTime)
            .toSecsSinceEpoch());
        return record;
    };

    const std::vector<FocusRecord> records = {
        makeRecord(1, QDate(2024, 2, 26), 9, FocusRecordStatus::Success, 10 * 60, 1, 0),
        makeRecord(2, QDate(2024, 3, 1), 12, FocusRecordStatus::Abandoned, 5 * 60, 1, 1),
        makeRecord(3, QDate(2024, 3, 3), 18, FocusRecordStatus::Success, 20 * 60, 2, 2),
        makeRecord(4, QDate(2024, 3, 4), 8, FocusRecordStatus::Success, 30 * 60, 2, 3),
    };
    const QMap<uint32_t, QString> tags = {{1, QStringLiteral("study")}, {2, QStringLiteral("reading")}};
    const auto week = FocusStatisticsQuery::query(records, tags, selectedDate,
                                                   FocusStatisticsQuery::Period::Week);
    if (!expect(week.focusCount == 3 && week.successCount == 2 && week.abandonedCount == 1,
                "week query includes only Monday through Sunday")) return false;
    if (!expect(week.totalMinutes == 30 && week.timeBuckets[0] == 10 && week.timeBuckets[6] == 20,
                "week query assigns successful durations to local weekday buckets")) return false;

    const auto month = FocusStatisticsQuery::query(records, tags, selectedDate,
                                                    FocusStatisticsQuery::Period::Month);
    if (!expect(month.focusCount == 3 && month.timeBuckets.size() == 31 &&
                month.timeBuckets[2] == 20 && month.timeBuckets[3] == 30,
                "month query excludes records before the selected month")) return false;

    const auto year = FocusStatisticsQuery::query(records, tags, selectedDate,
                                                   FocusStatisticsQuery::Period::Year);
    return expect(year.focusCount == 4 && year.timeBuckets[1] == 10 && year.timeBuckets[2] == 50,
                  "year query includes records across months in the selected year");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!expect(dir.isValid(), "temporary directory is available")) return 1;

    bool ok = true;
    ok &= testCrashRecovery(QDir(dir.path()).filePath("crash_sessions.dat"));
    ok &= testPartialRecordRepair(QDir(dir.path()).filePath("partial_sessions.dat"));
    ok &= testBackupRecovery(QDir(dir.path()).filePath("backup_sessions.dat"));
    ok &= testFocusCheckpointRecovery(QDir(dir.path()).filePath("checkpoint_sessions.dat"));
    ok &= testBackupRestore(dir.path());
    ok &= testLegacyWalletLoad(QDir(dir.path()).filePath("legacy_coins.dat"));
    ok &= testNewWalletUnlocksDefaultPlant(QDir(dir.path()).filePath("new_coins.dat"));
    ok &= testWalletBackupRecovery(QDir(dir.path()).filePath("backup_coins.dat"));
    ok &= testLegacyUserMigration(QDir(dir.path()).filePath("legacy_users.dat"));
    ok &= testAchievementMaskMapping(QDir(dir.path()).filePath("achievement_sessions.dat"),
                                     QDir(dir.path()).filePath("achievement_coins.dat"));
    ok &= testFocusControllerCompletionKeepsDashboardFields(QDir(dir.path()).filePath("focus_controller_sessions.dat"));
    ok &= testGachaDefinitionsAndPersistence(QDir(dir.path()).filePath("variants.dat"));
    ok &= testGachaCoinFlow(QDir(dir.path()).filePath("gacha_coin_variants.dat"),
                            QDir(dir.path()).filePath("gacha_coins.dat"));
    ok &= testManagerFirstLaunchLoads(dir.path());
    ok &= testChallengeActionsPersist(QDir(dir.path()).filePath("challenge_sessions.dat"),
                                      QDir(dir.path()).filePath("challenges.dat"));
    ok &= testChallengeProgressUsesLocalUserOnly(QDir(dir.path()).filePath("challenge_progress_sessions.dat"),
                                                 QDir(dir.path()).filePath("challenge_progress.dat"));
    ok &= testFocusResultPipeline(dir.path());
    ok &= testStatisticsQueryAndPlantCatalog();
    ok &= testPlantFactoryCompatibility();
    ok &= testStatisticsQueryPeriodBoundaries();

    return ok ? 0 : 1;
}
