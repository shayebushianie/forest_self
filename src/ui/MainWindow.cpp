#include "ui/MainWindow.h"
#include "ui/TimerRing.h"
#include "ui/GardenCanvas.h"
#include "ui/StoreDialog.h"
#include "ui/StatisticsDialog.h"
#include "ui/AchievementToast.h"
#include "ui/HistoryWidget.h"
#include "core/FocusController.h"
#include "core/CoinManager.h"
#include "core/QuoteProvider.h"
#include "core/AchievementEngine.h"
#include "system/RuleEngine.h"
#include "system/SystemMonitor.h"
#include "storage/DatabaseManager.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QCloseEvent>
#include <QMenu>
#include <QApplication>
#include <QDir>
#include <QStyle>
#include <QScrollArea>
#include <QGroupBox>

MainWindow::MainWindow(FocusController& controller,
                       SystemMonitor& monitor,
                       RuleEngine& ruleEngine,
                       CoinManager& coinManager,
                       QuoteProvider& quotes,
                       DatabaseManager& db,
                       PresetManager& presets,
                       AchievementEngine& achievements,
                       QWidget* parent)
    : QMainWindow(parent),
      controller_(controller), monitor_(monitor),
      ruleEngine_(ruleEngine), coinManager_(coinManager),
      quotes_(quotes), db_(db), presets_(presets),
      achievements_(achievements)
{
    setWindowTitle(QStringLiteral("Forest 专注森林"));
    setMinimumSize(720, 520);

    initNavigationLayout();

    timerRing_ = new TimerRing;
    gardenCanvas_ = new GardenCanvas;

    // ========== Page 0: 专注主页 ==========
    auto* page0 = new QWidget;
    auto* p0Layout = new QVBoxLayout(page0);
    p0Layout->setContentsMargins(16, 16, 16, 16);

    p0Layout->addWidget(timerRing_, 1);

    auto* btnLayout = new QHBoxLayout;
    startBtn_   = new QPushButton(QStringLiteral("开始专注"));
    startBtn_->setObjectName("btnPrimary");
    pauseBtn_   = new QPushButton(QStringLiteral("暂停"));
    abandonBtn_ = new QPushButton(QStringLiteral("放弃"));
    pauseBtn_->setEnabled(false);
    abandonBtn_->setEnabled(false);
    btnLayout->addWidget(startBtn_);
    btnLayout->addWidget(pauseBtn_);
    btnLayout->addWidget(abandonBtn_);
    btnLayout->addStretch();
    p0Layout->addLayout(btnLayout);

    // Preset cards
    const auto& list = presets_.getPresets();
    QStringList names = {QStringLiteral("Code Rush"), QStringLiteral("Deep Read"), QStringLiteral("Free Study")};
    QStringList labels = {QStringLiteral("代码冲刺 30min"), QStringLiteral("沉浸阅读 45min"), QStringLiteral("深度自学")};

    auto* cardWidget = new QWidget(page0);
    auto* cardLayout = new QHBoxLayout(cardWidget);
    cardLayout->setContentsMargins(0, 4, 0, 0);

    for (size_t i = 0; i < list.size(); ++i) {
        auto* btn = new QPushButton(cardWidget);
        btn->setText(QStringLiteral("%1\n%2").arg(names[i]).arg(labels[i]));
        btn->setStyleSheet(
            "QPushButton { background-color: #1e2922; border:1px dashed #3f5647;"
            " border-radius:8px; padding:8px 12px; font-weight:bold; font-size:12px;"
            " color:#E8EAE6; text-align:left; }"
            "QPushButton:hover { background-color:#24342a; border:1px solid #4E9F3D; }");
        btn->setMinimumWidth(140);
        QObject::connect(btn, &QPushButton::clicked, this, [this, i]() {
            startPresetFocus(static_cast<uint32_t>(i));
        });
        cardLayout->addWidget(btn);
    }
    cardLayout->addStretch();
    p0Layout->addWidget(cardWidget);

    stackedWidget_->addWidget(page0);

    // ========== Page 1: 我的森林 ==========
    auto* page1 = new QWidget;
    auto* p1Layout = new QVBoxLayout(page1);
    p1Layout->setContentsMargins(0, 0, 0, 0);

    auto* forestLabel = new QLabel(QStringLiteral("我的森林"), page1);
    forestLabel->setAlignment(Qt::AlignCenter);
    forestLabel->setStyleSheet("font-size:16px; font-weight:bold; color:#8A9A86; padding:12px; background:transparent;");
    p1Layout->addWidget(forestLabel);

    p1Layout->addWidget(gardenCanvas_, 1);

    historyWidget_ = new HistoryWidget(db_, page1);
    p1Layout->addWidget(historyWidget_, 1);

    auto* p1BtnBar = new QHBoxLayout;
    p1BtnBar->addStretch();
    auto* statsBtn = new QPushButton(QStringLiteral("查看统计"), page1);
    QObject::connect(statsBtn, &QPushButton::clicked, this, [this]() {
        StatisticsDialog dlg(db_, this);
        dlg.exec();
    });
    p1BtnBar->addWidget(statsBtn);
    p1BtnBar->setContentsMargins(8, 4, 8, 8);
    p1Layout->addLayout(p1BtnBar);

    stackedWidget_->addWidget(page1);

    // ========== Page 2: 植物商城 ==========
    auto* page2 = new QWidget;
    auto* p2Layout = new QVBoxLayout(page2);
    p2Layout->setContentsMargins(20, 20, 20, 20);

    auto* shopTitle = new QLabel(QStringLiteral("植物商城"), page2);
    shopTitle->setStyleSheet("font-size:18px; font-weight:bold; color:#E8EAE6; background:transparent; border:none;");
    shopTitle->setAlignment(Qt::AlignCenter);
    p2Layout->addWidget(shopTitle);

    struct { const char* name; uint32_t cost; } items[] = {
        {"松树 (PineTree)", 500}, {"玫瑰 (Rose)", 500},
        {"银杏树", 1000}, {"向日葵", 1000}, {"仙人掌", 1500}};

    auto* scroll = new QScrollArea(page2);
    scroll->setWidgetResizable(true);
    auto* scrollContent = new QWidget(scroll);
    auto* sLayout = new QVBoxLayout(scrollContent);
    sLayout->setSpacing(10);

    for (const auto& item : items) {
        auto* card = new QWidget(scrollContent);
        card->setStyleSheet("QWidget { background-color:#24332b; border:1px solid #2e3f34; border-radius:8px; }");
        auto* cLayout = new QHBoxLayout(card);
        cLayout->setContentsMargins(12, 8, 12, 8);

        auto* name = new QLabel(QString::fromUtf8(item.name), card);
        name->setStyleSheet("font-weight:bold; font-size:15px; color:#E8EAE6; background:transparent; border:none;");
        cLayout->addWidget(name);
        cLayout->addStretch();

        auto* buyBtn = new QPushButton(QStringLiteral("%1 金币").arg(item.cost), card);
        buyBtn->setObjectName("btnPrimary");
        QObject::connect(buyBtn, &QPushButton::clicked, this, [this, cost = item.cost]() {
            if (coinManager_.spend(cost)) {
                QMessageBox::information(this, QStringLiteral("解锁成功"), QStringLiteral("新植物已解锁！"));
            } else {
                QMessageBox::warning(this, QStringLiteral("金币不足"), QStringLiteral("请先完成专注任务赚取金币。"));
            }
        });
        cLayout->addWidget(buyBtn);
        sLayout->addWidget(card);
    }
    sLayout->addStretch();
    scroll->setWidget(scrollContent);
    p2Layout->addWidget(scroll, 1);

    stackedWidget_->addWidget(page2);

    // ========== Page 3: 系统设置 ==========
    auto* page3 = new QWidget;
    auto* p3Layout = new QVBoxLayout(page3);
    p3Layout->setContentsMargins(20, 20, 20, 20);
    p3Layout->setSpacing(8);

    auto* setTitle = new QLabel(QStringLiteral("系统设置"), page3);
    setTitle->setStyleSheet("font-size:18px; font-weight:bold; color:#E8EAE6; background:transparent; border:none;");
    setTitle->setAlignment(Qt::AlignCenter);
    p3Layout->addWidget(setTitle);

    auto* scroll3 = new QScrollArea(page3);
    scroll3->setWidgetResizable(true);
    auto* scrollContent3 = new QWidget(scroll3);
    auto* s3Layout = new QVBoxLayout(scrollContent3);
    s3Layout->setSpacing(10);

    auto* plantGroup = new QGroupBox(QStringLiteral("植物选择"));
    auto* plantLayout = new QHBoxLayout(plantGroup);
    settingsPlantCombo_ = new QComboBox;
    settingsPlantCombo_->addItem(QStringLiteral("橡树 (OakTree)"), 0);
    settingsPlantCombo_->addItem(QStringLiteral("松树 (PineTree)"), 1);
    settingsPlantCombo_->addItem(QStringLiteral("玫瑰 (Rose)"), 2);
    plantLayout->addWidget(new QLabel(QStringLiteral("选择植物:")));
    plantLayout->addWidget(settingsPlantCombo_);
    s3Layout->addWidget(plantGroup);

    auto* modeGroup = new QGroupBox(QStringLiteral("计时模式"));
    auto* modeLayout = new QHBoxLayout(modeGroup);
    settingsModeCombo_ = new QComboBox;
    settingsModeCombo_->addItem(QStringLiteral("倒计时 (番茄钟)"), 0);
    settingsModeCombo_->addItem(QStringLiteral("正计时 (自由专注)"), 1);
    modeLayout->addWidget(new QLabel(QStringLiteral("模式:")));
    modeLayout->addWidget(settingsModeCombo_);
    s3Layout->addWidget(modeGroup);

    auto* focusGroup = new QGroupBox(QStringLiteral("专注模式"));
    auto* focusLayout = new QHBoxLayout(focusGroup);
    settingsFocusModeCombo_ = new QComboBox;
    settingsFocusModeCombo_->addItem(QStringLiteral("严格模式 (切到游戏立即枯萎)"), 0);
    settingsFocusModeCombo_->addItem(QStringLiteral("温和模式 (切屏不枯，金币减半)"), 1);
    focusLayout->addWidget(settingsFocusModeCombo_);
    s3Layout->addWidget(focusGroup);

    auto* timeGroup = new QGroupBox(QStringLiteral("专注时长"));
    auto* timeLayout = new QHBoxLayout(timeGroup);
    settingsMinutesSpin_ = new QSpinBox;
    settingsMinutesSpin_->setRange(10, 120);
    settingsMinutesSpin_->setValue(25);
    settingsMinutesSpin_->setSuffix(QStringLiteral(" 分钟"));
    timeLayout->addWidget(new QLabel(QStringLiteral("时长:")));
    timeLayout->addWidget(settingsMinutesSpin_);
    s3Layout->addWidget(timeGroup);

    QObject::connect(settingsModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int idx) { settingsMinutesSpin_->setEnabled(idx == 0); });

    auto* tagGroup = new QGroupBox(QStringLiteral("标签"));
    auto* tagLayout = new QHBoxLayout(tagGroup);
    settingsTagCombo_ = new QComboBox;
    settingsTagCombo_->addItem(QStringLiteral("无标签"), 0);
    settingsTagCombo_->addItem(QStringLiteral("学习"), 1);
    settingsTagCombo_->addItem(QStringLiteral("写代码"), 2);
    settingsTagCombo_->addItem(QStringLiteral("阅读"), 3);
    settingsTagCombo_->addItem(QStringLiteral("运动"), 4);
    tagLayout->addWidget(new QLabel(QStringLiteral("标签:")));
    tagLayout->addWidget(settingsTagCombo_);
    s3Layout->addWidget(tagGroup);

    auto* blGroup = new QGroupBox(QStringLiteral("进程黑名单"));
    auto* blLayout = new QVBoxLayout(blGroup);
    settingsBlacklistWidget_ = new QListWidget;
    for (const QString& name : ruleEngine_.blacklist()) {
        settingsBlacklistWidget_->addItem(name);
    }
    blLayout->addWidget(settingsBlacklistWidget_);
    auto* inputLayout = new QHBoxLayout;
    settingsBlacklistInput_ = new QLineEdit;
    settingsBlacklistInput_->setPlaceholderText(QStringLiteral("输入进程名，如 chrome.exe"));
    auto* addBtn = new QPushButton(QStringLiteral("添加"));
    auto* removeBtn = new QPushButton(QStringLiteral("移除"));
    inputLayout->addWidget(settingsBlacklistInput_);
    inputLayout->addWidget(addBtn);
    inputLayout->addWidget(removeBtn);
    blLayout->addLayout(inputLayout);
    s3Layout->addWidget(blGroup);

    QObject::connect(addBtn, &QPushButton::clicked, this, [this]() {
        QString name = settingsBlacklistInput_->text().trimmed();
        if (name.isEmpty()) return;
        ruleEngine_.addToBlacklist(name);
        settingsBlacklistWidget_->addItem(name);
        settingsBlacklistInput_->clear();
    });
    QObject::connect(removeBtn, &QPushButton::clicked, this, [this]() {
        auto* item = settingsBlacklistWidget_->currentItem();
        if (!item) return;
        ruleEngine_.removeFromBlacklist(item->text());
        delete settingsBlacklistWidget_->takeItem(settingsBlacklistWidget_->row(item));
    });

    s3Layout->addStretch();
    scroll3->setWidget(scrollContent3);
    p3Layout->addWidget(scroll3, 1);

    stackedWidget_->addWidget(page3);

    // ========== 信号连接 ==========
    QObject::connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    QObject::connect(pauseBtn_, &QPushButton::clicked, this, &MainWindow::onPauseResumeClicked);
    QObject::connect(abandonBtn_, &QPushButton::clicked, this, &MainWindow::onAbandonClicked);

    QObject::connect(&controller_, &FocusController::sig_tick,
        this, [this](uint32_t ds, bool sw) { timerRing_->setDisplaySeconds(ds, sw); });

    QObject::connect(&monitor_, &SystemMonitor::sig_violationDetected,
        &controller_, &FocusController::handleViolationDetected);
    QObject::connect(&monitor_, &SystemMonitor::sig_safeWindowDetected,
        &controller_, &FocusController::handleSafeWindowDetected);

    QObject::connect(&controller_, &FocusController::sig_softViolation,
        this, [this](const QString&) {
            trayIcon_->showMessage(QStringLiteral("专注监督系统"),
                QStringLiteral("当前是温和模式，检测到分心行为，金币收益已减半。"),
                QSystemTrayIcon::Information, 5000);
        });

    QObject::connect(&controller_, &FocusController::sig_strictWarningTick,
        this, [this](uint32_t r) {
            if (!warningOverlay_) return;
            if (r == 0) { warningOverlay_->hide(); return; }
            warningLabel_->setText(
                QStringLiteral("违规！%1 秒内切回，否则小树枯萎").arg(r));
            warningOverlay_->show(); warningOverlay_->raise();
        });

    QObject::connect(&controller_, &FocusController::sig_stateChanged,
        this, [this](FocusController::State s) {
            if (s != FocusController::State::WARNING && warningOverlay_) warningOverlay_->hide();
            updateUI();
        });

    QObject::connect(&controller_, &FocusController::sig_growthStageChanged,
        this, [this](uint32_t) { updateUI(); });

    QObject::connect(&quoteTimer_, &QTimer::timeout, this, [this]() {
        timerRing_->setQuote(quotes_.getRandomQuote());
    });

    QObject::connect(&achievements_, &AchievementEngine::sig_achievementUnlocked,
        this, [this](uint32_t, const QString& t, const QString& d) {
            AchievementToast::showToast(this, t, d);
        });

    warningOverlay_ = new QWidget(this);
    warningOverlay_->setStyleSheet("background-color: rgba(180,40,40,220);");
    warningOverlay_->setGeometry(0, 0, width(), height());
    warningLabel_ = new QLabel(warningOverlay_);
    warningLabel_->setAlignment(Qt::AlignCenter);
    warningLabel_->setStyleSheet("font-size:22px; font-weight:bold; color:#D8B257; background:transparent;");
    warningLabel_->setGeometry(0, height()/3, width(), 100);
    warningOverlay_->hide();

    setupTrayIcon();
    refreshGarden();
    updateUI();
}

void MainWindow::initNavigationLayout()
{
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    sidebarWidget_ = new QWidget(centralWidget);
    sidebarWidget_->setFixedWidth(180);
    sidebarWidget_->setObjectName("sidebarWidget");
    sidebarWidget_->setStyleSheet(
        "QWidget#sidebarWidget { background-color: #1e2922; border-right: 1px solid #2e3f34; }"
        "QPushButton[nav=\"true\"] { background:transparent; border:none; border-radius:0;"
        "  border-left:3px solid transparent; text-align:left; padding:12px 16px;"
        "  font-size:14px; font-weight:bold; color:#8A9A86; }"
        "QPushButton[nav=\"true\"]:hover { background-color:#24342a; color:#E8EAE6; }"
        "QPushButton[nav=\"true\"][active=\"true\"] { background-color:#16201a; color:#4E9F3D;"
        "  border-left:4px solid #4E9F3D; }"
        "QPushButton#btnToggle { background:transparent; border:none; text-align:left;"
        "  padding:16px; font-weight:bold; color:#E8EAE6; font-size:15px; }"
        "QPushButton#btnToggle:hover { color:#4E9F3D; }");

    auto* sidebarLayout = new QVBoxLayout(sidebarWidget_);
    sidebarLayout->setContentsMargins(0, 10, 0, 10);
    sidebarLayout->setSpacing(6);

    btnToggle_ = new QPushButton(QStringLiteral("  ☰  收起菜单"), sidebarWidget_);
    btnToggle_->setObjectName("btnToggle");
    sidebarLayout->addWidget(btnToggle_);

    btnHome_ = new QPushButton(QStringLiteral("  💻 专注主页"), sidebarWidget_);
    btnForest_ = new QPushButton(QStringLiteral("  🌳 我的森林"), sidebarWidget_);
    btnShop_ = new QPushButton(QStringLiteral("  🪙 植物商城"), sidebarWidget_);
    btnSettings_ = new QPushButton(QStringLiteral("  ⚙️ 系统设置"), sidebarWidget_);

    btnHome_->setProperty("nav", "true");
    btnForest_->setProperty("nav", "true");
    btnShop_->setProperty("nav", "true");
    btnSettings_->setProperty("nav", "true");

    sidebarLayout->addWidget(btnHome_);
    sidebarLayout->addWidget(btnForest_);
    sidebarLayout->addWidget(btnShop_);
    sidebarLayout->addWidget(btnSettings_);
    sidebarLayout->addStretch();

    mainLayout->addWidget(sidebarWidget_);

    stackedWidget_ = new QStackedWidget(centralWidget);
    stackedWidget_->setStyleSheet("QStackedWidget { background-color: #121915; }");
    mainLayout->addWidget(stackedWidget_);

    QObject::connect(btnToggle_, &QPushButton::clicked, this, &MainWindow::toggleSidebar);
    QObject::connect(btnHome_, &QPushButton::clicked, this, [this](){ switchPage(0); });
    QObject::connect(btnForest_, &QPushButton::clicked, this, [this](){ switchPage(1); });
    QObject::connect(btnShop_, &QPushButton::clicked, this, [this](){ switchPage(2); });
    QObject::connect(btnSettings_, &QPushButton::clicked, this, [this](){ switchPage(3); });

    switchPage(0);
}

void MainWindow::toggleSidebar()
{
    int startW = isSidebarExpanded_ ? 180 : 60;
    int endW   = isSidebarExpanded_ ? 60 : 180;
    isSidebarExpanded_ = !isSidebarExpanded_;

    auto* animMin = new QPropertyAnimation(sidebarWidget_, "minimumWidth", this);
    animMin->setDuration(220);
    animMin->setStartValue(startW);
    animMin->setEndValue(endW);
    animMin->setEasingCurve(QEasingCurve::InOutQuad);

    auto* animMax = new QPropertyAnimation(sidebarWidget_, "maximumWidth", this);
    animMax->setDuration(220);
    animMax->setStartValue(startW);
    animMax->setEndValue(endW);
    animMax->setEasingCurve(QEasingCurve::InOutQuad);

    animMin->start(QAbstractAnimation::DeleteWhenStopped);
    animMax->start(QAbstractAnimation::DeleteWhenStopped);

    if (isSidebarExpanded_) {
        btnToggle_->setText(QStringLiteral("  ☰  收起菜单"));
        btnHome_->setText(QStringLiteral("  💻 专注主页"));
        btnForest_->setText(QStringLiteral("  🌳 我的森林"));
        btnShop_->setText(QStringLiteral("  🪙 植物商城"));
        btnSettings_->setText(QStringLiteral("  ⚙️ 系统设置"));
    } else {
        btnToggle_->setText(QStringLiteral("  ☰"));
        btnHome_->setText(QStringLiteral("  💻"));
        btnForest_->setText(QStringLiteral("  🌳"));
        btnShop_->setText(QStringLiteral("  🪙"));
        btnSettings_->setText(QStringLiteral("  ⚙️"));
    }
}

void MainWindow::switchPage(int index)
{
    stackedWidget_->setCurrentIndex(index);
    btnHome_->setProperty("active", index == 0 ? "true" : "false");
    btnForest_->setProperty("active", index == 1 ? "true" : "false");
    btnShop_->setProperty("active", index == 2 ? "true" : "false");
    btnSettings_->setProperty("active", index == 3 ? "true" : "false");
    btnHome_->style()->unpolish(btnHome_); btnHome_->style()->polish(btnHome_);
    btnForest_->style()->unpolish(btnForest_); btnForest_->style()->polish(btnForest_);
    btnShop_->style()->unpolish(btnShop_); btnShop_->style()->polish(btnShop_);
    btnSettings_->style()->unpolish(btnSettings_); btnSettings_->style()->polish(btnSettings_);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (controller_.currentState() == FocusController::State::RUNNING ||
        controller_.currentState() == FocusController::State::PAUSED ||
        controller_.currentState() == FocusController::State::WARNING) {
        auto result = QMessageBox::question(this,
            QStringLiteral("确认退出"),
            QStringLiteral("正在专注中！退出将会导致小树枯萎。\n确定要退出吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result == QMessageBox::Yes) {
            controller_.abandonFocus();
            event->accept();
        } else { event->ignore(); }
    } else { event->accept(); }
}

void MainWindow::onStartClicked()
{
    auto mode = settingsModeCombo_->currentData().toUInt() == 1
        ? FocusController::TimerMode::STOPWATCH : FocusController::TimerMode::COUNTDOWN;
    auto fm = settingsFocusModeCombo_->currentData().toUInt() == 1
        ? FocusController::FocusMode::GENTLE_MODE : FocusController::FocusMode::STRICT_MODE;

    controller_.startFocus(
        static_cast<uint32_t>(settingsPlantCombo_->currentData().toUInt()),
        static_cast<uint32_t>(settingsMinutesSpin_->value()),
        mode, fm);

    monitor_.startMonitoring();
    quoteTimer_.start(10000);
    timerRing_->setQuote(quotes_.getRandomQuote());
    switchPage(0);
    updateUI();
}

void MainWindow::onPauseResumeClicked()
{
    if (controller_.currentState() == FocusController::State::RUNNING) {
        controller_.pauseFocus(); monitor_.stopMonitoring(); quoteTimer_.stop();
    } else if (controller_.currentState() == FocusController::State::PAUSED) {
        controller_.resumeFocus(); monitor_.startMonitoring(); quoteTimer_.start(10000);
    }
    updateUI();
}

void MainWindow::onAbandonClicked()
{
    bool sw = (controller_.timerMode() == FocusController::TimerMode::STOPWATCH);
    if (sw && controller_.actualSeconds() < 600) {
        auto btn = QMessageBox::warning(this, QStringLiteral("专注时间过短"),
            QStringLiteral("不足10分钟，完成树苗会枯萎且无金币。确定？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn == QMessageBox::Yes) controller_.completeFocus();
        else return;
    } else if (sw) {
        controller_.completeFocus();
    } else {
        auto r = QMessageBox::question(this, QStringLiteral("确认放弃"),
            QStringLiteral("放弃后小树会枯萎，确定吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
        controller_.abandonFocus();
    }
    monitor_.stopMonitoring(); quoteTimer_.stop();
    updateUI(); refreshGarden();
}

void MainWindow::updateUI()
{
    auto state = controller_.currentState();
    bool running = (state == FocusController::State::RUNNING);
    bool paused  = (state == FocusController::State::PAUSED);
    bool idle = (state == FocusController::State::IDLE ||
                 state == FocusController::State::SUCCESS ||
                 state == FocusController::State::FAILED);

    startBtn_->setEnabled(idle);
    pauseBtn_->setEnabled(running || paused);
    abandonBtn_->setEnabled(running || paused);

    if (controller_.timerMode() == FocusController::TimerMode::STOPWATCH) {
        abandonBtn_->setText(QStringLiteral("完成专注"));
        pauseBtn_->setEnabled(false);
    } else {
        abandonBtn_->setText(QStringLiteral("放弃"));
    }
    if (running) pauseBtn_->setText(QStringLiteral("暂停"));
    else if (paused) pauseBtn_->setText(QStringLiteral("继续"));

    if (state == FocusController::State::SUCCESS) {
        uint32_t coins = controller_.actualSeconds() / 300;
        coinManager_.earn(coins);
        achievements_.checkAndUnlock();
        QMessageBox::information(this, QStringLiteral("恭喜！"),
            QStringLiteral("专注完成！获得了 %1 枚金币！").arg(coins));
        monitor_.stopMonitoring(); quoteTimer_.stop(); refreshGarden();
    } else if (state == FocusController::State::FAILED) {
        achievements_.checkAndUnlock();
        monitor_.stopMonitoring(); quoteTimer_.stop(); refreshGarden();
    }
}

void MainWindow::refreshGarden()
{
    gardenCanvas_->loadRecords(db_.getAllRecords());
    if (historyWidget_) historyWidget_->refreshHistory();
}

void MainWindow::setupTrayIcon()
{
    trayIcon_ = new QSystemTrayIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon), this);
    trayIcon_->setToolTip(QStringLiteral("Forest 专注森林"));
    auto* menu = new QMenu;
    menu->addAction(QStringLiteral("显示主窗口"), this, &QWidget::show);
    menu->addAction(QStringLiteral("退出"), qApp, &QApplication::quit);
    trayIcon_->setContextMenu(menu);
    QObject::connect(trayIcon_, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    trayIcon_->show();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) { show(); raise(); activateWindow(); }
}

void MainWindow::startPresetFocus(uint32_t index)
{
    if (controller_.currentState() != FocusController::State::IDLE) {
        QMessageBox::information(this, QStringLiteral("正在专注中"),
            QStringLiteral("你正在专注中，无法启动新的预设！"));
        return;
    }
    const auto& p = presets_.getPresets()[index];
    auto mode = (p.plannedMinutes == 0)
        ? FocusController::TimerMode::STOPWATCH : FocusController::TimerMode::COUNTDOWN;
    controller_.startFocus(p.plantType, p.plannedMinutes, mode, FocusController::FocusMode::STRICT_MODE);
    monitor_.startMonitoring(); quoteTimer_.start(10000);
    timerRing_->setQuote(quotes_.getRandomQuote());
    updateUI();
}
