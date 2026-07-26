#include <QApplication>
#include <QDir>
#include <QTimer>
#include <cstdlib>
#include <ctime>
#include "storage/ApplicationInstanceLock.h"
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
    Logger::instance().error(detail.toStdString());
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
    AppStyle::installFocusVisibility(app);
    QCoreApplication::setOrganizationName(QStringLiteral("Forest"));
    QCoreApplication::setApplicationName(QStringLiteral("Forest"));
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    app.setStyleSheet(AppStyle::styleSheet());

    QString appDataDir = PathConfig::getAppDataDir();
    QString migratedFrom;
    if (!PathConfig::migrateLegacyAppData(&migratedFrom)) {
        DialogPresenter::error(nullptr, QStringLiteral("Forest 数据迁移失败"),
            QStringLiteral("无法迁移旧版本的数据。请确认旧安装目录和当前数据目录均可读写。"));
        return -1;
    }
    ApplicationInstanceLock instanceLock(appDataDir);
    QString lockError;
    if (!instanceLock.acquire(&lockError)) {
        DialogPresenter::error(nullptr, QStringLiteral("Forest 已在运行"), lockError);
        return -1;
    }

    Logger::instance().setLogPath(QDir(appDataDir).filePath("app.log").toStdString());
    Logger::instance().open();
    Logger::instance().info("forest application started.");
    Logger::instance().info(std::string(PathConfig::isPortableMode() ? "portable" : "installed") +
                            " data mode; dir=" + appDataDir.toStdString());
    if (!migratedFrom.isEmpty()) {
        Logger::instance().info("migrated legacy data from " + migratedFrom.toStdString());
    }
    QString restoredFrom;
    QString restoreError;
    if (!BackupService::restoreIfRequested(appDataDir, &restoredFrom, &restoreError)) {
        return failStartup(QStringLiteral("Forest 数据恢复失败"), restoreError);
    }
    if (!restoredFrom.isEmpty()) {
        Logger::instance().info("restored data from " + restoredFrom.toStdString());
        DialogPresenter::information(nullptr, QStringLiteral("Forest 数据已恢复"),
            QStringLiteral("已从备份恢复本地数据。"));
    }
    UserPreferences::instance().configure(appDataDir);
    if (!UserPreferences::instance().load()) {
        return failStartup(QStringLiteral("Forest 偏好设置无法打开"),
                           UserPreferences::instance().lastError());
    }
    const auto accessibility = UserPreferences::instance().accessibilityOptions();
    app.setStyleSheet(AppStyle::styleSheet(accessibility.fontScalePercent,
                                           accessibility.highContrast));

    // Account data is shared by all users and is migrated once from old builds.
    PathConfig::migrateLegacyFileToAppData("users.dat");
    QString userDbPath = PathConfig::getAppDataFilePath("users.dat");
    UserManager userManager(userDbPath.toStdString());
    if (!userManager.open()) {
        return failStartup(QStringLiteral("Forest 用户数据无法打开"),
                           QStringLiteral("无法打开用户数据。请从备份恢复后重试。"));
    }

    LoginDialog loginDlg(userManager);
    if (loginDlg.exec() != QDialog::Accepted) return 0;

    uint32_t loggedUserId = loginDlg.getLoggedInUserId();
    Logger::instance().info("User logged in. userId=" + std::to_string(loggedUserId));

    // Focus records and wallet data are isolated per logged-in user.
    QString userDir = PathConfig::getUserSandboxDir(loggedUserId);

    std::string sessionFile = QDir(userDir).filePath("sessions.dat").toStdString();
    std::string coinFile    = QDir(userDir).filePath("coins.dat").toStdString();

    DatabaseManager db(sessionFile);
    if (!db.open()) {
        return failStartup(QStringLiteral("Forest 专注数据无法打开"),
                           QStringLiteral("无法打开专注记录：%1").arg(db.lastError()));
    }

    CoinManager coinManager;
    coinManager.setDataPath(coinFile);
    if (!coinManager.load()) {
        return failStartup(QStringLiteral("Forest 金币数据无法打开"),
                           QStringLiteral("无法打开金币数据。请从备份恢复后重试。"));
    }

    RuleEngine ruleEngine;
    ruleEngine.setBlacklist({"notepad.exe", "chrome.exe", "msedge.exe",
                              "steam.exe", "spotify.exe", "discord.exe"});

    FocusController focusCtl(db);
    SystemMonitor monitor(ruleEngine);
    QuoteProvider quotes;

    AchievementEngine achievements(db, coinManager);
    int ret = 0;

    {
        UserFeatureServices services(userDir, loggedUserId, userManager, db);
        if (!services.load()) {
            return failStartup(QStringLiteral("Forest 功能数据无法打开"),
                               QStringLiteral("无法打开本地功能数据。请从备份恢复后重试。"));
        }
        FocusResultService focusResults(db, coinManager, achievements, services.gacha(),
                                        services.forestLayout(), services.guardian(),
                                        services.challenges(), services.settlements());
        const auto recoveredOutcomes = focusResults.recoverPendingRecords();
        for (const auto& outcome : recoveredOutcomes) {
            if (!outcome.applied) {
                return failStartup(QStringLiteral("Forest 专注结算恢复失败"),
                    QStringLiteral("专注记录 #%1 无法完成恢复结算：%2")
                        .arg(outcome.recordId).arg(outcome.error));
            }
        }
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
