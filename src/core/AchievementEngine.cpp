#include "core/AchievementEngine.h"

void AchievementEngine::checkAndUnlock()
{
    uint32_t mask = coinMgr_.getUnlockedAchievementMask();
    auto records = db_.getAllRecords();

    uint32_t successCount = 0;
    uint32_t failCount = 0;
    uint64_t totalSeconds = 0;
    bool plantedOak = false, plantedPine = false, plantedRose = false;

    for (const auto& rec : records) {
        if (rec.status == 0) {
            ++successCount;
            totalSeconds += rec.actualSeconds;
            if (rec.plantType == 0) plantedOak = true;
            if (rec.plantType == 1) plantedPine = true;
            if (rec.plantType == 2) plantedRose = true;
        } else if (rec.status == 1 || rec.status == 2) {
            ++failCount;
        }
    }

    if (!(mask & (1u << 0)) && successCount >= 1)
        unlock(0, QStringLiteral("小试牛刀"),
               QStringLiteral("成功完成第一次专注，迈出绿化地球的第一步！"));

    if (!(mask & (1u << 1)) && totalSeconds >= 30000)
        unlock(1, QStringLiteral("深林守护者"),
               QStringLiteral("累计专注时长达 500 分钟，你的森林已粗具规模！"));

    if (!(mask & (1u << 2)) && coinMgr_.getTotalCoins() >= 1000)
        unlock(2, QStringLiteral("金币大亨"),
               QStringLiteral("钱包金币总数累计突破 1000 枚！"));

    if (!(mask & (1u << 3)) && failCount >= 5)
        unlock(3, QStringLiteral("百折不挠"),
               QStringLiteral("累计小树枯萎 5 次，但你依然选择继续专注！"));

    if (!(mask & (1u << 4)) && (plantedOak && plantedPine && plantedRose))
        unlock(4, QStringLiteral("物种多样性"),
               QStringLiteral("成功种植过橡树、松树和玫瑰三种植物！"));
}

void AchievementEngine::unlock(uint32_t bit, const QString& title, const QString& desc)
{
    uint32_t mask = coinMgr_.getUnlockedAchievementMask();
    mask |= (1u << bit);
    coinMgr_.setUnlockedAchievementMask(mask);
    emit sig_achievementUnlocked(bit, title, desc);
}
