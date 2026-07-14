#include "core/AchievementEngine.h"
#include "config/UserPreferences.h"
#include "core/CoinManager.h"
#include "core/DashboardSnapshotService.h"
#include "core/FocusController.h"
#include "core/FocusResultService.h"
#include "core/FriendManager.h"
#include "core/GuardianManager.h"
#include "core/QuoteProvider.h"
#include "core/TagManager.h"
#include "core/UserFeatureServices.h"
#include "storage/DatabaseManager.h"
#include "storage/UserManager.h"
#include "system/RuleEngine.h"
#include "system/SystemMonitor.h"
#include "ui/AppStyle.h"
#include "ui/ChallengeDashboardWidget.h"
#include "ui/ForestDashboardWidget.h"
#include "ui/GuardianDashboardWidget.h"
#include "ui/LoginDialog.h"
#include "ui/MainWindow.h"
#include "ui/DialogPresenter.h"
#include "ui/PlantTimerWidget.h"

#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextStream>
#include <QtTest>

namespace {

QPushButton* buttonByTestId(QObject& root, const QString& testId)
{
    const auto buttons = root.findChildren<QPushButton*>();
    for (QPushButton* button : buttons) {
        if (button->property("testId").toString() == testId) return button;
    }
    return nullptr;
}

} // namespace

class UiSmokeTest final : public QObject {
    Q_OBJECT

private slots:
    void mainWindowFocusAndNavigation();
};

void UiSmokeTest::mainWindowFocusAndNavigation()
{
    qApp->setProperty("forest.uiSmokeAutoClose", true);
    QTemporaryDir sandbox;
    QVERIFY2(sandbox.isValid(), "UI smoke test needs a temporary sandbox");

    UserManager users(QDir(sandbox.path()).filePath("users.dat").toStdString());
    QVERIFY(users.open());
    QVERIFY(users.registerUser("ui-smoke", "test-password"));
    QVERIFY(users.registerUser("ui-smoke-friend", "test-password"));

    const QString userDir = QDir(sandbox.path()).filePath("user_1");
    QVERIFY(QDir().mkpath(userDir));
    UserPreferences::instance().configure(userDir);
    QVERIFY(UserPreferences::instance().load());
    DatabaseManager database(QDir(userDir).filePath("sessions.dat").toStdString());
    QVERIFY(database.open());

    CoinManager coins;
    coins.setDataPath(QDir(userDir).filePath("coins.dat").toStdString());
    QVERIFY(coins.load());
    QVERIFY(coins.earn(1000));

    RuleEngine rules;
    rules.setBlacklist({});
    FocusController controller(database);
    SystemMonitor monitor(rules);
    QuoteProvider quotes;
    AchievementEngine achievements(database, coins);
    UserFeatureServices features(userDir, 1, users, database);
    QVERIFY(features.load());
    const uint32_t smokeTagId = features.tags()->add(QStringLiteral("UI smoke"), QStringLiteral("#4ECDC4"));
    QVERIFY(features.tags()->save());

    FocusResultService results(database, coins, achievements, features.gacha(), features.forestLayout(),
                               features.guardian(), features.challenges(), features.settlements());
    DashboardSnapshotService snapshots(database, coins, features.tags(), features.guardian(),
                                       features.challenges());

    AppStyle::installSmoothInteractions(*qApp);
    qApp->setStyleSheet(AppStyle::styleSheet());

    {
        MainWindow window(controller, monitor, rules, coins, quotes, database, achievements,
                          results, snapshots, features);
        window.resize(1280, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* pageStack = window.findChild<QStackedWidget*>("mainPageStack");
        auto* timer = window.findChild<PlantTimerWidget*>("focusTimer");
        auto* forestDashboard = window.findChild<ForestDashboardWidget*>();
        auto* start = buttonByTestId(window, QStringLiteral("focusStartButton"));
        auto* pause = buttonByTestId(window, QStringLiteral("focusPauseButton"));
        auto* abandon = buttonByTestId(window, QStringLiteral("focusAbandonButton"));
        auto* homeNavigation = window.findChild<QPushButton*>("navHome");
        auto* tagSelector = window.findChild<QComboBox*>("focusTagSelector");
        QVERIFY(pageStack);
        QVERIFY(timer);
        QVERIFY(forestDashboard);
        QVERIFY(start);
        QVERIFY(pause);
        QVERIFY(abandon);
        QVERIFY(homeNavigation);
        QVERIFY(tagSelector);
        QVERIFY(!window.findChild<QPushButton*>("navTags"));
        QCOMPARE(pageStack->currentIndex(), 0);
        QVERIFY(start->isVisible());

        const QString screenshotDirectory = QDir(QCoreApplication::applicationDirPath()).filePath("ui-fullscreen-snapshots");
        QVERIFY(QDir().mkpath(screenshotDirectory));
        QFile manifest(QDir(screenshotDirectory).filePath("manifest.txt"));
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream manifestStream(&manifest);
        manifestStream << "Forest full-screen visual baseline\n";
        manifestStream << "generated=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        const auto checkpoint = [&manifestStream](const QString& step) {
            manifestStream << "checkpoint=" << step << Qt::endl;
        };
        LoginDialog loginDialog(users, &window);
        QVERIFY(loginDialog.windowFlags() & Qt::FramelessWindowHint);
        loginDialog.show();
        QTest::qWait(50);
        const QPixmap loginScreenshot = loginDialog.grab();
        QVERIFY(!loginScreenshot.isNull());
        QVERIFY(loginScreenshot.save(QDir(screenshotDirectory).filePath(QStringLiteral("login-dialog.png"))));
        manifestStream << "login-dialog size=" << loginScreenshot.width() << "x"
                       << loginScreenshot.height() << "\n";
        loginDialog.hide();
        QMessageBox messageBox;
        DialogPresenter::prepare(messageBox);
        QVERIFY(messageBox.windowFlags() & Qt::FramelessWindowHint);
        const auto captureFullscreen = [&window, &screenshotDirectory, &manifestStream](const QString& name) {
            const QPixmap screenshot = window.grab();
            QVERIFY(!screenshot.isNull());
            QVERIFY(screenshot.width() >= 880);
            QVERIFY(screenshot.height() >= 600);
            QVERIFY(screenshot.save(QDir(screenshotDirectory).filePath(name + ".png")));
            manifestStream << name << " size=" << screenshot.width() << "x" << screenshot.height() << "\n";
        };
        const auto isFullyVisible = [](QWidget* widget) {
            return widget && widget->isVisible() && widget->visibleRegion().contains(widget->rect());
        };
        const auto captureAtSize = [&window, &screenshotDirectory, &manifestStream](
                                       const QString& name, const QSize& size) {
            const QPixmap screenshot = window.grab();
            QVERIFY(!screenshot.isNull());
            QCOMPARE(screenshot.size(), size);
            QVERIFY(screenshot.save(QDir(screenshotDirectory).filePath(name + ".png")));
            manifestStream << name << " size=" << screenshot.width() << "x" << screenshot.height() << "\n";
        };
        auto* forestNavigation = window.findChild<QPushButton*>("navForest");
        QVERIFY(forestNavigation);
        const QList<QSize> visualSizes = {
            QSize(1366, 768), QSize(1440, 900), QSize(1920, 1080)
        };
        for (const QSize& size : visualSizes) {
            window.showNormal();
            window.resize(size);
            QTest::qWait(100);
            QCOMPARE(window.size(), size);

            QTest::mouseClick(homeNavigation, Qt::LeftButton);
            QTRY_COMPARE(pageStack->currentIndex(), 0);
            QVERIFY(isFullyVisible(timer));
            QVERIFY(isFullyVisible(start));
            captureAtSize(QStringLiteral("focus-home-%1x%2").arg(size.width()).arg(size.height()), size);

            QTest::mouseClick(forestNavigation, Qt::LeftButton);
            QTRY_COMPARE(pageStack->currentIndex(), 1);
            const QRect forestHeader(0, 0, forestDashboard->width(), qMin(74, forestDashboard->height()));
            QVERIFY(forestDashboard->visibleRegion().contains(forestHeader));
            captureAtSize(QStringLiteral("navForest-%1x%2").arg(size.width()).arg(size.height()), size);
        }
        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 0);
        window.showFullScreen();
        QTest::qWait(100);
        captureFullscreen(QStringLiteral("focus-home"));

        bool homeOverviewOpened = false;
        QTimer::singleShot(0, [&]() {
            auto* overviewDialog = window.findChild<QDialog*>("forestOverviewDialog");
            QVERIFY(overviewDialog);
            QVERIFY(overviewDialog->windowFlags() & Qt::FramelessWindowHint);
            homeOverviewOpened = true;
            overviewDialog->reject();
        });
        timer->sig_tagClicked();
        QTRY_VERIFY(homeOverviewOpened);
        QCOMPARE(pageStack->currentIndex(), 0);

        auto* settingsNavigation = window.findChild<QPushButton*>("navSettings");
        QVERIFY(settingsNavigation);
        QTest::mouseClick(settingsNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 3);
        auto* settingsCategories = window.findChild<QListWidget*>("settingsCategoryNav");
        auto* settingsStack = window.findChild<QStackedWidget*>("settingsCategoryStack");
        QVERIFY(settingsCategories);
        QVERIFY(settingsStack);
        QCOMPARE(settingsCategories->count(), 4);
        for (int row = 0; row < settingsCategories->count(); ++row) {
            QTest::mouseClick(settingsCategories->viewport(), Qt::LeftButton, Qt::NoModifier,
                              settingsCategories->visualItemRect(settingsCategories->item(row)).center());
            QTRY_COMPARE(settingsStack->currentIndex(), row);
            captureFullscreen(QStringLiteral("settings-%1").arg(row));
        }
        const QList<QPair<QString, int>> pageChecks = {
            {QStringLiteral("navForest"), 1}, {QStringLiteral("navShop"), 2},
            {QStringLiteral("navAchievements"), 4}, {QStringLiteral("navGacha"), 5},
            {QStringLiteral("navFriends"), 6}, {QStringLiteral("navChallenges"), 7},
            {QStringLiteral("navGuardian"), 8}
        };
        for (const auto& pageCheck : pageChecks) {
            auto* navigation = window.findChild<QPushButton*>(pageCheck.first);
            QVERIFY(navigation);
            QTest::mouseClick(navigation, Qt::LeftButton);
            QTRY_COMPARE(pageStack->currentIndex(), pageCheck.second);
            captureFullscreen(pageCheck.first);
        }
        QTest::mouseClick(window.findChild<QPushButton*>("navForest"), Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 1);
        bool overviewFilterApplied = false;
        QTimer::singleShot(0, [&]() {
            auto* overviewDialog = window.findChild<QDialog*>("forestOverviewDialog");
            QVERIFY(overviewDialog);
            QVERIFY(overviewDialog->windowFlags() & Qt::FramelessWindowHint);
            auto* overviewApply = buttonByTestId(*overviewDialog, QStringLiteral("forestOverviewApply"));
            auto* overviewAddTag = buttonByTestId(*overviewDialog, QStringLiteral("forestOverviewAddTag"));
            QVERIFY(overviewApply);
            QVERIFY(overviewAddTag);
            QTest::mouseClick(overviewAddTag, Qt::LeftButton);
            auto* overviewAddInput = overviewDialog->findChild<QLineEdit*>("forestOverviewAddInput");
            auto* overviewAddConfirm = buttonByTestId(*overviewDialog, QStringLiteral("forestOverviewAddConfirm"));
            QVERIFY(overviewAddInput);
            QVERIFY(overviewAddConfirm);
            overviewAddInput->setText(QStringLiteral("Popup add"));
            QTest::mouseClick(overviewAddConfirm, Qt::LeftButton);
            bool addedTagVisible = false;
            for (QRadioButton* option : overviewDialog->findChildren<QRadioButton*>()) {
                if (option->text() == QStringLiteral("Popup add")) {
                    addedTagVisible = true;
                    break;
                }
            }
            QVERIFY(addedTagVisible);
            QRadioButton* selectedTag = nullptr;
            for (QRadioButton* option : overviewDialog->findChildren<QRadioButton*>()) {
                if (option->property("tagId").toUInt() == smokeTagId) {
                    selectedTag = option;
                    break;
                }
            }
            QVERIFY(selectedTag);
            QCOMPARE(selectedTag->text(), QStringLiteral("UI smoke"));
            QTest::mouseClick(selectedTag, Qt::LeftButton);
            QVERIFY(selectedTag->isChecked());
            const QPixmap overviewScreenshot = overviewDialog->grab();
            QVERIFY(!overviewScreenshot.isNull());
            QVERIFY(overviewScreenshot.save(
                QDir(screenshotDirectory).filePath(QStringLiteral("forest-overview-filter.png"))));
            manifestStream << "forest-overview-filter size=" << overviewScreenshot.width() << "x"
                           << overviewScreenshot.height() << "\n";
            QTest::mouseClick(overviewApply, Qt::LeftButton);
            overviewFilterApplied = true;
        });
        QTest::mouseClick(forestDashboard, Qt::LeftButton, Qt::NoModifier, QPoint(68, 51));
        QTRY_VERIFY(overviewFilterApplied);
        QTRY_COMPARE(forestDashboard->recordFilter().tagId, smokeTagId);
        forestDashboard->setRecordFilter({});
        const int focusTagIndex = tagSelector->findData(smokeTagId);
        QVERIFY(focusTagIndex >= 0);
        tagSelector->setCurrentIndex(focusTagIndex);
        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 0);
        window.showNormal();
        QTest::qWait(50);

        const QList<QSize> desktopSizes = {
            QSize(1366, 768), QSize(1440, 900), QSize(1920, 1080)
        };
        for (const QSize& size : desktopSizes) {
            window.resize(size);
            QTRY_VERIFY(window.size().width() >= size.width());
            QTRY_VERIFY(timer->isVisible());
            QTRY_VERIFY(start->isVisible());
            const QPixmap screenshot = window.grab();
            QVERIFY(!screenshot.isNull());
            QVERIFY(screenshot.width() >= size.width());
        }
        window.resize(1280, 820);

        QTimer messageBoxCloser;
        messageBoxCloser.setInterval(10);
        QObject::connect(&messageBoxCloser, &QTimer::timeout, []() {
            if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                dialog->accept();
            }
        });
        messageBoxCloser.start();

        auto* shopNavigation = window.findChild<QPushButton*>("navShop");
        QVERIFY(shopNavigation);
        QTest::mouseClick(shopNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 2);
        window.showFullScreen();
        QTest::qWait(100);
        auto* purchase = buttonByTestId(window, QStringLiteral("shopPurchase_1"));
        QVERIFY(purchase && purchase->isEnabled());
        QTest::mouseClick(purchase, Qt::LeftButton);
        QTRY_VERIFY(coins.isPlantUnlocked(1));
        captureFullscreen(QStringLiteral("shop-after-purchase"));
        auto* unaffordable = buttonByTestId(window, QStringLiteral("shopPurchase_3"));
        QVERIFY(unaffordable && unaffordable->isEnabled());
        const uint32_t beforeUnaffordablePurchase = coins.balance();
        QTest::mouseClick(unaffordable, Qt::LeftButton);
        QTRY_COMPARE(coins.balance(), beforeUnaffordablePurchase);

        auto* gachaNavigation = window.findChild<QPushButton*>("navGacha");
        QVERIFY(gachaNavigation);
        QTest::mouseClick(gachaNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 5);
        auto* gachaPull = buttonByTestId(window, QStringLiteral("gachaPullButton"));
        auto* gachaResult = window.findChild<QLabel*>("gachaResultTitle");
        QVERIFY(gachaPull && gachaResult);
        QTest::mouseClick(gachaPull, Qt::LeftButton);
        QTRY_VERIFY(gachaResult->text() != QStringLiteral("准备发掘新的异色树种"));
        captureFullscreen(QStringLiteral("gacha-after-pull"));
        checkpoint(QStringLiteral("gacha-pull-complete"));

        auto* friendsNavigation = window.findChild<QPushButton*>("navFriends");
        QVERIFY(friendsNavigation);
        checkpoint(QStringLiteral("friends-navigation-ready"));
        QTest::mouseClick(friendsNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 6);
        checkpoint(QStringLiteral("friends-page-open"));
        auto* friendPage = window.findChild<QWidget*>("friendsPage");
        auto* friendSearch = friendPage ? friendPage->findChild<QLineEdit*>("friendSearchInput") : nullptr;
        auto* friendList = friendPage ? friendPage->findChild<QListWidget*>("friendUserList") : nullptr;
        auto* sendRequest = friendPage ? buttonByTestId(*friendPage, QStringLiteral("friendSendRequestButton")) : nullptr;
        QVERIFY(friendSearch && friendList && sendRequest);
        friendSearch->setText(QStringLiteral("ui-smoke-friend"));
        QTest::keyClick(friendSearch, Qt::Key_Return);
        QTRY_COMPARE(friendList->count(), 1);
        checkpoint(QStringLiteral("friends-search-complete"));
        QTest::mouseClick(friendList->viewport(), Qt::LeftButton, Qt::NoModifier,
                          friendList->visualItemRect(friendList->item(0)).center());
        checkpoint(QStringLiteral("friends-selection-complete"));
        QTest::mouseClick(sendRequest, Qt::LeftButton);
        QTRY_COMPARE(features.friends()->outgoingRequests().size(), 1);
        checkpoint(QStringLiteral("friends-request-complete"));

        auto* challengeNavigation = window.findChild<QPushButton*>("navChallenges");
        QVERIFY(challengeNavigation);
        QTest::mouseClick(challengeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 7);
        auto* challenge = window.findChild<ChallengeDashboardWidget*>("challengeDashboard");
        QVERIFY(challenge);
        QTest::qWait(50);
        QSignalSpy checkinReward(challenge, &ChallengeDashboardWidget::rewardCoinsRequested);
        const int currentCheckin = (QDate::currentDate().day() - 1) % 5;
        const qreal contentWidth = challenge->width() - 60.0;
        const qreal checkinWidth = contentWidth * 0.34;
        const qreal rewardWidth = (checkinWidth - 52.0 - 40.0) / 5.0;
        const QPoint checkinCenter(static_cast<int>(30.0 + 26.0 +
            currentCheckin * (rewardWidth + 10.0) + rewardWidth / 2.0), 467);
        QTest::mouseClick(challenge, Qt::LeftButton, Qt::NoModifier, checkinCenter);
        QTRY_COMPARE(checkinReward.count(), 1);
        QTest::mouseClick(challenge, Qt::LeftButton, Qt::NoModifier, checkinCenter);
        QTest::qWait(50);
        QCOMPARE(checkinReward.count(), 1);
        captureFullscreen(QStringLiteral("challenge-after-checkin"));
        messageBoxCloser.stop();

        auto* guardianNavigation = window.findChild<QPushButton*>("navGuardian");
        QVERIFY(guardianNavigation);
        QTest::mouseClick(guardianNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 8);
        auto* goalSpin = window.findChild<QSpinBox*>("guardianGoalSpin");
        QVERIFY(goalSpin);
        const int newGoal = goalSpin->value() + 5;
        goalSpin->setValue(newGoal);
        QTRY_COMPARE(static_cast<int>(features.guardian()->dailyGoalMinutes()), newGoal);
        captureFullscreen(QStringLiteral("guardian-after-goal-save"));

        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 0);
        window.showNormal();

        QSignalSpy plantClickSpy(timer, &PlantTimerWidget::sig_plantClicked);
        bool plantDialogHandled = false;
        QTimer::singleShot(0, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            const auto cards = dialog->findChildren<QPushButton*>();
            for (QPushButton* card : cards) {
                if (!card->property("plantCard").toBool()) continue;
                plantDialogHandled = true;
                QTest::mouseClick(card, Qt::LeftButton);
                return;
            }
        });
        QTest::mouseClick(timer, Qt::LeftButton, Qt::NoModifier, timer->rect().center());
        QCOMPARE(plantClickSpy.count(), 1);
        QVERIFY(plantDialogHandled);

        window.activateWindow();
        window.setFocus(Qt::OtherFocusReason);
        QTest::qWait(50);
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::RUNNING));
        QVERIFY(!start->isVisible());
        QVERIFY(pause->isVisible() && pause->isEnabled());
        QVERIFY(abandon->isVisible() && abandon->isEnabled());

        QTest::keyClick(&window, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::PAUSED));
        auto* pauseOverlay = window.findChild<QWidget*>("pauseBreakOverlay");
        auto* continueButton = window.findChild<QPushButton*>("pauseBreakContinue");
        QVERIFY(pauseOverlay && pauseOverlay->isVisible());
        QVERIFY(continueButton && continueButton->isVisible());
        QTest::mouseClick(continueButton, Qt::LeftButton);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::RUNNING));

        bool abandonCancelled = false;
        QTimer::singleShot(0, [&]() {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* cancelButton = dialog->button(QMessageBox::Cancel);
            if (!cancelButton) return;
            abandonCancelled = true;
            QTest::mouseClick(cancelButton, Qt::LeftButton);
        });
        QTest::mouseClick(abandon, Qt::LeftButton);
        QVERIFY(abandonCancelled);
        QCOMPARE(static_cast<int>(controller.currentState()),
                 static_cast<int>(FocusController::State::RUNNING));

        bool abandonConfirmed = false;
        QTimer abandonResultCloser;
        abandonResultCloser.setInterval(1);
        QObject::connect(&abandonResultCloser, &QTimer::timeout, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog && !qobject_cast<QMessageBox*>(dialog)) dialog->reject();
        });
        abandonResultCloser.start();
        QTimer::singleShot(0, [&]() {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* yesButton = dialog->button(QMessageBox::Yes);
            if (!yesButton) return;
            abandonConfirmed = true;
            QTest::mouseClick(yesButton, Qt::LeftButton);
        });
        QTest::mouseClick(abandon, Qt::LeftButton);
        abandonResultCloser.stop();
        QVERIFY(abandonConfirmed);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::FAILED));
        QTRY_VERIFY(start->isVisible() && start->isEnabled());

        const auto records = database.getAllRecords();
        QCOMPARE(records.size(), size_t(1));
        QCOMPARE(records.back().status, FocusRecordStatus::Abandoned);
        QCOMPARE(records.back().tagId, smokeTagId);

        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::RUNNING));
        QTimer modalCloser;
        modalCloser.setInterval(1);
        QObject::connect(&modalCloser, &QTimer::timeout, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (auto* message = qobject_cast<QMessageBox*>(dialog)) {
                message->accept();
            } else {
                dialog->reject();
            }
        });
        modalCloser.start();
        for (int second = 0; second < 25 * 60 &&
             controller.currentState() == FocusController::State::RUNNING; ++second) {
            controller.tick();
        }
        modalCloser.stop();
        QCOMPARE(static_cast<int>(controller.currentState()),
                 static_cast<int>(FocusController::State::SUCCESS));
        QTRY_VERIFY(start->isVisible() && start->isEnabled());

        const auto completedRecords = database.getAllRecords();
        QCOMPARE(completedRecords.size(), size_t(2));
        QCOMPARE(completedRecords.back().status, FocusRecordStatus::Success);
        QCOMPARE(completedRecords.back().tagId, smokeTagId);
    }

    QVERIFY(features.save());
    database.close();
    users.close();
}

QTEST_MAIN(UiSmokeTest)
#include "UiSmokeTest.moc"
