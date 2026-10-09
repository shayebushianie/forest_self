#include "core/AchievementEngine.h"
#include "core/ChallengeRewardService.h"
#include "core/DashboardSnapshotService.h"
#include "core/FocusStatisticsQuery.h"
#include "core/FocusRecordSemantics.h"
#include "core/FocusResultService.h"
#include "storage/BackupService.h"
#include "storage/DatabaseManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QSqlQuery>
#include "config/PlantCatalog.h"
#include "core/GachaManager.h"
#include <iostream>
#include <limits>

namespace {
bool expect(bool ok, const char* text)
{
    if (!ok) std::cerr << "FAILED: " << text << '\n';
    return ok;
}

FocusRecord makeRecord(uint32_t id, QDate date, FocusRecordStatus status, uint32_t seconds,
                       uint32_t tag = 1, uint32_t plant = 0)
{
    FocusRecord record;
    record.recordId = id;
    record.status = status;
    record.actualSeconds = seconds;
    record.tagId = tag;
    record.plantType = plant;
    record.growthStage = status == FocusRecordStatus::Success ? 3 : 4;
    record.startTimestamp = static_cast<uint64_t>(QDateTime(date, QTime(23, 59), Qt::LocalTime).toSecsSinceEpoch());
    return record;
}

bool statisticsSemantics()
{
    const QDate date(2024, 2, 29);
    const std::vector<FocusRecord> records = {
        makeRecord(0, date, FocusRecordStatus::Success, 31, 1),
        makeRecord(1, date, FocusRecordStatus::Success, 31, 2),
        makeRecord(2, date, FocusRecordStatus::Failed, 60, 1),
        makeRecord(3, date, FocusRecordStatus::Abandoned, 60, 2),
        makeRecord(4, date, FocusRecordStatus::Running, 60, 1)
    };
    const auto data = FocusStatisticsQuery::query(records, {{1, "same"}, {2, "same"}}, date,
                                                 FocusStatisticsQuery::Period::Day);
    bool ok = expect(data.focusCount == 4 && data.islandPlants.size() == 4,
                     "failed records remain visible; running records are excluded");
    ok &= expect(data.totalMinutes == 1 && data.timeBuckets.last() == 1,
                 "aggregate exact seconds before truncating minutes; never round each record up");
    ok &= expect(data.projects.size() == 2, "different tag IDs cannot merge because names match");
    ok &= expect(data.totalSeconds == 62 && data.failedCount == 1 && data.abandonedCount == 1 &&
                 data.successCount + data.failedCount + data.abandonedCount + data.interruptedCount == data.focusCount,
                 "precise seconds and terminal categories conserve the raw facts");
    const auto nextDay = FocusStatisticsQuery::query(records, {}, date.addDays(1), FocusStatisticsQuery::Period::Day);
    ok &= expect(nextDay.focusCount == 0, "a session that crosses midnight stays attributed to its start date");
    for (const auto period : {FocusStatisticsQuery::Period::Week, FocusStatisticsQuery::Period::Month,
                              FocusStatisticsQuery::Period::Year}) {
        const auto aggregate = FocusStatisticsQuery::query(records, {}, date, period);
        ok &= expect(aggregate.focusCount == 4 && aggregate.totalSeconds == 62 && aggregate.totalMinutes == 1,
                     "all period queries preserve the same terminal and effective-time semantics");
    }
    return ok;
}

bool challengeSemantics()
{
    const QDate date(2024, 2, 29);
    const auto baseline = ChallengeRewardService::progress(date, {}, {});
    Challenge friendChallenge{};
    friendChallenge.id = 1;
    friendChallenge.status = ChallengeStatus::ACCEPTED;
    friendChallenge.targetMinutes = 90;
    friendChallenge.creatorProgress = 999;
    friendChallenge.endDate = date.addDays(4);
    const auto changed = ChallengeRewardService::progress(date, {friendChallenge, friendChallenge}, {});
    bool ok = expect(changed.monthTarget == baseline.monthTarget && changed.monthMinutes == 0 &&
                     changed.plantType == baseline.plantType && changed.remainingDays == baseline.remainingDays,
                     "monthly focus goal and featured plant are independent of local friend challenges");
    for (int i = 0; i < baseline.tasks.size(); ++i)
        ok &= expect(changed.tasks[i].target == baseline.tasks[i].target,
                     "daily task targets are stable within a period");
    auto yesterday = makeRecord(0, date.addDays(-1), FocusRecordStatus::Success, 30000);
    const auto old = ChallengeRewardService::progress(date, {}, {yesterday});
    ok &= expect(old.tasks[3].progress == 0, "daily growth excludes previous days in this month");
    const auto shortRecords = ChallengeRewardService::progress(date, {}, {
        makeRecord(1, date, FocusRecordStatus::Success, 31, 1, baseline.plantType),
        makeRecord(2, date, FocusRecordStatus::Success, 31, 1, baseline.plantType)});
    ok &= expect(shortRecords.todayMinutes == 1 && shortRecords.monthMinutes == 1 &&
                 shortRecords.tasks[2].progress == 1,
                 "challenge minute progress uses exact aggregated seconds and displayed plant identity");
    const auto forest = FocusStatisticsQuery::query({
        makeRecord(1, date, FocusRecordStatus::Success, 31, 1, baseline.plantType),
        makeRecord(2, date, FocusRecordStatus::Success, 31, 1, baseline.plantType)}, {}, date,
        FocusStatisticsQuery::Period::Day);
    ok &= expect(shortRecords.todayGrowth == forest.growthValue && shortRecords.todaySessions == forest.successCount,
                 "challenge growth and success counts agree with forest for identical records");
    ok &= expect(baseline.monthTarget == 29 * 25 && baseline.remainingDays == 0,
                 "leap-month goal and month-end refresh are calendar based");
    const auto march = ChallengeRewardService::progress(date.addDays(1), {}, {yesterday});
    ok &= expect(march.monthMinutes == 0 && march.tasks[3].progress == 0,
                 "new month does not inherit the previous month or day progress");
    return ok;
}

bool achievementsAndSnapshot()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    DatabaseManager database(QDir(temporary.path()).filePath("sessions.dat").toStdString());
    CoinManager wallet;
    wallet.setDataPath(QDir(temporary.path()).filePath("coins.dat").toStdString());
    if (!expect(database.open(), "business fixture database opens")) {
        std::cerr << database.lastError().toStdString() << '\n';
        return false;
    }
    if (!expect(wallet.load(), "business fixture wallet opens")) return false;
    AchievementEngine engine(database, wallet);
    AchievementEngine::Context context;
    context.totalSessions = 10;
    context.totalSuccessCount = 1;
    context.totalFailCount = 9;
    context.lastSessionSuccess = true;
    engine.checkAll(context);
    bool ok = true;
    for (const auto& info : engine.all())
        if (info.id == "dedicated") ok &= expect(!info.unlocked, "nine abandons and one success do not unlock ten completions");
    const QString path = QDir(temporary.path()).filePath("challenges.dat");
    {
        ChallengeManager creator(path, &database, 1);
        if (!creator.load() || !creator.createChallenge(2, 25, QDate::currentDate().addDays(1))) return false;
    }
    ChallengeManager target(path, &database, 2);
    if (!target.load()) return false;
    DashboardSnapshotService snapshots(database, wallet, nullptr, nullptr, &target);
    ok &= expect(snapshots.challenges().challenges.size() == 1, "incoming pending challenge appears only once in snapshot");
    ChallengeManager unrelated(path, &database, 3);
    if (!unrelated.load()) return false;
    ok &= expect(unrelated.active().isEmpty(), "local challenge list excludes other accounts' relationships");
    for (int i = 0; i < 9; ++i)
        if (database.append(makeRecord(i, QDate::currentDate(), FocusRecordStatus::Abandoned, 90)) != static_cast<uint32_t>(i)) return false;
    if (database.append(makeRecord(9, QDate::currentDate(), FocusRecordStatus::Success, 60)) != 9) return false;
    FocusResultService results(database, wallet, engine, nullptr, nullptr, nullptr, nullptr);
    if (!results.applyTerminalRecord(9).applied) return false;
    for (const auto& info : engine.all())
        if (info.id == "dedicated") ok &= expect(!info.unlocked, "real result service does not count abandonment as completion");
    if (!wallet.grantAchievement(12, 80)) return false;
    AchievementEngine historical(database, wallet);
    historical.checkAll(context);
    for (const auto& info : historical.all())
        if (info.id == "dedicated") ok &= expect(info.unlocked, "already granted historical achievements are not revoked");
    return ok;
}

bool catalogAchievements()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    DatabaseManager database(QDir(temporary.path()).filePath("sessions.sqlite").toStdString());
    CoinManager wallet;
    wallet.setDataPath(QDir(temporary.path()).filePath("coins.dat").toStdString());
    if (!database.open() || !wallet.load()) return false;
    AchievementEngine engine(database, wallet);
    FocusResultService results(database, wallet, engine, nullptr, nullptr, nullptr, nullptr);
    const auto& plants = PlantCatalog::all();
    const auto missingType = plants.last().type;
    for (int i = 0; i + 1 < plants.size(); ++i) {
        if (database.append(makeRecord(i, QDate::currentDate(), FocusRecordStatus::Success, 1, 1,
                                       plants[i].type)) == std::numeric_limits<uint32_t>::max()) return false;
    }
    for (const auto status : {FocusRecordStatus::Failed, FocusRecordStatus::Running}) {
        if (database.append(makeRecord(0, QDate::currentDate(), status, 1, 1, missingType)) ==
            std::numeric_limits<uint32_t>::max()) return false;
    }
    if (database.append(makeRecord(0, QDate::currentDate(), FocusRecordStatus::Success, 1, 1, 9999)) ==
        std::numeric_limits<uint32_t>::max()) return false;
    results.evaluateAchievements();
    bool ok = expect(results.lastAchievementEvaluationSucceeded() &&
                     (wallet.getUnlockedAchievementMask() & (1u << 5)) == 0,
                     "unknown, failed, and running plants cannot complete the catalog");
    for (const auto& plant : plants) if (!wallet.unlockPlant(plant.type)) return false;
    if (database.append(makeRecord(0, QDate::currentDate(), FocusRecordStatus::Success, 1, 1, missingType)) ==
        std::numeric_limits<uint32_t>::max()) return false;
    results.evaluateAchievements();
    ok &= expect(results.lastAchievementEvaluationSucceeded() &&
                 (wallet.getUnlockedAchievementMask() & (1u << 5)) != 0 &&
                 (wallet.getUnlockedAchievementMask() & (1u << 10)) != 0,
                 "all registered success types and owned plants unlock the legacy collection bits");
    const auto balance = wallet.balance();
    results.evaluateAchievements();
    ok &= expect(wallet.balance() == balance, "catalog achievements pay once");
    const auto unknown = FocusStatisticsQuery::query(
        {makeRecord(0, QDate::currentDate(), FocusRecordStatus::Success, 1, 1, 9999)}, {},
        QDate::currentDate(), FocusStatisticsQuery::Period::Day);
    ok &= expect(unknown.trees.size() == 1 && unknown.trees.first().plantType == 9999 &&
                 unknown.trees.first().name == QStringLiteral("未知植物"),
                 "unknown historical type remains a raw fact, not an oak");

    GachaManager variants(QDir(temporary.path()).filePath("variants.dat"));
    if (!variants.load()) return false;
    for (const auto& variant : variants.allVariants())
        ok &= expect(PlantCatalog::isKnown(variant.basePlantType) &&
                     variant.basePlant == PlantCatalog::byType(variant.basePlantType).internalName,
                     "variant inherited identity agrees with the base catalog");
    AchievementEngine::Context incomplete;
    incomplete.variantCount = variants.variantCount() - 1;
    incomplete.availableVariantCount = variants.variantCount();
    engine.checkAll(incomplete);
    ok &= expect((wallet.getUnlockedAchievementMask() & (1u << 22)) == 0,
                 "incomplete variant collection does not unlock mastery");
    ++incomplete.variantCount;
    engine.checkAll(incomplete);
    ok &= expect((wallet.getUnlockedAchievementMask() & (1u << 22)) != 0,
                 "mastery uses the defined variant count");
    CoinManager persisted;
    persisted.setDataPath(QDir(temporary.path()).filePath("coins.dat").toStdString());
    if (!persisted.load()) return false;
    AchievementEngine reloaded(database, persisted);
    reloaded.checkAll({});
    ok &= expect((persisted.getUnlockedAchievementMask() & ((1u << 5) | (1u << 10) | (1u << 22))) ==
                 ((1u << 5) | (1u << 10) | (1u << 22)), "historical collection grants are never revoked");
    return ok;
}

bool interruptionAndMigration()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    const QString path = QDir(temporary.path()).filePath("sessions.sqlite");
    const QDate date(2024, 12, 31);
    {
        DatabaseManager database(path.toStdString());
        if (!database.open()) return false;
        if (database.append(makeRecord(0, date, FocusRecordStatus::Running, 45)) != 0 ||
            database.append(makeRecord(1, date, FocusRecordStatus::Abandoned, 45)) != 1 ||
            database.append(makeRecord(2, date, FocusRecordStatus::Failed, 45)) != 2) return false;
    }
    // Recreate the previous real schema (without the new interruption column).
    const QString connection = QStringLiteral("business-schema-fixture");
    bool prepared = false;
    {
        auto sqlite = QSqlDatabase::addDatabase("QSQLITE", connection);
        sqlite.setDatabaseName(path);
        if (sqlite.open()) {
            QSqlQuery query(sqlite);
            prepared = query.exec("ALTER TABLE focus_records DROP COLUMN interrupted") &&
                       query.exec("UPDATE schema_version SET version=3") &&
                       query.exec("CREATE TRIGGER reject_version BEFORE UPDATE ON schema_version "
                                  "BEGIN SELECT RAISE(ABORT,'injected schema update failure'); END");
        }
        sqlite.close();
    }
    QSqlDatabase::removeDatabase(connection);
    if (!expect(prepared, "real schema-v3 fixture and migration failure hook created")) return false;
    {
        DatabaseManager failed(path.toStdString());
        if (!expect(!failed.open(), "schema migration failure stops account loading")) return false;
    }
    bool rolledBack = false;
    {
        auto sqlite = QSqlDatabase::addDatabase("QSQLITE", connection);
        sqlite.setDatabaseName(path);
        if (sqlite.open()) {
            QSqlQuery query(sqlite);
            if (query.exec("SELECT version FROM schema_version") && query.next()) rolledBack = query.value(0).toInt() == 3;
            query.finish();
            bool foundColumn = false;
            if (!query.exec("PRAGMA table_info(focus_records)")) rolledBack = false;
            while (query.next()) foundColumn |= query.value(1).toString() == "interrupted";
            query.finish();
            rolledBack &= !foundColumn && query.exec("DROP TRIGGER reject_version");
        }
        sqlite.close();
    }
    QSqlDatabase::removeDatabase(connection);
    if (!expect(rolledBack, "schema version and column roll back together; retry input intact")) return false;
    DatabaseManager restored(path.toStdString());
    if (!expect(restored.open(), "schema-v3 retry upgrades safely")) return false;
    const auto records = restored.getAllRecords();
    if (!expect(records.size() == 3, "migration preserves all records")) return false;
    bool ok = expect(isInterruptedFocus(records[0]) && !isInterruptedFocus(records[1]) && !isInterruptedFocus(records[2]),
                     "new crash recovery is distinguishable without guessing old abandonment reasons");
    const auto snapshot = FocusStatisticsQuery::query(records, {}, date, FocusStatisticsQuery::Period::Day);
    ok &= expect(snapshot.focusCount == 3 && snapshot.failedCount == 1 && snapshot.abandonedCount == 1 &&
                 snapshot.interruptedCount == 1 && snapshot.islandPlants.size() == 3,
                 "all non-success reasons stay in forest with withered plants");
    for (const auto& plant : snapshot.islandPlants) ok &= expect(plant.abandoned, "all non-success plants are withered");
    QString error;
    const QString csv = QDir(temporary.path()).filePath("history.csv");
    if (!BackupService::exportFocusCsv(restored, csv, &error)) return false;
    QFile file(csv);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto lines = file.readAll().trimmed().split('\n');
    ok &= expect(lines.size() == 4 && lines[1].trimmed().endsWith(",1") &&
                 lines[2].trimmed().endsWith(",0") && lines[3].trimmed().endsWith(",0"),
                 "CSV preserves raw status/effective seconds and interruption flag without passwords");
    return ok;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = statisticsSemantics();
    ok &= challengeSemantics();
    ok &= achievementsAndSnapshot();
    ok &= catalogAchievements();
    ok &= interruptionAndMigration();
    return ok ? 0 : 1;
}
