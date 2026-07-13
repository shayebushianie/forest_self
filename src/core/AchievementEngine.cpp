#include "core/AchievementEngine.h"

#include <algorithm>
#include <QSet>
#include <QtGlobal>

AchievementEngine::AchievementEngine(DatabaseManager& db, CoinManager& coinMgr, QObject* parent)
    : QObject(parent), db_(db), coinMgr_(coinMgr) {
    setupDefs();
    validateDefinitions();
    loadStateFromMask();
}

void AchievementEngine::setupDefs() {
    m_infos = {
        // Keep storageBit stable forever; saved wallets persist achievements by bit mask.
        { QStringLiteral("first_focus_old"), QStringLiteral("小试牛刀"),
          QStringLiteral("成功完成第一次专注，迈出绿化地球的第一步！"), QStringLiteral("🎯"), QStringLiteral("common"), 20, 0, false },
        { QStringLiteral("deep_guardian"), QStringLiteral("深林守护者"),
          QStringLiteral("累计专注时长达 500 分钟，你的森林已粗具规模！"), QStringLiteral("🌲"), QStringLiteral("silver"), 120, 1, false },
        { QStringLiteral("coin_tycoon"), QStringLiteral("金币大亨"),
          QStringLiteral("钱包金币总数累计突破 1000 枚！"), QStringLiteral("💰"), QStringLiteral("gold"), 160, 2, false },
        { QStringLiteral("resilient"), QStringLiteral("百折不挠"),
          QStringLiteral("累计小树枯萎 5 次，但你依然选择继续专注！"), QStringLiteral("🌱"), QStringLiteral("silver"), 80, 3, false },
        { QStringLiteral("biodiversity"), QStringLiteral("物种多样性"),
          QStringLiteral("成功种植过橡树、松树和玫瑰三种植物！"), QStringLiteral("🦋"), QStringLiteral("silver"), 100, 4, false },
        { QStringLiteral("botanist"), QStringLiteral("植物学家"),
          QStringLiteral("成功种植过全部六种植物！"), QStringLiteral("🌿"), QStringLiteral("gold"), 220, 5, false },

        // New achievement system merged into one engine.
        { QStringLiteral("first_oak"),       QStringLiteral("橡树培育家"),
          QStringLiteral("成功种出第一棵橡树"), QStringLiteral("🌳"), QStringLiteral("silver"), 50, 6, false },
        { QStringLiteral("first_rose"),      QStringLiteral("玫瑰园丁"),
          QStringLiteral("成功种出第一棵玫瑰"), QStringLiteral("🌹"), QStringLiteral("silver"), 50, 7, false },
        { QStringLiteral("first_sunflower"), QStringLiteral("向日葵农夫"),
          QStringLiteral("成功种出第一棵向日葵"), QStringLiteral("🌻"), QStringLiteral("silver"), 50, 8, false },
        { QStringLiteral("first_pine"),      QStringLiteral("松树造林者"),
          QStringLiteral("成功种出第一棵松树"), QStringLiteral("🌲"), QStringLiteral("silver"), 50, 9, false },

        { QStringLiteral("full_collection"), QStringLiteral("全植收藏家"),
          QStringLiteral("解锁所有植物种类"), QStringLiteral("📚"), QStringLiteral("gold"), 260, 10, false },

        { QStringLiteral("first_focus"),     QStringLiteral("初出茅庐"),
          QStringLiteral("完成第一次专注"), QStringLiteral("🎯"), QStringLiteral("common"), 20, 11, false },
        { QStringLiteral("dedicated"),       QStringLiteral("专注达人"),
          QStringLiteral("累计完成 10 次专注"), QStringLiteral("⭐"), QStringLiteral("common"), 80, 12, false },
        { QStringLiteral("master"),          QStringLiteral("专注大师"),
          QStringLiteral("累计完成 50 次专注"), QStringLiteral("👑"), QStringLiteral("gold"), 300, 13, false },
        { QStringLiteral("marathon"),        QStringLiteral("马拉松专注"),
          QStringLiteral("一次专注达到 120 分钟"), QStringLiteral("⏰"), QStringLiteral("silver"), 120, 14, false },
        { QStringLiteral("no_distractions"), QStringLiteral("心无旁骛"),
          QStringLiteral("深度专注下无违规完成专注"), QStringLiteral("🧘"), QStringLiteral("silver"), 100, 15, false },

        { QStringLiteral("saver"),           QStringLiteral("小有积蓄"),
          QStringLiteral("累计赚取 100 金币"), QStringLiteral("🪙"), QStringLiteral("common"), 60, 16, false },
        { QStringLiteral("wealthy"),         QStringLiteral("富甲一方"),
          QStringLiteral("累计赚取 1000 金币"), QStringLiteral("💰"), QStringLiteral("gold"), 200, 17, false },

        { QStringLiteral("decorator"),       QStringLiteral("森林装饰者"),
          QStringLiteral("在森林中摆放 5 棵树"), QStringLiteral("🌲"), QStringLiteral("common"), 70, 18, false },
        { QStringLiteral("architect"),       QStringLiteral("森林建筑师"),
          QStringLiteral("在森林中摆放 15 棵树"), QStringLiteral("🌳"), QStringLiteral("gold"), 220, 19, false },

        { QStringLiteral("first_variant"),       QStringLiteral("异色初探"),
          QStringLiteral("获得第 1 种异色树种"), QStringLiteral("✨"), QStringLiteral("silver"), 120, 20, false },
        { QStringLiteral("variety_collector"),   QStringLiteral("色彩收藏家"),
          QStringLiteral("获得 6 种异色树种"), QStringLiteral("🌈"), QStringLiteral("gold"), 260, 21, false },
        { QStringLiteral("master_collector"),    QStringLiteral("全色彩大师"),
          QStringLiteral("集齐全部 12 种异色树种"), QStringLiteral("💎"), QStringLiteral("gold"), 420, 22, false },
    };
}

void AchievementEngine::validateDefinitions() const {
    QSet<QString> ids;
    QSet<QString> names;
    QSet<uint8_t> bits;

    for (const auto& info : m_infos) {
        if (ids.contains(info.id)) {
            qWarning("Duplicate achievement id detected.");
        }
        if (names.contains(info.name)) {
            qWarning("Duplicate achievement display name detected.");
        }
        if (info.storageBit >= 32 || bits.contains(info.storageBit)) {
            qWarning("Invalid or duplicate achievement storage bit detected.");
        }
        ids.insert(info.id);
        names.insert(info.name);
        bits.insert(info.storageBit);
    }
}

void AchievementEngine::loadStateFromMask() {
    uint32_t mask = coinMgr_.getUnlockedAchievementMask();
    for (int i = 0; i < m_infos.size(); ++i) {
        if (m_infos[i].storageBit < 32) {
            m_infos[i].unlocked = (mask & (1u << m_infos[i].storageBit)) != 0;
        }
    }
}

bool AchievementEngine::isUnlocked(int index) const {
    if (index < 0 || index >= m_infos.size()) return false;
    return m_infos[index].unlocked;
}

int AchievementEngine::indexOf(const QString& id) const {
    for (int i = 0; i < m_infos.size(); ++i) {
        if (m_infos[i].id == id) return i;
    }
    return -1;
}

bool AchievementEngine::unlock(int index) {
    if (index < 0 || index >= m_infos.size()) return false;
    if (m_infos[index].unlocked) return true;
    if (!coinMgr_.grantAchievement(m_infos[index].storageBit, m_infos[index].rewardCoins)) {
        return false;
    }
    m_infos[index].unlocked = true;
    emit sig_achievementUnlocked(index, m_infos[index].name, m_infos[index].description);
    return true;
}

void AchievementEngine::unlockById(const QString& id, bool condition, QVector<int>& newlyUnlocked) {
    if (!condition) return;
    int idx = indexOf(id);
    if (idx < 0 || m_infos[idx].unlocked) return;
    if (unlock(idx)) {
        newlyUnlocked.append(idx);
    } else {
        lastCheckSucceeded_ = false;
    }
}

QVector<int> AchievementEngine::checkAll(const Context& ctx) {
    lastCheckSucceeded_ = true;
    QVector<int> newlyUnlocked;
    auto check = [&](const QString& id, bool condition) {
        unlockById(id, condition, newlyUnlocked);
    };

    check(QStringLiteral("first_oak"), ctx.lastSessionSuccess && ctx.lastPlantType == QStringLiteral("OakTree"));
    check(QStringLiteral("first_rose"), ctx.lastSessionSuccess && ctx.lastPlantType == QStringLiteral("Rose"));
    check(QStringLiteral("first_sunflower"), ctx.lastSessionSuccess && ctx.lastPlantType == QStringLiteral("Sunflower"));
    check(QStringLiteral("first_pine"), ctx.lastSessionSuccess && ctx.lastPlantType == QStringLiteral("PineTree"));
    check(QStringLiteral("full_collection"), ctx.unlockedPlantCount >= 6);
    if (ctx.lastSessionSuccess) {
        check(QStringLiteral("first_focus"), ctx.totalSessions >= 1);
        check(QStringLiteral("dedicated"), ctx.totalSessions >= 10);
        check(QStringLiteral("master"), ctx.totalSessions >= 50);
    }
    check(QStringLiteral("marathon"), ctx.lastSessionSuccess && ctx.lastSessionDuration >= 7200);
    check(QStringLiteral("no_distractions"), ctx.lastSessionSuccess && ctx.lastSessionStrict && ctx.lastSessionViolations == 0);
    check(QStringLiteral("saver"), ctx.totalCoinsEarned >= 100);
    check(QStringLiteral("wealthy"), ctx.totalCoinsEarned >= 1000);
    check(QStringLiteral("decorator"), ctx.forestPlacementCount >= 5);
    check(QStringLiteral("architect"), ctx.forestPlacementCount >= 15);
    check(QStringLiteral("first_variant"), ctx.variantCount >= 1);
    check(QStringLiteral("variety_collector"), ctx.variantCount >= 6);
    check(QStringLiteral("master_collector"), ctx.variantCount >= 12);

    if (ctx.totalSuccessCount >= 1) {
        check(QStringLiteral("first_focus_old"), ctx.totalSuccessCount >= 1);
        check(QStringLiteral("deep_guardian"), ctx.totalSeconds >= 30000);
        check(QStringLiteral("coin_tycoon"), ctx.totalCoinsEarned >= 1000);
        check(QStringLiteral("resilient"), ctx.totalFailCount >= 5);
        check(QStringLiteral("biodiversity"), ctx.plantedOak && ctx.plantedPine && ctx.plantedRose);
        check(QStringLiteral("botanist"), ctx.plantedOak && ctx.plantedPine && ctx.plantedRose &&
                  ctx.plantedGinkgo && ctx.plantedSunflower && ctx.plantedCactus);
    }

    return newlyUnlocked;
}

