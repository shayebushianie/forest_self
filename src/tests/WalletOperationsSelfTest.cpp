#include "core/CoinManager.h"
#include "config/PlantCatalog.h"
#include "storage/StorageEnvelope.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <iostream>
#include <limits>

namespace {
bool expect(bool ok, const char* message)
{
    if (!ok) std::cerr << message << '\n';
    return ok;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == "--claim") {
        CoinManager wallet;
        wallet.setDataPath(app.arguments()[2].toStdString());
        if (!wallet.load()) return 2;
        const auto result = wallet.grantChallengeReward("challenge/daily/20261006/focus-session", 8);
        return result == CoinManager::OperationResult::Applied || result == CoinManager::OperationResult::AlreadyApplied ? 0 : 3;
    }
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const auto path = QDir(directory.path()).filePath("coins.dat");
    CoinManager wallet;
    wallet.setDataPath(path.toStdString());
    if (!wallet.load() || !wallet.earn(1000)) return 1;
    const auto before = wallet.balance();
    StorageEnvelope::failNextWriteForTesting(path);
    bool ok = expect(wallet.purchasePlant(1) == CoinManager::OperationResult::SaveFailed &&
                     wallet.balance() == before && !wallet.isPlantUnlocked(1), "failed purchase changes neither field");
    ok &= expect(wallet.purchasePlant(1) == CoinManager::OperationResult::Applied &&
                 wallet.balance() == before - PlantCatalog::byType(1).storeCost && wallet.isPlantUnlocked(1),
                 "purchase commits balance and ownership together");
    const auto purchased = wallet.balance();
    ok &= expect(wallet.purchasePlant(1) == CoinManager::OperationResult::AlreadyApplied && wallet.balance() == purchased,
                 "repeat purchase is free of duplicate charges");
    ok &= expect(wallet.purchasePlant(100) == CoinManager::OperationResult::Invalid && !wallet.isPlantUnlocked(100),
                 "unknown plant avoids undefined shifts");
    const QString claim = "challenge/daily/20261006/focus-session";
    StorageEnvelope::failNextWriteForTesting(path);
    ok &= expect(wallet.grantChallengeReward(claim, 8) == CoinManager::OperationResult::SaveFailed &&
                 !wallet.hasChallengeReward(claim) && wallet.balance() == purchased,
                 "claim failure leaves balance and receipt untouched");
    for (int restart = 0; restart < 2; ++restart) {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--claim", path});
        ok &= expect(child.waitForFinished(15000) && child.exitCode() == 0, "claim survives process restart");
    }
    CoinManager reloaded;
    reloaded.setDataPath(path.toStdString());
    ok &= expect(reloaded.load() && reloaded.balance() == purchased + 8 && reloaded.hasChallengeReward(claim),
                 "two processes grant once with persistent receipt");
    const auto total = reloaded.getTotalCoins();
    ok &= expect(reloaded.applyGachaPull("one", "golden-oak", false, 100, 50) == CoinManager::OperationResult::Applied &&
                 reloaded.balance() == purchased + 8 - 50 && reloaded.getTotalCoins() == total,
                 "gacha duplicate net debit does not count refund as income");
    const auto afterPull = reloaded.balance();
    ok &= expect(reloaded.applyGachaPull("one", "golden-oak", false, 100, 50) == CoinManager::OperationResult::AlreadyApplied &&
                 reloaded.balance() == afterPull, "same operation has one receipt");
    ok &= expect(reloaded.applyGachaPull("one", "silver-oak", false, 100, 50) == CoinManager::OperationResult::Invalid,
                 "changed replay cannot change selected variant");
    ok &= expect(reloaded.importChallengeClaims({"challenge/checkin/20261001/checkin-0"}) &&
                 reloaded.balance() == afterPull && reloaded.getTotalCoins() == total,
                 "legacy paid flags migrate without minting coins");
    ok &= expect(!reloaded.earn(std::numeric_limits<uint32_t>::max()), "income overflow is rejected");
    QFile corrupted(path);
    if (!corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    corrupted.write("broken-wallet");
    corrupted.close();
    CoinManager damaged;
    damaged.setDataPath(path.toStdString());
    ok &= expect(!damaged.load() && !damaged.lastError().isEmpty(), "ambiguous wallet rollback blocks automatic loading");
    if (!corrupted.open(QIODevice::ReadOnly)) return 2;
    ok &= expect(corrupted.readAll() == "broken-wallet" && QFile::exists(path + ".bak"),
                 "rollback diagnostic preserves original and backup evidence");
    return ok ? 0 : 1;
}
