#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTimer>
#include "BuildProvenance.h"
#include <QUuid>
#include <cstdlib>
#include <ctime>
#include "storage/StartupDataService.h"
#include "storage/BackupService.h"
#include "storage/DatabaseManager.h"
#include "storage/UserManager.h"
#include "core/AchievementEngine.h"
#include "core/FocusController.h"
#include "core/FocusResultService.h"
#include "core/DashboardSnapshotService.h"
#include "core/CoinManager.h"
#include "core/QuoteProvider.h"
#include "core/UserFeatureServices.h"
#include "core/GuardianManager.h"
#include "system/RuleEngine.h"
#include "system/SystemMonitor.h"
#include "ui/AppStyle.h"
#include "ui/LoginDialog.h"
#include "ui/DialogPresenter.h"
#include "ui/MainWindow.h"
#include "common/PathConfig.h"
#include "config/UserPreferences.h"
#include "utils/Logger.h"

namespace {

int failStartup(const QString& title, const QString& detail)
{
    Logger::instance().error(title.toStdString(), "startup", "initialize-failed");
    DialogPresenter::error(nullptr, title,
        detail + QStringLiteral("\n\n请检查数据目录和日志：\n%1")
            .arg(PathConfig::getAppDataDir()));
    Logger::instance().close();
    return -1;
}

}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Forest"));
    QCoreApplication::setApplicationName(QStringLiteral("Forest"));
    QCoreApplication::setApplicationVersion(QString::fromUtf8(ForestBuild::appVersion.data()));
    AppStyle::installSmoothInteractions(app);
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    app.setStyleSheet(AppStyle::styleSheet());

    QString appDataDir = PathConfig::getAppDataDir();
    QString migratedFrom;
    StartupDataService startupData(appDataDir);
    QString startupError;
    QStringList legacyAccountFiles;
    QString legacyDirectory;
    if (!PathConfig::isPortableMode()) {
        legacyDirectory = PathConfig::legacyAppDataDir();
        legacyAccountFiles = {
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("users.dat")),
            QDir(appDataDir).filePath(QStringLiteral("app_data/users.dat"))
        };
    }
    if (!startupData.acquire(&startupError)) {
        DialogPresenter::error(nullptr, QStringLiteral("Forest 启动数据准备失败"),
            startupError + QStringLiteral("\n\n未加载账户。请关闭其他实例并检查目录权限：\n%1").arg(appDataDir));
        return -1;
    }

    Logger::instance().setLogPath(QDir(appDataDir).filePath("app.log").toStdString());
    Logger::instance().setSessionId(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    if (!Logger::instance().open()) {
        DialogPresenter::warning(nullptr, QStringLiteral("诊断日志未开启"),
            QStringLiteral("无法打开或轮转日志。未删除受阻文件，本次运行将缺少诊断日志。\n请检查日志权限与占用：\n%1")
                .arg(QDir(appDataDir).filePath(QStringLiteral("app.log"))));
    }
    Logger::instance().info("forest application started.", "startup", "initialize");
    Logger::instance().info(std::string(PathConfig::isPortableMode() ? "portable" : "installed") +
                            " data mode.", "startup", "data-mode");
    QString restoredFrom;
    QString restoreError;
    QString recoveryNotice;
    if (!BackupService::restoreIfRequested(appDataDir, &restoredFrom, &restoreError, &recoveryNotice,
                                           &startupData)) {
        return failStartup(QStringLiteral("Forest 数据恢复失败"), restoreError);
    }
    if (!recoveryNotice.isEmpty()) {
        Logger::instance().info("Interrupted restore rolled back.", "storage", "restore-rollback");
        DialogPresenter::information(nullptr, QStringLiteral("Forest 恢复已回滚"), recoveryNotice);
    }
    if (!restoredFrom.isEmpty()) {
        Logger::instance().info("Startup restored a complete snapshot.", "storage", "restore");
        DialogPresenter::information(nullptr, QStringLiteral("Forest 数据已恢复"),
            QStringLiteral("已从备份恢复本地数据。"));
    }
    if (!startupData.prepare(legacyDirectory, legacyAccountFiles, &migratedFrom, &startupError)) {
        return failStartup(QStringLiteral("Forest 启动数据准备失败"), startupError);
    }
    if (!migratedFrom.isEmpty()) {
        Logger::instance().info("Legacy data migration completed.", "storage", "migrate");
    }
    // Account migration has already succeeded under the directory lock.
    QString userDbPath = PathConfig::getAppDataFilePath("users.dat");
    UserManager userManager(userDbPath.toStdString());
    if (!userManager.open()) {
        return failStartup(QStringLiteral("Forest 用户数据无法打开"),
                           QStringLiteral("无法打开用户数据。请从备份恢复后重试。"));
    }

    LoginDialog loginDlg(userManager);
    if (loginDlg.exec() != QDialog::Accepted) return 0;

    uint32_t loggedUserId = loginDlg.getLoggedInUserId();
    Logger::instance().info("User logged in. userId=" + std::to_string(loggedUserId), "account", "login");

    // Focus records and wallet data are isolated per logged-in user.
    QString userDir = PathConfig::getUserSandboxDir(loggedUserId);

    UserPreferences& preferences = UserPreferences::instance();
    preferences.configure(appDataDir);
    QVector<uint32_t> knownUserIds;
    for (const auto& user : userManager.getAllUsers()) knownUserIds.push_back(user.first);
    preferences.bindAccount(userDir, loggedUserId, knownUserIds);
    if (!preferences.load()) {
        return failStartup(QStringLiteral("Forest 偏好设置无法打开"), preferences.lastError());
    }
    if (!preferences.migrationNotice().isEmpty()) {
        Logger::instance().info("Preferences migration requires user notice.", "preferences", "migrate");
    }

    std::string sessionFile = QDir(userDir).filePath("sessions.dat").toStdString();
    std::string coinFile    = QDir(userDir).filePath("coins.dat").toStdString();

    DatabaseManager db(sessionFile);
    if (!db.open()) {
        return failStartup(QStringLiteral("Forest 专注数据无法打开"),
                           QStringLiteral("无法打开专注记录：%1").arg(db.lastError()));
    }

    CoinManager coinManager;
    coinManager.setDataPath(coinFile);
    uint32_t recordCount = 0;
    if (!db.count(recordCount)) {
        return failStartup(QStringLiteral("Forest 专注数据查询失败"), db.lastError());
    }
    if (recordCount > 0 && !QFile::exists(QString::fromStdString(coinFile))) {
        return failStartup(QStringLiteral("Forest 金币数据缺失"),
                           QStringLiteral("当前账户已有专注历史，但钱包文件缺失。未创建空钱包，请恢复完整数据快照后重试。"));
    }
    if (!coinManager.load()) {
        return failStartup(QStringLiteral("Forest 金币数据无法打开"),
                           coinManager.lastError().isEmpty() ? QStringLiteral("无法打开金币数据。请恢复完整数据快照后重试。") : coinManager.lastError());
    }

    RuleEngine ruleEngine;
    ruleEngine.setBlacklist(preferences.processBlacklist());

    FocusController focusCtl(db);
    SystemMonitor monitor(ruleEngine);
    QuoteProvider quotes;

    AchievementEngine achievements(db, coinManager);
    int ret = 0;

    {
        UserFeatureServices services(userDir, loggedUserId, userManager, db);
        if (!services.load()) {
            return failStartup(QStringLiteral("Forest 功能数据无法打开"),
                               services.lastError());
        }
        QString economicNotice;
        QString economicError;
        if (!services.recoverEconomy(coinManager, &economicNotice, &economicError)) {
            return failStartup(QStringLiteral("Forest 奖励操作恢复失败"), economicError);
        }
        FocusResultService focusResults(db, coinManager, achievements, services.gacha(),
                                        services.forestLayout(), services.guardian(),
                                        services.challenges(), services.settlements());
        const bool guardianHistoryUpgraded = services.guardian()->needsHistoryReconciliation();
        const auto recoveredOutcomes = focusResults.recoverPendingRecords();
        for (const auto& outcome : recoveredOutcomes) {
            if (!outcome.applied) {
                return failStartup(QStringLiteral("Forest 专注结算恢复失败"),
                    QStringLiteral("专注记录 #%1 无法完成恢复结算：%2")
                        .arg(outcome.recordId).arg(outcome.error));
            }
        }
        QString notice = preferences.migrationNotice();
        if (guardianHistoryUpgraded) {
            notice += (notice.isEmpty() ? QString() : QStringLiteral("\n\n")) +
                QStringLiteral("时间守护已升级：每日进度按成功专注的开始日期和有效秒数校正；"
                               "旧累计时长及最长连续记录保留。旧文件未保存逐日目标，"
                               "缺少目标的历史日期以迁移时的目标补充，过去的金币与成就奖励不变。");
        }
        if (!economicNotice.isEmpty()) notice += (notice.isEmpty() ? QString() : QStringLiteral("\n\n")) + economicNotice;
        if (!notice.isEmpty()) DialogPresenter::information(nullptr, QStringLiteral("Forest 数据恢复与迁移"), notice);
        DashboardSnapshotService dashboardSnapshots(db, coinManager, services.tags(),
                                                    services.guardian(), services.challenges());

        // A single application-level timer drives the focus state machine.
        QTimer focusTimer;
        QObject::connect(&focusTimer, &QTimer::timeout, [&]() { focusCtl.tick(); });
        focusTimer.start(1000);

        MainWindow window(focusCtl, monitor, ruleEngine, coinManager, quotes, db,
                          achievements, focusResults, dashboardSnapshots, services);
        window.show();
        Logger::instance().info("MainWindow displayed.");

        ret = app.exec();

        Logger::instance().info("forest application exiting.");
        if (!services.save()) {
            Logger::instance().error("Failed to save feature data on exit.");
        }
    }

    db.close();
    userManager.close();
    Logger::instance().close();
    return ret;
}
