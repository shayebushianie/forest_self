#include "core/CoinManager.h"
#include "core/GachaManager.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

namespace {

constexpr quint32 kVariantMagic = 0x46564131u;

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size() ||
        !file.flush()) return false;
    file.close();
    return file.error() == QFileDevice::NoError;
}

bool runRecoveryChild(const QString& mode, const QString& variantsPath, const QString& walletPath,
                      const QString& operationId, const QString& variantId)
{
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(),
                {mode, variantsPath, walletPath, operationId, variantId});
    if (!child.waitForFinished(30000) || child.exitCode() != 0) {
        std::cerr << child.readAllStandardOutput().toStdString()
                  << child.readAllStandardError().toStdString();
        return false;
    }
    return true;
}

bool runRecoveryChildMode(const QStringList& arguments)
{
    if (arguments.size() != 6) return false;
    const bool isNew = arguments[1] == QStringLiteral("--recover-new");
    if (!isNew && arguments[1] != QStringLiteral("--recover-duplicate")) return false;

    CoinManager coins;
    coins.setDataPath(arguments[3].toStdString());
    GachaManager gacha(arguments[2]);
    if (!coins.load() || !gacha.load()) return false;

    const uint32_t expectedBalance = isNew ? 900u : 50u;
    const uint32_t expectedEarned = isNew ? 1000u : 100u;
    const auto recovered = gacha.recover(coins);
    bool ok = expect(recovered.has_value(), "restart exposes the completed operation once");
    if (!recovered) return false;
    ok &= expect(recovered->success, "recovered result is successful only after final variants save");
    ok &= expect(recovered->operationId == arguments[4], "recovery keeps the durable operation id");
    ok &= expect(recovered->variantId == arguments[5], "recovery keeps the chosen stable variant");
    ok &= expect(recovered->isNew == isNew, "recovery keeps the original new/duplicate classification");
    ok &= expect(recovered->coinRefund == (isNew ? 0 : GachaManager::duplicateCoinRefund()),
                 "recovery reports the original refund");
    ok &= expect(gacha.isUnlocked(arguments[5]), "recovered new variant is unlocked");
    ok &= expect(coins.balance() == expectedBalance, "wallet is not charged twice after restart");
    ok &= expect(coins.getTotalCoins() == expectedEarned,
                 "gacha debit and duplicate refund are not earned income");
    ok &= expect(coins.hasGachaOperation(arguments[4]), "wallet retains the operation receipt");

    const auto secondRecovery = gacha.recover(coins);
    ok &= expect(!secondRecovery.has_value(), "the recovered result is returned only once");
    const auto exactReplay = coins.applyGachaPull(arguments[4], arguments[5], isNew,
                                                   GachaManager::pullCost(),
                                                   static_cast<uint32_t>(recovered->coinRefund));
    ok &= expect(exactReplay == CoinManager::OperationResult::AlreadyApplied,
                 "an exact wallet receipt replay is idempotent");
    const QString otherVariant = arguments[5] == QStringLiteral("golden_oak")
                                     ? QStringLiteral("silver_oak")
                                     : QStringLiteral("golden_oak");
    ok &= expect(coins.applyGachaPull(arguments[4], otherVariant, isNew, GachaManager::pullCost(),
                                      static_cast<uint32_t>(recovered->coinRefund)) ==
                     CoinManager::OperationResult::Invalid,
                 "a changed variant cannot reuse a wallet operation id");
    ok &= expect(coins.applyGachaPull(arguments[4], arguments[5], isNew, GachaManager::pullCost() - 1,
                                      static_cast<uint32_t>(recovered->coinRefund)) ==
                     CoinManager::OperationResult::Invalid,
                 "a changed cost cannot reuse a wallet operation id");
    if (!isNew) {
        ok &= expect(coins.applyGachaPull(arguments[4], arguments[5], false, GachaManager::pullCost(), 49) ==
                         CoinManager::OperationResult::Invalid,
                     "a changed duplicate refund cannot reuse a wallet operation id");
        ok &= expect(coins.applyGachaPull(arguments[4], arguments[5], true, GachaManager::pullCost(), 0) ==
                         CoinManager::OperationResult::Invalid,
                     "a changed new/duplicate classification cannot reuse a wallet operation id");
    } else {
        ok &= expect(coins.applyGachaPull(arguments[4], arguments[5], false, GachaManager::pullCost(), 50) ==
                         CoinManager::OperationResult::Invalid,
                     "a changed new/duplicate classification cannot reuse a wallet operation id");
    }
    ok &= expect(coins.balance() == expectedBalance && coins.getTotalCoins() == expectedEarned,
                 "rejected changed receipts do not mutate wallet totals");
    return ok;
}

bool testInsufficientFunds(const QString& root)
{
    const QString userDir = QDir(root).filePath("insufficient");
    if (!QDir().mkpath(userDir)) return false;
    const QString walletPath = QDir(userDir).filePath("coins.dat");
    const QString variantsPath = QDir(userDir).filePath("variants.dat");
    CoinManager coins;
    coins.setDataPath(walletPath.toStdString());
    if (!coins.load() || !coins.earn(99)) return false;
    GachaManager gacha(variantsPath);
    const auto result = gacha.pull(coins);
    bool ok = expect(!result.success && !result.error.isEmpty(),
                     "insufficient funds returns an explicit failure");
    ok &= expect(result.operationId.isEmpty(), "insufficient funds creates no pending operation id");
    ok &= expect(coins.balance() == 99u && coins.getTotalCoins() == 99u,
                 "insufficient funds leaves wallet totals unchanged");
    ok &= expect(gacha.unlockedCount() == 0, "insufficient funds unlocks no variant");
    ok &= expect(!gacha.recover(coins).has_value(), "insufficient funds leaves no operation to recover");
    return ok;
}

bool testPendingWriteFailureAndRetry(const QString& root)
{
    const QString userDir = QDir(root).filePath("pending-write");
    if (!QDir().mkpath(userDir)) return false;
    const QString walletPath = QDir(userDir).filePath("coins.dat");
    const QString variantsPath = QDir(userDir).filePath("variants.dat");
    CoinManager coins;
    coins.setDataPath(walletPath.toStdString());
    if (!coins.load() || !coins.earn(300)) return false;
    GachaManager gacha(variantsPath);

    gacha.failVariantsSaveAfterForTesting(0);
    const auto failed = gacha.pull(coins);
    bool ok = expect(!failed.success && !failed.operationId.isEmpty(),
                     "failed pending write reports its stable in-memory choice");
    ok &= expect(coins.balance() == 300u && !coins.hasGachaOperation(failed.operationId),
                 "wallet is untouched until the pending choice is durable");

    const auto retried = gacha.pull(coins);
    ok &= expect(retried.success && retried.operationId == failed.operationId &&
                     retried.variantId == failed.variantId,
                 "retry persists and uses the original operation and variant");
    ok &= expect(coins.balance() == 200u && coins.getTotalCoins() == 300u,
                 "successful retry charges once without changing earned income");
    return ok;
}

bool testNewOperationRecovery(const QString& root)
{
    const QString userDir = QDir(root).filePath("new-recovery");
    if (!QDir().mkpath(userDir)) return false;
    const QString walletPath = QDir(userDir).filePath("coins.dat");
    const QString variantsPath = QDir(userDir).filePath("variants.dat");
    QString operationId;
    QString variantId;
    {
        CoinManager coins;
        coins.setDataPath(walletPath.toStdString());
        if (!coins.load() || !coins.earn(1000)) return false;
        GachaManager gacha(variantsPath);
        StorageEnvelope::failNextWriteForTesting(walletPath);
        const auto interrupted = gacha.pull(coins);
        bool ok = expect(!interrupted.success && !interrupted.operationId.isEmpty(),
                         "wallet write failure retains a durable pending operation");
        ok &= expect(interrupted.isNew && !interrupted.variantId.isEmpty(),
                     "new-variant choice is stable before the wallet write");
        ok &= expect(coins.balance() == 1000u && !coins.hasGachaOperation(interrupted.operationId),
                     "failed wallet write has no receipt or debit");
        if (!ok) return false;
        operationId = interrupted.operationId;
        variantId = interrupted.variantId;
    }

    if (!runRecoveryChild(QStringLiteral("--recover-new"), variantsPath, walletPath, operationId, variantId))
        return false;
    CoinManager restarted;
    restarted.setDataPath(walletPath.toStdString());
    return expect(restarted.load() && restarted.balance() == 900u && restarted.getTotalCoins() == 1000u,
                  "new operation is durably recovered after a separate process restart");
}

bool testDuplicateOperationRecovery(const QString& root)
{
    const QString userDir = QDir(root).filePath("duplicate-recovery");
    if (!QDir().mkpath(userDir)) return false;
    const QString walletPath = QDir(userDir).filePath("coins.dat");
    const QString variantsPath = QDir(userDir).filePath("variants.dat");
    QString operationId;
    QString variantId;
    {
        CoinManager coins;
        coins.setDataPath(walletPath.toStdString());
        if (!coins.load() || !coins.earn(100)) return false;
        GachaManager gacha(variantsPath);
        const QVector<GachaManager::VariantDef> variants = gacha.allVariants();
        for (const auto& variant : variants) {
            if (!gacha.unlockVariant(variant.id)) return false;
        }
        gacha.failVariantsSaveAfterForTesting(2);
        const auto interrupted = gacha.pull(coins);
        bool ok = expect(!interrupted.success && !interrupted.operationId.isEmpty(),
                         "failed final variants write is never reported as pull success");
        ok &= expect(!interrupted.isNew && interrupted.coinRefund == GachaManager::duplicateCoinRefund(),
                     "duplicate decision and refund survive the final-save failure");
        ok &= expect(coins.balance() == 50u && coins.getTotalCoins() == 100u,
                     "wallet atomically records one net duplicate debit without income");
        ok &= expect(coins.hasGachaOperation(interrupted.operationId),
                     "wallet receipt exists while variants completion remains pending");
        if (!ok) return false;
        operationId = interrupted.operationId;
        variantId = interrupted.variantId;
    }

    if (!runRecoveryChild(QStringLiteral("--recover-duplicate"), variantsPath, walletPath,
                          operationId, variantId))
        return false;
    CoinManager restarted;
    restarted.setDataPath(walletPath.toStdString());
    return expect(restarted.load() && restarted.balance() == 50u && restarted.getTotalCoins() == 100u,
                  "duplicate operation recovery does not charge or refund twice");
}

bool testWalletAppliedSaveFailureRecovery(const QString& root)
{
    const QString userDir = QDir(root).filePath("wallet-applied-save-failure");
    if (!QDir().mkpath(userDir)) return false;
    const QString walletPath = QDir(userDir).filePath("coins.dat");
    const QString variantsPath = QDir(userDir).filePath("variants.dat");
    QString operationId;
    QString variantId;
    {
        CoinManager coins;
        coins.setDataPath(walletPath.toStdString());
        if (!coins.load() || !coins.earn(1000)) return false;
        GachaManager gacha(variantsPath);
        gacha.failVariantsSaveAfterForTesting(1);
        const auto interrupted = gacha.pull(coins);
        bool ok = expect(!interrupted.success, "WalletApplied variants write failure is not success");
        ok &= expect(coins.balance() == 900u && coins.hasGachaOperation(interrupted.operationId),
                     "wallet receipt is durable before WalletApplied state failure");
        if (!ok) return false;
        operationId = interrupted.operationId;
        variantId = interrupted.variantId;
    }
    return runRecoveryChild(QStringLiteral("--recover-new"), variantsPath, walletPath,
                            operationId, variantId);
}

bool testFeedbackAcknowledgementFailureRecovery(const QString& root)
{
    const QString userDir = QDir(root).filePath("feedback-ack-failure");
    if (!QDir().mkpath(userDir)) return false;
    const QString walletPath = QDir(userDir).filePath("coins.dat");
    const QString variantsPath = QDir(userDir).filePath("variants.dat");
    QString operationId;
    QString variantId;
    {
        CoinManager coins;
        coins.setDataPath(walletPath.toStdString());
        if (!coins.load() || !coins.earn(1000)) return false;
        GachaManager gacha(variantsPath);
        gacha.failVariantsSaveAfterForTesting(3);
        const auto interrupted = gacha.pull(coins);
        bool ok = expect(!interrupted.success && !interrupted.error.isEmpty(),
                         "feedback acknowledgement write failure is not reported as pull success");
        ok &= expect(gacha.isUnlocked(interrupted.variantId),
                     "final unlock and completed result remain durable after acknowledgement failure");
        ok &= expect(coins.balance() == 900u && coins.hasGachaOperation(interrupted.operationId),
                     "acknowledgement failure does not repeat the wallet operation");
        if (!ok) return false;
        operationId = interrupted.operationId;
        variantId = interrupted.variantId;
    }
    return runRecoveryChild(QStringLiteral("--recover-new"), variantsPath, walletPath,
                            operationId, variantId);
}

bool testFeedbackAcknowledgementImmediateRetry(const QString& root)
{
    const QString userDir = QDir(root).filePath("feedback-ack-immediate-retry");
    if (!QDir().mkpath(userDir)) return false;
    CoinManager coins;
    coins.setDataPath(QDir(userDir).filePath("coins.dat").toStdString());
    if (!coins.load() || !coins.earn(100)) return false;
    GachaManager gacha(QDir(userDir).filePath("variants.dat"));
    gacha.failVariantsSaveAfterForTesting(3);
    const auto first = gacha.pull(coins);
    bool ok = expect(!first.success && coins.balance() == 0u,
                     "feedback acknowledgement can fail after the pull is paid and unlocked");
    const auto retry = gacha.pull(coins);
    ok &= expect(retry.success && retry.operationId == first.operationId &&
                     retry.variantId == first.variantId,
                 "UI retry delivers the completed result without restarting or rerolling");
    ok &= expect(coins.balance() == 0u && coins.getTotalCoins() == 100u,
                 "feedback retry does not require or charge another pull cost");
    ok &= expect(!gacha.recover(coins).has_value() && gacha.lastError().isEmpty(),
                 "delivered feedback is not repeated on recovery");
    return ok;
}

bool testLowBalancePendingPullRetry(const QString& root)
{
    const QString userDir = QDir(root).filePath("low-balance-retry");
    if (!QDir().mkpath(userDir)) return false;
    const QString walletPath = QDir(userDir).filePath("coins.dat");
    const QString variantsPath = QDir(userDir).filePath("variants.dat");
    CoinManager coins;
    coins.setDataPath(walletPath.toStdString());
    if (!coins.load() || !coins.earn(100)) return false;
    GachaManager gacha(variantsPath);
    const QVector<GachaManager::VariantDef> variants = gacha.allVariants();
    for (const auto& variant : variants) {
        if (!gacha.unlockVariant(variant.id)) return false;
    }
    gacha.failVariantsSaveAfterForTesting(2);
    const auto first = gacha.pull(coins);
    bool ok = expect(!first.success && coins.balance() == 50u,
                     "duplicate debit can leave less than pull cost before variants finalize");
    const auto retry = gacha.pull(coins);
    ok &= expect(retry.success && retry.operationId == first.operationId &&
                     retry.variantId == first.variantId,
                 "pending paid pull resumes without a balance precheck or reroll");
    ok &= expect(coins.balance() == 50u && coins.getTotalCoins() == 100u,
                 "low-balance retry does not debit or refund twice");
    return ok;
}

bool testCompletedReceiptRollbackIsDetected(const QString& root)
{
    const QString userDir = QDir(root).filePath("completed-wallet-rollback");
    if (!QDir().mkpath(userDir)) return false;
    const QString walletPath = QDir(userDir).filePath("coins.dat");
    const QString variantsPath = QDir(userDir).filePath("variants.dat");
    QByteArray oldWalletBytes;
    QString operationId;
    {
        CoinManager coins;
        coins.setDataPath(walletPath.toStdString());
        if (!coins.load() || !coins.earn(1000)) return false;
        QFile original(walletPath);
        if (!original.open(QIODevice::ReadOnly)) return false;
        oldWalletBytes = original.readAll();
        original.close();
        GachaManager gacha(variantsPath);
        const auto completed = gacha.pull(coins);
        if (!expect(completed.success, "completed pull fixture succeeds")) return false;
        operationId = completed.operationId;
    }

    if (!writeBytes(walletPath, oldWalletBytes)) return false;
    CoinManager olderWallet;
    olderWallet.setDataPath(walletPath.toStdString());
    if (!olderWallet.load()) return false;
    GachaManager gacha(variantsPath);
    const auto recovery = gacha.recover(olderWallet);
    bool ok = expect(!recovery.has_value(), "completed operation refuses a wallet missing its receipt");
    ok &= expect(gacha.lastError().contains(QStringLiteral("快照")),
                 "completed receipt rollback requests a consistent snapshot");
    ok &= expect(olderWallet.balance() == 1000u && !olderWallet.hasGachaOperation(operationId),
                 "receipt rollback detection does not charge an older wallet again");
    return ok;
}

bool testLegacyFormats(const QString& root)
{
    const QString userDir = QDir(root).filePath("legacy");
    if (!QDir().mkpath(userDir)) return false;
    QByteArray maskPayload;
    QDataStream maskStream(&maskPayload, QIODevice::WriteOnly);
    maskStream.setVersion(QDataStream::Qt_6_5);
    maskStream << quint32(1u);

    const QString rawPath = QDir(userDir).filePath("raw-variants.dat");
    bool ok = expect(writeBytes(rawPath, maskPayload), "raw legacy mask fixture is written");
    {
        GachaManager raw(rawPath);
        ok &= expect(raw.isUnlocked(QStringLiteral("golden_oak")) && raw.unlockedCount() == 1,
                     "raw legacy unlock mask is retained");
    }
    const auto rawMigrated = StorageEnvelope::inspect(rawPath, kVariantMagic);
    ok &= expect(rawMigrated.state == StorageEnvelope::ReadState::Valid && rawMigrated.version == 2,
                 "raw legacy mask migrates to envelope version two");

    const QString envelopePath = QDir(userDir).filePath("v1-variants.dat");
    ok &= expect(StorageEnvelope::write(envelopePath, kVariantMagic, 1, maskPayload),
                 "version-one envelope fixture is written");
    {
        GachaManager versionOne(envelopePath);
        ok &= expect(versionOne.isUnlocked(QStringLiteral("golden_oak")) && versionOne.unlockedCount() == 1,
                     "version-one envelope unlock mask is retained");
    }
    const auto v1Migrated = StorageEnvelope::inspect(envelopePath, kVariantMagic);
    ok &= expect(v1Migrated.state == StorageEnvelope::ReadState::Valid && v1Migrated.version == 2,
                 "version-one envelope migrates to version two");
    return ok;
}

bool testAmbiguousBackupIsNotRestored(const QString& root)
{
    const QString userDir = QDir(root).filePath("ambiguous-backup");
    if (!QDir().mkpath(userDir)) return false;
    bool ok = true;
    const QString corruptPath = QDir(userDir).filePath("corrupt-primary.dat");
    {
        GachaManager initial(corruptPath);
        if (!initial.lastError().isEmpty()) return false;
    }
    if (!QFile::copy(corruptPath, corruptPath + QStringLiteral(".bak")) ||
        !writeBytes(corruptPath, "damaged-primary")) return false;

    GachaManager corruptBlocked(corruptPath);
    ok &= expect(!corruptBlocked.load(), "corrupt primary with valid backup requires snapshot restore");
    ok &= expect(corruptBlocked.lastError().contains(QStringLiteral("快照")),
                 "ambiguous variants rollback reports consistent snapshot recovery");
    QFile primary(corruptPath);
    ok &= expect(primary.open(QIODevice::ReadOnly) && primary.readAll() == QByteArray("damaged-primary"),
                 "ambiguous backup is not copied over the primary");

    const QString legacyPath = QDir(userDir).filePath("legacy-primary.dat");
    {
        GachaManager initial(legacyPath);
        if (!initial.lastError().isEmpty()) return false;
    }
    if (!QFile::copy(legacyPath, legacyPath + QStringLiteral(".bak"))) return false;
    QByteArray rawMask;
    QDataStream maskStream(&rawMask, QIODevice::WriteOnly);
    maskStream.setVersion(QDataStream::Qt_6_5);
    maskStream << quint32(1u);
    if (!writeBytes(legacyPath, rawMask)) return false;
    GachaManager legacyBlocked(legacyPath);
    ok &= expect(!legacyBlocked.load() && legacyBlocked.lastError().contains(QStringLiteral("快照")),
                 "legacy primary with valid backup is not treated as raw migration input");
    QFile legacyPrimary(legacyPath);
    ok &= expect(legacyPrimary.open(QIODevice::ReadOnly) && legacyPrimary.readAll() == rawMask,
                 "legacy primary is preserved when fallback is ambiguous");

    const QString missingPath = QDir(userDir).filePath("missing-primary.dat");
    {
        GachaManager initial(missingPath);
        if (!initial.lastError().isEmpty()) return false;
    }
    if (!QFile::copy(missingPath, missingPath + QStringLiteral(".bak")) || !QFile::remove(missingPath))
        return false;
    GachaManager missingBlocked(missingPath);
    ok &= expect(!missingBlocked.load() && !QFileInfo::exists(missingPath),
                 "missing primary with a backup does not initialize empty state");
    ok &= expect(missingBlocked.lastError().contains(QStringLiteral("快照")),
                 "missing primary with backup requests consistent snapshot restore");
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() > 1 && arguments[1].startsWith(QStringLiteral("--recover-")))
        return runRecoveryChildMode(arguments) ? 0 : 1;

    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    bool ok = true;
    ok &= testInsufficientFunds(directory.path());
    ok &= testPendingWriteFailureAndRetry(directory.path());
    ok &= testNewOperationRecovery(directory.path());
    ok &= testWalletAppliedSaveFailureRecovery(directory.path());
    ok &= testDuplicateOperationRecovery(directory.path());
    ok &= testFeedbackAcknowledgementFailureRecovery(directory.path());
    ok &= testFeedbackAcknowledgementImmediateRetry(directory.path());
    ok &= testLowBalancePendingPullRetry(directory.path());
    ok &= testCompletedReceiptRollbackIsDetected(directory.path());
    ok &= testLegacyFormats(directory.path());
    ok &= testAmbiguousBackupIsNotRestored(directory.path());
    StorageEnvelope::clearWriteFailuresForTesting();
    return ok ? 0 : 1;
}
