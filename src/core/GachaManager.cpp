#include "core/GachaManager.h"
#include "core/CoinManager.h"
#include "plant/VariantPlants.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QUuid>
#include <limits>

namespace {

constexpr quint32 kVariantMagic = 0x46564131u; // FVA1
constexpr quint16 kVariantVersion = 2;

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.c_str(), static_cast<qsizetype>(value.size()));
}

GachaManager::VariantDef makeVariantDef(const PlantVariantInfo& plant)
{
    GachaManager::VariantDef def;
    def.id = fromUtf8(plant.variantId());
    def.basePlant = fromUtf8(plant.basePlantName());
    def.basePlantType = plant.basePlantType();
    def.displayName = fromUtf8(plant.displayName());
    def.description = fromUtf8(plant.description());
    def.icon = fromUtf8(plant.icon());
    def.imagePath = fromUtf8(plant.imagePath());
    def.rarity = fromUtf8(plant.rarity());
    def.tintColor = fromUtf8(plant.tintColor());
    return def;
}

bool validOperationId(const QString& operationId)
{
    return !operationId.isEmpty() && !QUuid(operationId).isNull();
}

bool readLegacyUnlocks(const QByteArray& bytes, QVector<GachaManager::VariantDef>& variants)
{
    if (bytes.isEmpty()) return true;
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    stream.setVersion(QDataStream::Qt_6_5);
    if (bytes.size() == static_cast<qsizetype>(sizeof(quint32))) {
        quint32 mask = 0;
        stream >> mask;
        if (stream.status() != QDataStream::Ok) return false;
        for (int i = 0; i < variants.size() && i < 32; ++i) {
            variants[i].unlocked = (mask & (1u << i)) != 0;
        }
        return true;
    }

    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > 1000) return false;
    for (quint32 i = 0; i < count; ++i) {
        QByteArray idBytes;
        quint8 flag = 0;
        stream >> idBytes >> flag;
        if (stream.status() != QDataStream::Ok) return false;
        for (GachaManager::VariantDef& variant : variants) {
            if (variant.id == QString::fromUtf8(idBytes)) {
                variant.unlocked = flag != 0;
                break;
            }
        }
    }
    return buffer.atEnd();
}

QString snapshotRestoreError()
{
    return QStringLiteral("异色状态可能与钱包操作收据不一致；请恢复一致快照后再继续");
}

} // namespace

GachaManager::GachaManager(const QString& filePath)
    : m_filePath(filePath)
{
    m_variants = {
        makeVariantDef(GoldenOak()), makeVariantDef(SilverOak()),
        makeVariantDef(RedPine()), makeVariantDef(BluePine()),
        makeVariantDef(PinkRose()), makeVariantDef(PurpleRose()),
        makeVariantDef(GoldenGinkgo()), makeVariantDef(RedGinkgo()),
        makeVariantDef(GoldenSunflower()), makeVariantDef(BlackSunflower()),
        makeVariantDef(GemCactus()), makeVariantDef(FlameCactus()),
    };
    load();
}

GachaManager::~GachaManager() = default;

bool GachaManager::load()
{
    m_loaded = false;
    applyUnlockMask(0);
    m_originalUnlockMask = 0;
    m_pending = {};
    m_lastCompletedResult = {};
    m_hasLastCompletedResult = false;
    m_recoveryResultAvailable = false;
    m_lastError.clear();

    const StorageEnvelope::ReadResult primary = StorageEnvelope::inspect(m_filePath, kVariantMagic);
    const QString backupPath = m_filePath + QStringLiteral(".bak");
    const bool hasBackup = QFileInfo::exists(backupPath);
    const StorageEnvelope::ReadResult backup = StorageEnvelope::inspect(backupPath, kVariantMagic);

    if (primary.state == StorageEnvelope::ReadState::Missing) {
        if (hasBackup) {
            m_lastError = snapshotRestoreError();
            return false;
        }
        if (!save()) return false;
        m_loaded = true;
        return true;
    }
    if (primary.state == StorageEnvelope::ReadState::Corrupt) {
        m_lastError = backup.hasPayload() ? snapshotRestoreError() : primary.error;
        if (m_lastError.isEmpty()) m_lastError = QStringLiteral("异色状态文件损坏");
        return false;
    }
    if (primary.state == StorageEnvelope::ReadState::Legacy) {
        if (backup.hasPayload()) {
            m_lastError = snapshotRestoreError();
            return false;
        }
        QFile file(m_filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            m_lastError = QStringLiteral("无法读取旧版异色状态文件");
            return false;
        }
        const QByteArray bytes = file.readAll();
        file.close();
        if (!readLegacyUnlocks(bytes, m_variants)) {
            m_lastError = QStringLiteral("旧版异色状态文件格式无效");
            return false;
        }
        m_originalUnlockMask = unlockMask();
        if (!save()) return false;
        m_loaded = true;
        return true;
    }
    if (primary.state != StorageEnvelope::ReadState::Valid) {
        m_lastError = QStringLiteral("异色状态文件无法安全读取");
        return false;
    }

    if (primary.version == kVariantVersion) {
        if (!loadVersion2(primary.payload)) return false;
        m_loaded = true;
        return true;
    }
    if (primary.version != 1) {
        m_lastError = QStringLiteral("异色状态文件版本不受支持");
        return false;
    }
    if (!readLegacyUnlocks(primary.payload, m_variants)) {
        m_lastError = QStringLiteral("旧版异色状态文件格式无效");
        return false;
    }
    m_originalUnlockMask = unlockMask();
    if (!save()) return false;
    m_loaded = true;
    return true;
}

bool GachaManager::loadVersion2(const QByteArray& payload)
{
    QBuffer buffer;
    buffer.setData(payload);
    if (!buffer.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("异色状态内容无法读取");
        return false;
    }

    quint32 originalMask = 0;
    bool pendingActive = false;
    QString pendingOperationId;
    QString pendingVariantId;
    bool pendingIsNew = false;
    quint8 pendingState = 0;
    bool hasLastResult = false;
    QString lastOperationId;
    QString lastVariantId;
    bool lastIsNew = false;
    quint32 lastRefund = 0;
    bool recoveryResultAvailable = false;

    QDataStream stream(&buffer);
    stream.setVersion(QDataStream::Qt_6_5);
    stream >> originalMask >> pendingActive >> pendingOperationId >> pendingVariantId >> pendingIsNew >>
        pendingState >> hasLastResult >> lastOperationId >> lastVariantId >> lastIsNew >> lastRefund >>
        recoveryResultAvailable;
    if (stream.status() != QDataStream::Ok || !buffer.atEnd()) {
        m_lastError = QStringLiteral("异色状态内容不完整");
        return false;
    }

    const quint32 knownMask = m_variants.size() >= 32
                                  ? std::numeric_limits<quint32>::max()
                                  : ((1u << m_variants.size()) - 1u);
    if ((originalMask & ~knownMask) != 0 ||
        pendingState > static_cast<quint8>(ProcessingState::WalletApplied)) {
        m_lastError = QStringLiteral("异色状态包含无效标记");
        return false;
    }
    if (pendingActive) {
        const int pendingIndex = indexOf(pendingVariantId);
        if (!validOperationId(pendingOperationId) || pendingIndex < 0 ||
            pendingIsNew != ((originalMask & (1u << pendingIndex)) == 0)) {
            m_lastError = QStringLiteral("异色待处理操作无效");
            return false;
        }
    } else if (!pendingOperationId.isEmpty() || !pendingVariantId.isEmpty() || pendingIsNew ||
               pendingState != 0) {
        m_lastError = QStringLiteral("异色状态包含孤立的待处理字段");
        return false;
    }
    if (hasLastResult) {
        const int lastIndex = indexOf(lastVariantId);
        const quint32 expectedRefund = lastIsNew ? 0u : static_cast<quint32>(duplicateCoinRefund());
        if (!validOperationId(lastOperationId) || lastIndex < 0 || lastRefund != expectedRefund ||
            (recoveryResultAvailable && pendingActive)) {
            m_lastError = QStringLiteral("异色上次结果无效");
            return false;
        }
    } else if (!lastOperationId.isEmpty() || !lastVariantId.isEmpty() || lastIsNew || lastRefund != 0 ||
               recoveryResultAvailable) {
        m_lastError = QStringLiteral("异色状态包含孤立的结果字段");
        return false;
    }

    m_originalUnlockMask = originalMask;
    applyUnlockMask(originalMask);
    m_pending.active = pendingActive;
    m_pending.operationId = pendingOperationId;
    m_pending.variantId = pendingVariantId;
    m_pending.isNew = pendingIsNew;
    m_pending.state = static_cast<ProcessingState>(pendingState);
    m_hasLastCompletedResult = hasLastResult;
    if (hasLastResult) m_lastCompletedResult = resultFor(lastOperationId, lastVariantId, lastIsNew);
    m_recoveryResultAvailable = recoveryResultAvailable;
    return true;
}

bool GachaManager::save()
{
    QByteArray payload;
    QBuffer buffer(&payload);
    if (!buffer.open(QIODevice::WriteOnly)) {
        m_lastError = QStringLiteral("无法准备异色状态保存");
        return false;
    }

    QDataStream stream(&buffer);
    stream.setVersion(QDataStream::Qt_6_5);
    stream << static_cast<quint32>(m_originalUnlockMask) << m_pending.active << m_pending.operationId <<
        m_pending.variantId << m_pending.isNew << static_cast<quint8>(m_pending.state) <<
        m_hasLastCompletedResult << m_lastCompletedResult.operationId << m_lastCompletedResult.variantId <<
        m_lastCompletedResult.isNew << static_cast<quint32>(m_lastCompletedResult.coinRefund) <<
        m_recoveryResultAvailable;
    buffer.close();
    if (stream.status() != QDataStream::Ok) {
        m_lastError = QStringLiteral("异色状态序列化失败");
        return false;
    }

    if (m_variantsSavesUntilFailure == 0) {
        m_variantsSavesUntilFailure = -1;
        StorageEnvelope::failNextWriteForTesting(m_filePath);
    } else if (m_variantsSavesUntilFailure > 0) {
        --m_variantsSavesUntilFailure;
    }

    QString error;
    if (!StorageEnvelope::write(m_filePath, kVariantMagic, kVariantVersion, payload, &error)) {
        m_lastError = error.isEmpty() ? QStringLiteral("异色状态保存失败") : error;
        return false;
    }
    m_lastError.clear();
    return true;
}

QStringList GachaManager::unlockedVariantIds() const
{
    QStringList list;
    for (const VariantDef& variant : m_variants)
        if (variant.unlocked) list << variant.id;
    return list;
}

int GachaManager::unlockedCount() const
{
    int count = 0;
    for (const VariantDef& variant : m_variants)
        if (variant.unlocked) ++count;
    return count;
}

GachaManager::PullResult GachaManager::pull(CoinManager& coins)
{
    if (!m_loaded && !load()) return failedResult(m_lastError);
    if (!m_pending.active && m_recoveryResultAvailable) {
        const auto recovered = recover(coins);
        if (recovered) return *recovered;
        return failedResult(m_lastError);
    }
    if (!m_pending.active) {
        if (coins.balance() < static_cast<uint32_t>(pullCost())) {
            m_lastError = QStringLiteral("金币不足，无法抽取异色树种");
            return failedResult(m_lastError);
        }
        if (m_variants.isEmpty()) {
            m_lastError = QStringLiteral("没有可抽取的异色树种");
            return failedResult(m_lastError);
        }
        const int index = QRandomGenerator::global()->bounded(m_variants.size());
        m_pending.active = true;
        m_pending.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_pending.variantId = m_variants[index].id;
        m_pending.isNew = !m_variants[index].unlocked;
        m_pending.state = ProcessingState::AwaitingWallet;
        m_originalUnlockMask = unlockMask();
    }

    PullResult completed = processPending(coins);
    if (!completed.success) return completed;
    const auto delivered = consumeRecoveryResult();
    if (delivered) return *delivered;
    completed.success = false;
    completed.error = m_lastError;
    return completed;
}

std::optional<GachaManager::PullResult> GachaManager::recover(CoinManager& coins)
{
    m_lastError.clear();
    if (!m_loaded && !load()) return std::nullopt;
    if (m_hasLastCompletedResult) {
        if (!coins.hasGachaOperation(m_lastCompletedResult.operationId)) {
            m_lastError = snapshotRestoreError();
            return std::nullopt;
        }
        const CoinManager::OperationResult receiptResult = coins.applyGachaPull(
            m_lastCompletedResult.operationId, m_lastCompletedResult.variantId,
            m_lastCompletedResult.isNew, static_cast<uint32_t>(pullCost()),
            static_cast<uint32_t>(m_lastCompletedResult.coinRefund));
        if (receiptResult != CoinManager::OperationResult::AlreadyApplied) {
            m_lastError = snapshotRestoreError();
            return std::nullopt;
        }
    }
    if (m_pending.active) {
        const PullResult completed = processPending(coins);
        if (!completed.success) return std::nullopt;
    }
    return consumeRecoveryResult();
}

GachaManager::PullResult GachaManager::processPending(CoinManager& coins)
{
    const PendingOperation operation = m_pending;
    const int index = indexOf(operation.variantId);
    if (!operation.active || index < 0 || !validOperationId(operation.operationId)) {
        m_lastError = QStringLiteral("异色待处理操作无效");
        return failedResult(m_lastError, operation.active ? &operation : nullptr);
    }

    const uint32_t cost = static_cast<uint32_t>(pullCost());
    const uint32_t refundAmount = operation.isNew ? 0u : static_cast<uint32_t>(duplicateCoinRefund());
    auto discardInsufficientOperation = [&]() {
        PullResult result = failedResult(QStringLiteral("金币不足，无法完成异色抽取"), &operation);
        m_pending = {};
        if (!save()) {
            m_pending = operation;
            result.error = QStringLiteral("金币不足，且无法清除待处理操作：%1").arg(m_lastError);
            m_lastError = result.error;
            return result;
        }
        m_lastError = result.error;
        return result;
    };

    if (operation.state == ProcessingState::AwaitingWallet) {
        if (!save()) return failedResult(m_lastError, &operation);
        if (!coins.hasGachaOperation(operation.operationId) && coins.balance() < cost)
            return discardInsufficientOperation();

        const CoinManager::OperationResult walletResult = coins.applyGachaPull(
            operation.operationId, operation.variantId, operation.isNew, cost, refundAmount);
        if (walletResult == CoinManager::OperationResult::InsufficientFunds)
            return discardInsufficientOperation();
        if (walletResult == CoinManager::OperationResult::SaveFailed) {
            m_lastError = coins.lastError().isEmpty() ? QStringLiteral("钱包操作保存失败") : coins.lastError();
            return failedResult(m_lastError, &operation);
        }
        if (walletResult != CoinManager::OperationResult::Applied &&
            walletResult != CoinManager::OperationResult::AlreadyApplied) {
            m_lastError = QStringLiteral("钱包异色操作收据与待处理选择不一致");
            return failedResult(m_lastError, &operation);
        }
    } else {
        if (!coins.hasGachaOperation(operation.operationId)) {
            m_lastError = snapshotRestoreError();
            return failedResult(m_lastError, &operation);
        }
        const CoinManager::OperationResult walletResult = coins.applyGachaPull(
            operation.operationId, operation.variantId, operation.isNew, cost, refundAmount);
        if (walletResult != CoinManager::OperationResult::AlreadyApplied) {
            m_lastError = QStringLiteral("钱包操作收据与异色待处理状态不一致");
            return failedResult(m_lastError, &operation);
        }
    }

    m_pending.state = ProcessingState::WalletApplied;
    if (!save()) {
        const QString saveError = m_lastError;
        m_lastError = QStringLiteral("钱包已记录操作，但异色待处理状态保存失败：%1").arg(saveError);
        return failedResult(m_lastError, &operation);
    }

    const uint32_t previousMask = m_originalUnlockMask;
    const PullResult previousResult = m_lastCompletedResult;
    const bool hadPreviousResult = m_hasLastCompletedResult;
    const bool previousRecoveryResultAvailable = m_recoveryResultAvailable;
    if (operation.isNew) m_variants[index].unlocked = true;
    m_originalUnlockMask = unlockMask();
    m_lastCompletedResult = resultFor(operation.operationId, operation.variantId, operation.isNew);
    m_hasLastCompletedResult = true;
    m_recoveryResultAvailable = true;
    m_pending = {};
    if (!save()) {
        const QString saveError = m_lastError;
        applyUnlockMask(previousMask);
        m_originalUnlockMask = previousMask;
        m_pending = operation;
        m_pending.state = ProcessingState::WalletApplied;
        m_lastCompletedResult = previousResult;
        m_hasLastCompletedResult = hadPreviousResult;
        m_recoveryResultAvailable = previousRecoveryResultAvailable;
        m_lastError = QStringLiteral("钱包已记录操作，但异色结果保存失败：%1").arg(saveError);
        return failedResult(m_lastError, &m_pending);
    }

    m_lastError.clear();
    return m_lastCompletedResult;
}

GachaManager::PullResult GachaManager::resultFor(const QString& operationId, const QString& variantId,
                                                 bool isNew) const
{
    PullResult result;
    result.operationId = operationId;
    result.variantId = variantId;
    result.success = true;
    result.isNew = isNew;
    result.coinRefund = isNew ? 0 : duplicateCoinRefund();
    const int index = indexOf(variantId);
    if (index >= 0) {
        const VariantDef& variant = m_variants[index];
        result.displayName = variant.displayName;
        result.description = variant.description;
        result.icon = variant.icon;
        result.rarity = variant.rarity;
    }
    return result;
}

GachaManager::PullResult GachaManager::failedResult(const QString& error,
                                                    const PendingOperation* pending) const
{
    PullResult result;
    if (pending && pending->active) {
        result = resultFor(pending->operationId, pending->variantId, pending->isNew);
    }
    result.success = false;
    result.error = error;
    return result;
}

std::optional<GachaManager::PullResult> GachaManager::consumeRecoveryResult()
{
    if (!m_recoveryResultAvailable) return std::nullopt;
    const PullResult result = m_lastCompletedResult;
    m_recoveryResultAvailable = false;
    if (!save()) {
        const QString saveError = m_lastError;
        m_recoveryResultAvailable = true;
        m_lastError = QStringLiteral("异色结果已完成，但无法记录结果反馈状态：%1").arg(saveError);
        return std::nullopt;
    }
    m_lastError.clear();
    return result;
}

bool GachaManager::isUnlocked(const QString& variantId) const
{
    const int idx = indexOf(variantId);
    return idx >= 0 && m_variants[idx].unlocked;
}

bool GachaManager::unlockVariant(const QString& variantId)
{
    const int idx = indexOf(variantId);
    if (idx < 0 || m_variants[idx].unlocked || m_pending.active) return false;
    m_variants[idx].unlocked = true;
    m_originalUnlockMask = unlockMask();
    if (save()) return true;
    m_variants[idx].unlocked = false;
    m_originalUnlockMask = unlockMask();
    return false;
}

void GachaManager::failVariantsSaveAfterForTesting(int successfulSaves)
{
    m_variantsSavesUntilFailure = successfulSaves < 0 ? -1 : successfulSaves;
}

int GachaManager::indexOf(const QString& variantId) const
{
    for (int i = 0; i < m_variants.size(); ++i)
        if (m_variants[i].id == variantId) return i;
    return -1;
}

uint32_t GachaManager::unlockMask() const
{
    uint32_t mask = 0;
    for (int i = 0; i < m_variants.size() && i < 32; ++i)
        if (m_variants[i].unlocked) mask |= (1u << i);
    return mask;
}

void GachaManager::applyUnlockMask(uint32_t mask)
{
    for (int i = 0; i < m_variants.size() && i < 32; ++i)
        m_variants[i].unlocked = (mask & (1u << i)) != 0;
}
