#include <QApplication>
#include <QDir>
#include <QTimer>
#include <cstdlib>
#include <ctime>
#include "storage/DatabaseManager.h"
#include "storage/UserManager.h"
#include "storage/PresetManager.h"
#include "core/AchievementEngine.h"
#include "core/FocusController.h"
#include "core/CoinManager.h"
#include "core/QuoteProvider.h"
#include "system/RuleEngine.h"
#include "system/SystemMonitor.h"
#include "ui/LoginDialog.h"
#include "ui/MainWindow.h"
#include "common/PathConfig.h"
#include "utils/Logger.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    app.setStyleSheet(R"(
        QWidget { background-color: #121915; color: #E8EAE6; font-family: "Microsoft YaHei", "Segoe UI", sans-serif; font-size: 14px; }
        QDialog { background-color: #1e2922; border: 1px solid #2e3f34; border-radius: 12px; }
        QGroupBox { border: 1px solid #2e3f34; border-radius: 8px; margin-top: 12px; padding-top: 16px; font-weight: bold; color: #4E9F3D; }
        QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 4px; }
        QPushButton { background-color: #2e3f34; border: 1px solid #3f5647; border-radius: 6px; padding: 6px 16px; color: #E8EAE6; font-weight: bold; }
        QPushButton:hover { background-color: #4E9F3D; border-color: #a1e887; color: #121915; }
        QPushButton:pressed { background-color: #3e8131; }
        QPushButton:disabled { background-color: #18221c; border-color: #212e26; color: #556252; }
        QPushButton#btnPrimary { background-color: #4E9F3D; border-color: #a1e887; color: #121915; }
        QPushButton#btnPrimary:hover { background-color: #5ec44b; }
        QComboBox, QSpinBox, QLineEdit, QListWidget { background-color: #16201a; border: 1px solid #2e3f34; border-radius: 6px; padding: 4px 8px; color: #E8EAE6; }
        QComboBox:hover, QSpinBox:hover, QLineEdit:hover { border-color: #4E9F3D; }
        QComboBox::drop-down { border: none; background: transparent; }
        QListWidget::item { padding: 6px; border-radius: 4px; }
        QListWidget::item:hover { background-color: #24342a; }
        QListWidget::item:selected { background-color: #4E9F3D; color: #121915; }
        QScrollArea { border: none; background: transparent; }
        QMenu { background-color: #1e2922; border: 1px solid #2e3f34; padding: 4px; }
        QMenu::item { padding: 6px 24px; border-radius: 4px; }
        QMenu::item:selected { background-color: #4E9F3D; color: #121915; }
    )");

    QString appDir = QApplication::applicationDirPath();

    Logger::instance().setLogPath(QDir(appDir).filePath("app.log").toStdString());
    Logger::instance().open();
    Logger::instance().info("Forest application started.");

    // 1. 用户登录
    QString userDbPath = QDir(appDir).filePath("users.dat");
    UserManager userManager(userDbPath.toStdString());
    if (!userManager.open()) {
        Logger::instance().error("Failed to open user database.");
        return -1;
    }

    LoginDialog loginDlg(userManager);
    if (loginDlg.exec() != QDialog::Accepted) return 0;

    uint32_t loggedUserId = loginDlg.getLoggedInUserId();
    Logger::instance().info("User logged in. userId=" + std::to_string(loggedUserId));

    // 2. 创建用户沙箱目录
    QString userDir = PathConfig::getUserSandboxDir(loggedUserId);

    std::string sessionFile = QDir(userDir).filePath("sessions.dat").toStdString();
    std::string coinFile    = QDir(userDir).filePath("coins.dat").toStdString();
    std::string presetFile  = QDir(userDir).filePath("presets.dat").toStdString();

    // 3. 初始化各存储管理器
    DatabaseManager db(sessionFile);
    if (!db.open()) {
        Logger::instance().error("Failed to open session database.");
    }

    CoinManager coinManager;
    coinManager.setDataPath(coinFile);
    coinManager.load();

    PresetManager presets(presetFile);
    presets.loadPresets();

    RuleEngine ruleEngine;
    ruleEngine.setBlacklist({"notepad.exe", "chrome.exe", "msedge.exe",
                              "steam.exe", "spotify.exe", "discord.exe"});

    FocusController focusCtl(db);
    SystemMonitor monitor(ruleEngine);
    QuoteProvider quotes;

    AchievementEngine achievements(db, coinManager);

    QTimer focusTimer;
    QObject::connect(&focusTimer, &QTimer::timeout, [&]() { focusCtl.tick(); });
    focusTimer.start(1000);

    MainWindow window(focusCtl, monitor, ruleEngine, coinManager, quotes, db, presets, achievements);
    window.show();
    Logger::instance().info("MainWindow displayed.");

    int ret = app.exec();

    Logger::instance().info("Forest application exiting.");
    db.close();
    userManager.close();
    Logger::instance().close();
    return ret;
}
