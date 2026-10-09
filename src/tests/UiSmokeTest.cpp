#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/AchievementEngine.h"
#include "config/UserPreferences.h"
#include "config/PlantCatalog.h"
#include "common/PathConfig.h"
#include "core/CoinManager.h"
#include "core/DashboardSnapshotService.h"
#include "core/FocusController.h"
#include "tests/TestFocusClock.h"
#include "core/FocusResultService.h"
#include "core/FriendManager.h"
#include "core/GachaManager.h"
#include "core/GuardianManager.h"
#include "core/QuoteProvider.h"
#include "core/TagManager.h"
#include "core/UserFeatureServices.h"
#include "storage/DatabaseManager.h"
#include "storage/UserManager.h"
#include "storage/StorageEnvelope.h"
#include "system/RuleEngine.h"
#include "system/SystemMonitor.h"
#include "ui/AppStyle.h"
#include "ui/ChallengeDashboardWidget.h"
#include "ui/DashboardPageHost.h"
#include "ui/ForestDashboardWidget.h"
#include "ui/GuardianDashboardWidget.h"
#include "ui/LoginDialog.h"
#include "ui/MainWindow.h"
#include "ui/DialogPresenter.h"
#include "ui/PlantTimerWidget.h"
#include "utils/Logger.h"
#include "BaselineFixture.h"
#include "BuildProvenance.h"

#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QImage>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QAccessible>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QRadioButton>
#include <QScreen>
#include <QScopeGuard>
#include <QShortcut>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QMouseEvent>
#include <QTextStream>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

namespace {

QPushButton* buttonByTestId(QObject& root, const QString& testId)
{
    const auto buttons = root.findChildren<QPushButton*>();
    for (QPushButton* button : buttons) {
        if (button->property("testId").toString() == testId) return button;
    }
    return nullptr;
}

bool foregroundHandleBelongsToExpectedWindow(WId foregroundHandle, const QWidget& expectedWindow)
{
    if (!foregroundHandle || !expectedWindow.isVisible()) return false;
    const QWidget* expectedTopLevel = expectedWindow.window();
#ifdef Q_OS_WIN
    const HWND expectedHandle = reinterpret_cast<HWND>(expectedTopLevel->winId());
    const HWND foregroundNative = reinterpret_cast<HWND>(foregroundHandle);
    DWORD foregroundProcessId = 0;
    DWORD expectedProcessId = 0;
    if (GetWindowThreadProcessId(foregroundNative, &foregroundProcessId) == 0 ||
        GetWindowThreadProcessId(expectedHandle, &expectedProcessId) == 0 ||
        foregroundProcessId != GetCurrentProcessId() || foregroundProcessId != expectedProcessId) {
        return false;
    }
    const HWND foregroundRoot = GetAncestor(foregroundNative, GA_ROOT);
    return foregroundRoot && foregroundRoot == GetAncestor(expectedHandle, GA_ROOT);
#else
    const QWidget* foregroundWidget = QWidget::find(foregroundHandle);
    return foregroundWidget && foregroundWidget->window() == expectedTopLevel;
#endif
}

bool isExpectedWindowForeground(const QWidget& expectedWindow)
{
#ifdef Q_OS_WIN
    const HWND foregroundHandle = GetForegroundWindow();
    const bool matches = foregroundHandle && foregroundHandleBelongsToExpectedWindow(
        reinterpret_cast<WId>(foregroundHandle), expectedWindow);
    return matches;
#else
    const QWidget* foregroundWidget = QApplication::activeWindow();
    return foregroundWidget && foregroundWidget->isActiveWindow() &&
           foregroundHandleBelongsToExpectedWindow(foregroundWidget->winId(), expectedWindow);
#endif
}

void activateTestWindow(QWidget& window)
{
    window.raise();
    window.activateWindow();
#ifdef Q_OS_WIN
    // Windows may deny activation to a test launched by a background CTest process.
    const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD testThread = GetCurrentThreadId();
    const bool attached = foregroundThread != 0 && foregroundThread != testThread &&
                          AttachThreadInput(testThread, foregroundThread, TRUE);
    SetForegroundWindow(reinterpret_cast<HWND>(window.winId()));
    if (attached) AttachThreadInput(testThread, foregroundThread, FALSE);
#endif
}

void writeManifestRecord(QTextStream& stream, const QJsonObject& record)
{
    stream << QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact)) << QLatin1Char('\n');
}

bool shouldCaptureScreenshot(const QString& name)
{
    if (qEnvironmentVariable("FOREST_UI_SAMPLE_CAPTURE") == QStringLiteral("none")) return false;
    if (qEnvironmentVariable("FOREST_UI_SAMPLE_CAPTURE") == QStringLiteral("batch6")) {
        return name != QStringLiteral("login-dialog");
    }
    if (qEnvironmentVariable("FOREST_UI_SAMPLE_CAPTURE") == QStringLiteral("batch5")) {
        return name.startsWith(QStringLiteral("friends-")) || name.startsWith(QStringLiteral("forest-tags-")) ||
               name.startsWith(QStringLiteral("forest-tag-")) || name.startsWith(QStringLiteral("plant-selector-")) ||
               name == QStringLiteral("navFriends") || name == QStringLiteral("accessible-navFriends") ||
               name == QStringLiteral("keyboard-project-tags") || name == QStringLiteral("keyboard-plant-selector") ||
               name == QStringLiteral("forest-overview-filter") ||
               name == QStringLiteral("accessible-dialog-max-font");
    }
    if (qEnvironmentVariable("FOREST_UI_SAMPLE_CAPTURE") == QStringLiteral("batch4")) {
        return name.startsWith(QStringLiteral("challenge-")) || name.startsWith(QStringLiteral("guardian-")) ||
               name == QStringLiteral("navChallenges") || name == QStringLiteral("navGuardian") ||
               name == QStringLiteral("accessible-navChallenges") || name == QStringLiteral("accessible-navGuardian");
    }
    if (qEnvironmentVariable("FOREST_UI_SAMPLE_CAPTURE") == QStringLiteral("batch3")) {
        return name.startsWith(QStringLiteral("achievements-")) ||
               name.startsWith(QStringLiteral("gacha-")) || name == QStringLiteral("navAchievements") ||
               name == QStringLiteral("navGacha") || name == QStringLiteral("accessible-navAchievements") ||
               name == QStringLiteral("accessible-navGacha");
    }
    if (qEnvironmentVariable("FOREST_UI_SAMPLE_CAPTURE") == QStringLiteral("variant-assets")) {
        return name.startsWith(QStringLiteral("gacha-")) || name == QStringLiteral("navGacha") ||
               name == QStringLiteral("accessible-navGacha");
    }
    if (qEnvironmentVariable("FOREST_UI_SAMPLE_CAPTURE") == QStringLiteral("batch2")) {
        return name.startsWith(QStringLiteral("settings-")) ||
               name.startsWith(QStringLiteral("shop-")) ||
               name.startsWith(QStringLiteral("forest-")) ||
               name == QStringLiteral("navForest") || name == QStringLiteral("navShop") ||
               name == QStringLiteral("accessible-navForest") ||
               name == QStringLiteral("accessible-navShop") ||
               name == QStringLiteral("keyboard-forest-month") ||
               name == QStringLiteral("accessible-settings-max-font");
    }
    if (!qEnvironmentVariableIsSet("FOREST_UI_SAMPLE_CAPTURE")) return true;
    return name.startsWith(QStringLiteral("focus-")) || name == QStringLiteral("settings-0") ||
           name == QStringLiteral("accessible-focus-home") ||
           name == QStringLiteral("accessible-focus-settings") ||
           name == QStringLiteral("keyboard-timer-focus") || name == QStringLiteral("navShop");
}

void writeScreenshotManifest(QTextStream& stream, const QString& name, const QWidget& window,
                             const QPixmap& image, bool foregroundValidated = false)
{
    const QScreen* screen = window.screen();
    const QRect geometry = screen ? screen->geometry() : QRect();
    QJsonObject screenRecord;
    screenRecord.insert(QStringLiteral("name"), screen ? screen->name() : QString());
    screenRecord.insert(QStringLiteral("geometry"), QJsonObject{
        {QStringLiteral("x"), geometry.x()}, {QStringLiteral("y"), geometry.y()},
        {QStringLiteral("width"), geometry.width()}, {QStringLiteral("height"), geometry.height()}
    });
    screenRecord.insert(QStringLiteral("logicalDpi"), QJsonObject{
        {QStringLiteral("x"), screen ? screen->logicalDotsPerInchX() : 0.0},
        {QStringLiteral("y"), screen ? screen->logicalDotsPerInchY() : 0.0}
    });
    screenRecord.insert(QStringLiteral("physicalDpi"), QJsonObject{
        {QStringLiteral("x"), screen ? screen->physicalDotsPerInchX() : 0.0},
        {QStringLiteral("y"), screen ? screen->physicalDotsPerInchY() : 0.0}
    });
    screenRecord.insert(QStringLiteral("devicePixelRatio"),
                        screen ? screen->devicePixelRatio() : window.devicePixelRatioF());

    QJsonObject record;
    record.insert(QStringLiteral("type"), QStringLiteral("screenshot"));
    record.insert(QStringLiteral("name"), name);
    record.insert(QStringLiteral("fixtureId"), QStringLiteral("ui-smoke-v1"));
    record.insert(QStringLiteral("captureKind"), window.isFullScreen()
        ? QStringLiteral("full-screen-window")
        : qobject_cast<const QDialog*>(&window) ? QStringLiteral("dialog-crop")
                                               : QStringLiteral("windowed-size-check"));
    record.insert(QStringLiteral("capturedUtc"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    record.insert(QStringLiteral("windowFullScreen"), window.isFullScreen());
    record.insert(QStringLiteral("windowSize"), QJsonObject{
        {QStringLiteral("width"), window.width()}, {QStringLiteral("height"), window.height()}
    });
    record.insert(QStringLiteral("imageSize"), QJsonObject{
        {QStringLiteral("width"), image.width()}, {QStringLiteral("height"), image.height()}
    });
    record.insert(QStringLiteral("foregroundValidated"), foregroundValidated);
    record.insert(QStringLiteral("screen"), screenRecord);
    writeManifestRecord(stream, record);
}

} // namespace

class UiSmokeTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void foregroundWindowPredicate();
    void mainWindowFocusAndNavigation();
    void baselineFixture();
    void terminalStatisticsFullscreen();
    void forestIslandPaginationFullscreen();

private:
    QString smokeUserDirectory() const;

    QTemporaryDir smokeSandbox_;
    QString manifestPath_;
};

void UiSmokeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(smokeSandbox_.isValid());
    qApp->setProperty("forest.testDataRoot", smokeSandbox_.path());
    QCOMPARE(PathConfig::getAppDataDir(), QDir(smokeSandbox_.path()).absolutePath());
}

void UiSmokeTest::foregroundWindowPredicate()
{
    QWidget expectedWindow;
    QWidget otherTopLevel;

    expectedWindow.show();
    activateTestWindow(expectedWindow);
    QVERIFY(QTest::qWaitForWindowExposed(&expectedWindow));
    QTRY_VERIFY(isExpectedWindowForeground(expectedWindow));
    QVERIFY(foregroundHandleBelongsToExpectedWindow(expectedWindow.winId(), expectedWindow));

    otherTopLevel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&otherTopLevel));
    activateTestWindow(otherTopLevel);
    QTRY_VERIFY(isExpectedWindowForeground(otherTopLevel));
    QVERIFY(!foregroundHandleBelongsToExpectedWindow(otherTopLevel.winId(), expectedWindow));
}

QString UiSmokeTest::smokeUserDirectory() const
{
    return QDir(smokeSandbox_.path()).filePath(QStringLiteral("user_1"));
}

void UiSmokeTest::mainWindowFocusAndNavigation()
{
    qApp->setProperty("forest.uiSmokeAutoClose", true);
    QVERIFY2(smokeSandbox_.isValid(), "UI smoke test needs a temporary sandbox");

    UserManager users(QDir(smokeSandbox_.path()).filePath("users.dat").toStdString());
    QVERIFY(users.open());
    QVERIFY(users.registerUser("ui-smoke", "test-password"));
    QVERIFY(users.registerUser("ui-smoke-friend", "test-password"));

    const QString userDir = smokeUserDirectory();
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
    QVERIFY(UserPreferences::instance().setProcessBlacklist({}));
    rules.setBlacklist({});
    TestFocusClock focusClock;
    FocusController controller(database, focusClock.clock());
    SystemMonitor monitor(rules);
    QuoteProvider quotes;
    AchievementEngine achievements(database, coins);
    UserFeatureServices features(userDir, 1, users, database);
    QVERIFY(features.load());
    QString backupSaveError;
    QVERIFY(features.saveForBackup(controller, coins, &backupSaveError));
    StorageEnvelope::failNextWriteForTesting(QDir(userDir).filePath("coins.dat"));
    QVERIFY(!features.saveForBackup(controller, coins, &backupSaveError));
    QVERIFY(!backupSaveError.isEmpty());
    StorageEnvelope::clearWriteFailuresForTesting();
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
        window.resize(QSize(1280, 820).boundedTo(window.screen()->availableGeometry().size() - QSize(20, 44)));
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

        const QString configuredScreenshots = qEnvironmentVariable("FOREST_UI_SCREENSHOT_DIR");
        const QString screenshotDirectory = configuredScreenshots.isEmpty()
            ? QDir(QCoreApplication::applicationDirPath()).filePath("ui-fullscreen-snapshots")
            : configuredScreenshots;
        QVERIFY(QDir().mkpath(screenshotDirectory));
        manifestPath_ = QDir(screenshotDirectory).filePath(QStringLiteral("manifest.txt"));
        QFile manifest(manifestPath_);
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream manifestStream(&manifest);
        QJsonObject buildRecord;
        buildRecord.insert(QStringLiteral("sourceDirectory"), QString::fromUtf8(ForestBuild::sourceDirectory));
        buildRecord.insert(QStringLiteral("revision"), QString::fromUtf8(ForestBuild::revision));
        buildRecord.insert(QStringLiteral("sourceFingerprint"),
                           QString::fromUtf8(ForestBuild::sourceFingerprint));
        buildRecord.insert(QStringLiteral("configuredUtc"), QString::fromUtf8(ForestBuild::configuredUtc));
        buildRecord.insert(QStringLiteral("appVersion"), QString::fromUtf8(ForestBuild::appVersion));
        buildRecord.insert(QStringLiteral("qtVersion"), QString::fromUtf8(ForestBuild::qtVersion));
        buildRecord.insert(QStringLiteral("compiler"), QString::fromUtf8(ForestBuild::compiler));
        buildRecord.insert(QStringLiteral("buildConfiguration"),
                           QString::fromUtf8(ForestBuild::buildConfiguration));
        QJsonObject runRecord;
        runRecord.insert(QStringLiteral("type"), QStringLiteral("run"));
        runRecord.insert(QStringLiteral("format"), QStringLiteral("forest-ui-smoke-manifest-v2"));
        runRecord.insert(QStringLiteral("runUtc"),
                         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        runRecord.insert(QStringLiteral("build"), buildRecord);
        runRecord.insert(QStringLiteral("screenshotFixtureId"), QStringLiteral("ui-smoke-v1"));
        runRecord.insert(QStringLiteral("qtScaleFactor"), qEnvironmentVariable("QT_SCALE_FACTOR"));
        runRecord.insert(QStringLiteral("captureScope"), qEnvironmentVariable("FOREST_UI_SAMPLE_CAPTURE"));
        runRecord.insert(QStringLiteral("standardPathsTestMode"), QStandardPaths::isTestModeEnabled());
        writeManifestRecord(manifestStream, runRecord);
        LoginDialog loginDialog(users, &window);
        QVERIFY(loginDialog.windowFlags() & Qt::FramelessWindowHint);
        loginDialog.show();
        QTest::qWait(50);
        if (shouldCaptureScreenshot(QStringLiteral("login-dialog"))) {
            const QPixmap loginScreenshot = loginDialog.grab();
            QVERIFY(!loginScreenshot.isNull());
            QVERIFY(loginScreenshot.save(QDir(screenshotDirectory).filePath(QStringLiteral("login-dialog.png"))));
            writeScreenshotManifest(manifestStream, QStringLiteral("login-dialog"), loginDialog, loginScreenshot);
        }
        loginDialog.hide();
        QMessageBox messageBox;
        DialogPresenter::prepare(messageBox);
        QVERIFY(messageBox.windowFlags() & Qt::FramelessWindowHint);
        const auto captureFullscreen = [&window, &screenshotDirectory, &manifestStream](const QString& name) {
            QTest::qWait(50);
            if (!shouldCaptureScreenshot(name)) return;
            QVERIFY(window.isFullScreen());
            QVERIFY(window.screen());
            QCOMPARE(window.size(), window.screen()->geometry().size());
            QVERIFY2(isExpectedWindowForeground(window),
                     "The application must be foreground before desktop capture");
            const QPixmap screenshot = window.screen()->grabWindow(0);
            QVERIFY(!screenshot.isNull());
            QVERIFY2(isExpectedWindowForeground(window),
                     "The application lost foreground during desktop capture");
            QVERIFY(screenshot.width() >= 880);
            QVERIFY(screenshot.height() >= 600);
            const QSize logicalSize = window.screen()->geometry().size();
            const qreal dpr = window.screen()->devicePixelRatio();
            QVERIFY(std::abs(screenshot.width() - logicalSize.width() * dpr) <= 1);
            QVERIFY(std::abs(screenshot.height() - logicalSize.height() * dpr) <= 1);
            QVERIFY(screenshot.save(QDir(screenshotDirectory).filePath(name + ".png")));
            writeScreenshotManifest(manifestStream, name, window, screenshot, true);
        };
        const auto isFullyVisible = [](QWidget* widget) {
            return widget && widget->isVisible() && widget->visibleRegion().contains(widget->rect());
        };
        const auto labelTextFits = [](QLabel* label) {
            if (!label || label->contentsRect().width() <= 0) return false;
            const QRect required = QFontMetrics(label->font()).boundingRect(
                QRect(0, 0, label->contentsRect().width(), 1000), Qt::TextWordWrap, label->text());
            return required.height() <= label->contentsRect().height();
        };
        const auto verifyAccessibleButton = [&](QPushButton* button, QWidget* bounds) {
            QVERIFY(button && bounds);
            QVERIFY(isFullyVisible(button));
            const QRect buttonBounds(button->mapTo(bounds, QPoint()), button->size());
            QVERIFY(bounds->rect().contains(buttonBounds));
            QVERIFY(QFontMetrics(button->font()).horizontalAdvance(button->text()) <=
                    button->contentsRect().width());
            button->setFocus(Qt::OtherFocusReason);
            QTRY_VERIFY(button->hasFocus());
            auto* accessible = QAccessible::queryAccessibleInterface(button);
            QVERIFY(accessible && accessible->role() == QAccessible::Button);
            QVERIFY(!accessible->text(QAccessible::Name).trimmed().isEmpty());
        };
        int standardGachaDigWidth = 0;
        const auto verifyVariantRows = [&](QWidget* variants, int columns) {
            QVERIFY(variants);
            const auto rows = variants->findChildren<QWidget*>("variantRow", Qt::FindDirectChildrenOnly);
            QCOMPARE(rows.size(), (12 + columns - 1) / columns);
            QRect previousRow;
            int cardHeight = -1;
            int cardCount = 0;
            for (QWidget* row : rows) {
                if (!previousRow.isNull()) QVERIFY(!previousRow.intersects(row->geometry()));
                previousRow = row->geometry();
                const auto cards = row->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
                QCOMPARE(cards.size(), columns);
                QRect previousCard;
                for (QFrame* card : cards) {
                    QVERIFY(card->width() >= 200 && card->height() >= 260);
                    if (cardHeight < 0) cardHeight = card->height();
                    else QCOMPARE(card->height(), cardHeight);
                    if (!previousCard.isNull()) QVERIFY(!previousCard.intersects(card->geometry()));
                    previousCard = card->geometry();
                    for (QLabel* label : card->findChildren<QLabel*>()) {
                        if (label->property("variantName").toBool() ||
                            label->objectName() == QStringLiteral("variantState") ||
                            label->objectName() == QStringLiteral("variantDescription")) {
                            QVERIFY(labelTextFits(label));
                        }
                    }
                    ++cardCount;
                }
            }
            QCOMPARE(cardCount, 12);
        };
        const auto verifyHomeLayout = [&]() {
            timer->repaint();
            auto* mode = window.findChild<QWidget*>("focusModeSwitch");
            auto* balance = window.findChild<QLabel*>("focusCoinBalance");
            auto* hint = window.findChild<QLabel*>("focusHint");
            QVERIFY(mode && balance && hint);
            QVERIFY(mode->height() <= 60);
            QCOMPARE(start->size(), QSize(256, 52));
            const QRect timerBounds(timer->mapTo(&window, QPoint()), timer->size());
            for (QWidget* control : {mode, static_cast<QWidget*>(balance),
                                    static_cast<QWidget*>(hint), static_cast<QWidget*>(start)}) {
                QVERIFY(isFullyVisible(control));
                QVERIFY(!timerBounds.intersects(QRect(control->mapTo(&window, QPoint()), control->size())));
            }
            const QRectF ring = timer->ringBounds();
            const QRectF tag = timer->tagBounds();
            const QRectF time = timer->timeBounds();
            QVERIFY(QRectF(timer->rect()).contains(ring));
            QVERIFY(QRectF(timer->rect()).contains(tag));
            QVERIFY(QRectF(timer->rect()).contains(time));
            QVERIFY(tag.top() - ring.bottom() >= 47);
            QVERIFY(time.top() - tag.bottom() >= 23);
            auto* countdown = window.findChild<QPushButton*>("focusCountdownMode");
            auto* stopwatch = window.findChild<QPushButton*>("focusStopwatchMode");
            QVERIFY(countdown && stopwatch);
            for (auto* modeButton : {countdown, stopwatch}) {
                QVERIFY(isFullyVisible(modeButton));
                QVERIFY(!modeButton->icon().pixmap(QSize(20, 20)).isNull());
                QVERIFY(!modeButton->graphicsEffect());
            }
        };
        const auto captureModalFullscreen = [&](const QString& name) {
            if (!shouldCaptureScreenshot(name)) return;
            auto* modal = QApplication::activeModalWidget();
            QVERIFY(modal && modal->isVisible());
            QVERIFY(QTest::qWaitForWindowExposed(modal));
            modal->raise();
            QTest::qWait(200);
            QVERIFY(window.isFullScreen());
            QCOMPARE(window.size(), window.screen()->geometry().size());
            QVERIFY2(isExpectedWindowForeground(*modal),
                     "The modal must be foreground before desktop capture");
            const QPixmap image = window.screen()->grabWindow(0);
            QVERIFY(!image.isNull());
            QVERIFY2(isExpectedWindowForeground(*modal),
                     "The modal lost foreground during desktop capture");
            QVERIFY(image.save(QDir(screenshotDirectory).filePath(name + ".png")));
            writeScreenshotManifest(manifestStream, name, window, image, true);
        };
        const auto captureAtSize = [&window, &screenshotDirectory, &manifestStream](
                                       const QString& name, const QSize& size) {
            if (qEnvironmentVariableIsSet("FOREST_UI_SAMPLE_CAPTURE")) return;
            QVERIFY(!window.isFullScreen());
            const QPixmap screenshot = window.grab();
            QVERIFY(!screenshot.isNull());
            const qreal ratio = screenshot.devicePixelRatio();
            QCOMPARE(screenshot.size(), QSize(qRound(size.width() * ratio), qRound(size.height() * ratio)));
            QVERIFY(screenshot.save(QDir(screenshotDirectory).filePath(name + ".png")));
            writeScreenshotManifest(manifestStream, name, window, screenshot);
        };
        auto* forestNavigation = window.findChild<QPushButton*>("navForest");
        QVERIFY(forestNavigation);
        const QList<QSize> visualSizes = {
            QSize(1366, 768), QSize(1440, 900), QSize(1920, 1080)
        };
        for (const QSize& size : visualSizes) {
            const QSize available = window.screen()->availableGeometry().size() - QSize(20, 44);
            if (size.width() > available.width() || size.height() > available.height()) continue;
            window.showNormal();
            window.resize(size);
            QTest::qWait(100);
            QCOMPARE(window.size(), size);

            QTest::mouseClick(homeNavigation, Qt::LeftButton);
            QTRY_COMPARE(pageStack->currentIndex(), 0);
            QVERIFY(isFullyVisible(timer));
            QVERIFY(isFullyVisible(start));
            verifyHomeLayout();
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
        activateTestWindow(window);
        QTest::qWait(100);
        captureFullscreen(QStringLiteral("focus-home"));
        verifyHomeLayout();

        auto* countdownMode = window.findChild<QPushButton*>("focusCountdownMode");
        auto* stopwatchMode = window.findChild<QPushButton*>("focusStopwatchMode");
        auto* modeOverlay = window.findChild<QWidget*>("homeModeOverlay");
        auto* modeSettings = window.findChild<QComboBox*>("focusModeSelector");
        QVERIFY(countdownMode && stopwatchMode && modeOverlay && modeSettings);
        QTest::mouseClick(stopwatchMode, Qt::LeftButton);
        QCOMPARE(modeSettings->currentData().toInt(), 1);
        QVERIFY(!timer->timeSelectorAvailable());
        captureFullscreen(QStringLiteral("focus-stopwatch-options"));
        QTest::mouseClick(modeOverlay, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QVERIFY(!modeOverlay->isVisible());
        QTest::mouseClick(countdownMode, Qt::LeftButton);
        QCOMPARE(modeSettings->currentData().toInt(), 0);
        QVERIFY(timer->timeSelectorAvailable());
        captureFullscreen(QStringLiteral("focus-countdown-options"));
        QTest::mouseClick(modeOverlay, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QVERIFY(!modeOverlay->isVisible());

        auto* challengeForReadTest = window.findChild<ChallengeDashboardWidget*>();
        auto* challengeNavForReadTest = window.findChild<QPushButton*>("navChallenges");
        QVERIFY(challengeForReadTest);
        QVERIFY(challengeNavForReadTest);
        database.close();
        int readErrorDialogs = 0;
        const auto closeReadError = [&](const QString& name) {
            QTimer::singleShot(0, [&, name]() {
                auto* error = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                QVERIFY(error);
                QVERIFY(error->windowFlags() & Qt::FramelessWindowHint);
                ++readErrorDialogs;
                captureModalFullscreen(name);
                error->accept();
            });
        };
        closeReadError(QStringLiteral("forest-read-error"));
        QTest::mouseClick(forestNavigation, Qt::LeftButton);
        QVERIFY(!forestDashboard->isEnabled());
        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTest::mouseClick(forestNavigation, Qt::LeftButton);
        QCOMPARE(readErrorDialogs, 1);
        closeReadError(QStringLiteral("challenge-read-error"));
        QTest::mouseClick(challengeNavForReadTest, Qt::LeftButton);
        QVERIFY(!challengeForReadTest->isEnabled());
        QCOMPARE(readErrorDialogs, 2);
        QVERIFY2(database.open(), qPrintable(database.lastError()));
        QTest::mouseClick(forestNavigation, Qt::LeftButton);
        QVERIFY(forestDashboard->isEnabled());
        QTest::mouseClick(challengeNavForReadTest, Qt::LeftButton);
        QVERIFY(challengeForReadTest->isEnabled());
        QTest::mouseClick(homeNavigation, Qt::LeftButton);

        bool homeOverviewOpened = false;
        QTimer::singleShot(0, [&]() {
            auto* overviewDialog = window.findChild<QDialog*>("forestOverviewDialog");
            QVERIFY(overviewDialog);
            QVERIFY(overviewDialog->windowFlags() & Qt::FramelessWindowHint);
            homeOverviewOpened = true;
            captureModalFullscreen(QStringLiteral("forest-tags-home"));
            QTest::keyClick(overviewDialog, Qt::Key_Escape);
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
        QCOMPARE(settingsCategories->size(), QSize(196, 284));
        const QRect settingsNavGeometry = settingsCategories->geometry();
        for (int row = 0; row < settingsCategories->count(); ++row) {
            QTest::mouseClick(settingsCategories->viewport(), Qt::LeftButton, Qt::NoModifier,
                              settingsCategories->visualItemRect(settingsCategories->item(row)).center());
            QTRY_COMPARE(settingsStack->currentIndex(), row);
            auto* section = qobject_cast<QScrollArea*>(settingsStack->currentWidget());
            QVERIFY(section);
            QCOMPARE(section->horizontalScrollBar()->maximum(), 0);
            QVERIFY(section->widget()->width() <= 920);
            QCOMPARE(settingsCategories->geometry(), settingsNavGeometry);
            for (auto* control : section->widget()->findChildren<QComboBox*>()) {
                QCOMPARE(control->width(), 240);
                QVERIFY(control->isVisible());
            }
            if (row == 3) {
                auto* backup = buttonByTestId(window, QStringLiteral("settingsCreateBackup"));
                auto* restore = buttonByTestId(window, QStringLiteral("settingsRestoreBackup"));
                auto* exportRecords = buttonByTestId(window, QStringLiteral("settingsExportRecords"));
                QVERIFY(backup && restore && exportRecords);
                QVERIFY(backup->isVisible() && restore->isVisible() && exportRecords->isVisible());
                QCOMPARE(backup->objectName(), QStringLiteral("btnPrimary"));
                QCOMPARE(restore->objectName(), QStringLiteral("btnDanger"));
                QCOMPARE(exportRecords->objectName(), QStringLiteral("btnSecondary"));
                QVERIFY(restore->mapTo(section->widget(), QPoint()).y() >
                        exportRecords->mapTo(section->widget(), QPoint()).y() + exportRecords->height());
            }
            captureFullscreen(QStringLiteral("settings-%1").arg(row));
        }
        settingsCategories->setCurrentRow(0);
        settingsCategories->setFocus();
        QTest::keyClick(settingsCategories, Qt::Key_Down);
        QTRY_COMPARE(settingsStack->currentIndex(), 1);
        settingsCategories->setCurrentRow(3);
        auto* logStatus = window.findChild<QLabel*>("diagnosticLogStatus");
        QVERIFY(logStatus);
        auto closeTestLogger = qScopeGuard([] { Logger::instance().close(); });
        QTRY_VERIFY(logStatus->text().contains(QStringLiteral("未开启")));
        const QString diagnosticPath = QDir(smokeSandbox_.path()).filePath("ui-diagnostics.log");
        Logger::instance().setLogPath(diagnosticPath.toStdString());
        Logger::instance().setSessionId("ui-log-test");
        QVERIFY(Logger::instance().open());
        QTRY_VERIFY(logStatus->text().contains(QStringLiteral("正常")));
        captureFullscreen(QStringLiteral("settings-log-writing"));
        Logger::instance().setLogPath(QDir(smokeSandbox_.path()).filePath("uncreated-log-parent/ui.log").toStdString());
        QVERIFY(!Logger::instance().open());
        QTRY_VERIFY(logStatus->text().contains(QStringLiteral("停写")));
        QVERIFY(!QApplication::activeModalWidget());
        captureFullscreen(QStringLiteral("settings-log-failed"));
        Logger::instance().setLogPath(diagnosticPath.toStdString());
        QVERIFY(Logger::instance().open());
        QTRY_VERIFY(logStatus->text().contains(QStringLiteral("正常")));
        Logger::instance().close();
        QTRY_VERIFY(logStatus->text().contains(QStringLiteral("未开启")));
        settingsCategories->setCurrentRow(1);
        QTRY_COMPARE(settingsStack->currentIndex(), 1);
        auto* blacklist = window.findChild<QListWidget*>("processBlacklist");
        auto* blacklistInput = window.findChild<QLineEdit*>("processBlacklistInput");
        auto* addProcess = window.findChild<QPushButton*>("processBlacklistAdd");
        auto* removeProcess = window.findChild<QPushButton*>("processBlacklistRemove");
        QVERIFY(blacklist);
        QCOMPARE(blacklist->height(), 224);
        QVERIFY(blacklistInput);
        QVERIFY(addProcess);
        QVERIFY(removeProcess);
        const int originalProcessCount = blacklist->count();
        blacklistInput->setText(QStringLiteral(" Stage5.EXE "));
        QTest::mouseClick(addProcess, Qt::LeftButton);
        QCOMPARE(blacklist->count(), originalProcessCount + 1);
        QVERIFY(rules.isViolation(QStringLiteral("STAGE5.exe")));
        QVERIFY(UserPreferences::instance().processBlacklist().contains(QStringLiteral("stage5.exe")));
        blacklistInput->setText(QStringLiteral("stage5.exe"));
        QTest::mouseClick(addProcess, Qt::LeftButton);
        QCOMPARE(blacklist->count(), originalProcessCount + 1);
        captureFullscreen(QStringLiteral("settings-blacklist-saved"));

        bool invalidProcessReported = false;
        QTimer::singleShot(0, [&]() {
            if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                invalidProcessReported = dialog->windowTitle() == QStringLiteral("规则未保存") &&
                                         bool(dialog->windowFlags() & Qt::FramelessWindowHint);
                captureModalFullscreen(QStringLiteral("settings-blacklist-invalid"));
                dialog->accept();
            }
        });
        blacklistInput->setText(QStringLiteral("C:/invalid.exe"));
        QTest::mouseClick(addProcess, Qt::LeftButton);
        QVERIFY(invalidProcessReported);
        QCOMPARE(blacklist->count(), originalProcessCount + 1);
        const auto processItems = blacklist->findItems(QStringLiteral("stage5.exe"), Qt::MatchExactly);
        QCOMPARE(processItems.size(), 1);
        blacklist->setCurrentItem(processItems.front());
        QTest::mouseClick(removeProcess, Qt::LeftButton);
        QCOMPARE(blacklist->count(), originalProcessCount);
        QVERIFY(!rules.isViolation(QStringLiteral("stage5.exe")));
        QVERIFY(!UserPreferences::instance().processBlacklist().contains(QStringLiteral("stage5.exe")));
        captureFullscreen(QStringLiteral("settings-blacklist-removed"));
        QVERIFY(UserPreferences::instance().setProcessBlacklist({}));
        rules.setBlacklist({});

        const QList<QPair<QString, int>> pageChecks = {
            {QStringLiteral("navForest"), 1}, {QStringLiteral("navShop"), 2},
            {QStringLiteral("navAchievements"), 4}, {QStringLiteral("navGacha"), 5},
            {QStringLiteral("navFriends"), 6}, {QStringLiteral("navChallenges"), 7},
            {QStringLiteral("navGuardian"), 8}
        };
        for (const auto& pageCheck : pageChecks) {
            auto* navigation = window.findChild<QPushButton*>(pageCheck.first);
            QVERIFY(navigation);
            verifyAccessibleButton(navigation, &window);
            QTest::keyClick(navigation, Qt::Key_Space);
            QTRY_COMPARE(pageStack->currentIndex(), pageCheck.second);
            captureFullscreen(pageCheck.first);
            if (pageCheck.first == QStringLiteral("navForest")) {
                auto* scroll = window.findChild<QScrollArea*>("forestScroll");
                QVERIFY(scroll);
                QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
                captureFullscreen(QStringLiteral("forest-statistics-bottom"));
                scroll->verticalScrollBar()->setValue(0);
            }
            if (pageCheck.first == QStringLiteral("navAchievements")) {
                auto* page = window.findChild<QWidget*>("achievementsPage");
                auto* content = page ? page->findChild<QWidget*>("achievementContent") : nullptr;
                auto* summary = page ? page->findChild<QLabel*>("achievementSummary") : nullptr;
                auto* scroll = page ? page->findChild<QScrollArea*>("achievementScroll") : nullptr;
                QVERIFY(page && content && summary && scroll);
                QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                int cardCount = 0;
                int unlockedCount = 0;
                QRect previousCard;
                for (QFrame* card : content->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
                    if (!card->property("achievementId").isValid() || card->isHidden()) continue;
                    ++cardCount;
                    const auto definitions = achievements.all();
                    const auto definition = std::find_if(definitions.cbegin(), definitions.cend(),
                        [card](const AchievementEngine::Info& info) {
                            return info.id == card->property("achievementId").toString();
                        });
                    QVERIFY(definition != definitions.cend());
                    const bool unlocked = achievements.isUnlocked(
                        static_cast<int>(definition - definitions.cbegin()));
                    if (unlocked) ++unlockedCount;
                    QCOMPARE(card->property("unlocked").toBool(), unlocked);
                    auto* icon = card->findChild<QLabel*>("achievementIcon");
                    auto* reward = card->findChild<QLabel*>("achievementReward");
                    auto* state = card->findChild<QLabel*>("achievementState");
                    QVERIFY(icon && reward && state);
                    QCOMPARE(card->findChildren<QLabel*>("achievementReward").size(), 1);
                    QVERIFY(card->height() >= 124 && icon->width() >= 56 && icon->height() >= 56);
                    QVERIFY(reward->width() >= 132 && state->width() >= 124);
                    QCOMPARE(reward->text(), QStringLiteral("🪙 %1 金币").arg(definition->rewardCoins));
                    QVERIFY(QFontMetrics(reward->font()).horizontalAdvance(reward->text()) <=
                            reward->contentsRect().width());
                    QVERIFY(labelTextFits(state));
                    const QRect rewardBounds(reward->mapTo(card, QPoint()), reward->size());
                    const QRect stateBounds(state->mapTo(card, QPoint()), state->size());
                    QVERIFY(!rewardBounds.intersects(stateBounds));
                    QVERIFY(rewardBounds.right() < stateBounds.left());
                    QVERIFY(!previousCard.intersects(card->geometry()));
                    previousCard = card->geometry();
                    if (unlocked) {
                        QVERIFY(state->text().contains(QStringLiteral("已解锁")));
                        QVERIFY(state->text().contains(QStringLiteral("奖励已发放")));
                    } else {
                        QVERIFY(state->text().contains(QStringLiteral("未解锁")));
                    }
                }
                QCOMPARE(cardCount, achievements.count());
                QVERIFY(summary->text().contains(
                    QStringLiteral("%1 / %2").arg(unlockedCount).arg(achievements.count())));
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
                captureFullscreen(QStringLiteral("achievements-list-bottom"));
                scroll->verticalScrollBar()->setValue(0);
            }
            if (pageCheck.first == QStringLiteral("navGacha")) {
                auto* page = window.findChild<QWidget*>("gachaPage");
                auto* workspace = page ? page->findChild<QScrollArea*>("gachaWorkspaceScroll") : nullptr;
                auto* digPanel = window.findChild<QFrame*>("gachaDigPanel");
                auto* content = page ? page->findChild<QWidget*>("gachaContent") : nullptr;
                auto* collection = page ? page->findChild<QWidget*>("gachaCollection") : nullptr;
                auto* collectionScroll = page ? page->findChild<QScrollArea*>("gachaScroll") : nullptr;
                auto* variants = page ? page->findChild<QWidget*>("gachaVariants") : nullptr;
                QVERIFY(page && workspace && digPanel && content && collection && collectionScroll && variants);
                QCOMPARE(workspace->horizontalScrollBar()->maximum(), 0);
                QCOMPARE(collectionScroll->horizontalScrollBar()->maximum(), 0);
                QCOMPARE(digPanel->minimumWidth(), digPanel->maximumWidth());
                QVERIFY(digPanel->width() >= 300 && digPanel->width() <= 340);
                standardGachaDigWidth = digPanel->width();
                QVERIFY(digPanel->geometry().right() < collection->geometry().left());
                verifyVariantRows(variants, 3);
                auto* pullButton = buttonByTestId(window, QStringLiteral("gachaPullButton"));
                verifyAccessibleButton(pullButton, digPanel);
                QCOMPARE(pullButton->palette().color(QPalette::ButtonText), QColor(Qt::white));
                const QColor buttonFill = pullButton->grab().toImage().pixelColor(8, pullButton->height() / 2);
                QVERIFY(buttonFill == AppStyle::color(AppStyle::Color::Primary) ||
                        buttonFill == AppStyle::color(AppStyle::Color::PrimaryHover));
            }
        }
        const QString longOverviewTagName =
            QStringLiteral("Stage6 shared overview tag truncation tooltip validation");
        const auto longOverviewTag = features.tags()->addAndSave(longOverviewTagName);
        QVERIFY(longOverviewTag.status == TagManager::AddStatus::Added);
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
            QVERIFY(overviewDialog->styleSheet().contains(QStringLiteral("QDialog#forestOverviewDialog QPushButton#btnPrimary")));
            QVERIFY(overviewDialog->styleSheet().contains(AppStyle::color(AppStyle::Color::Primary).name()));
            QRadioButton* longOverviewTagOption = nullptr;
            for (auto* option : overviewDialog->findChildren<QRadioButton*>()) {
                if (option->property("tagId").toUInt() == longOverviewTag.tag.id) {
                    longOverviewTagOption = option;
                    break;
                }
            }
            QVERIFY(longOverviewTagOption);
            QCOMPARE(longOverviewTagOption->accessibleName(), longOverviewTagName);
            QVERIFY(longOverviewTagOption->text() != longOverviewTagName);
            QVERIFY(longOverviewTagOption->text().endsWith(QChar(0x2026)));
            QCOMPARE(longOverviewTagOption->toolTip(), longOverviewTagName);
            QTest::mouseClick(overviewAddTag, Qt::LeftButton);
            auto* overviewAddInput = overviewDialog->findChild<QLineEdit*>("forestOverviewAddInput");
            auto* overviewAddConfirm = buttonByTestId(*overviewDialog, QStringLiteral("forestOverviewAddConfirm"));
            QVERIFY(overviewAddInput);
            QVERIFY(overviewAddConfirm);
            QVERIFY(overviewAddInput->hasFocus());
            overviewAddInput->setText(QStringLiteral("Popup add"));
            const int tagsBefore = features.tags()->count();
            StorageEnvelope::failNextWriteForTesting(QDir(userDir).filePath(QStringLiteral("tags.dat")));
            bool saveFailureShown = false;
            QTimer::singleShot(0, [&]() {
                auto* error = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!error) {
                    if (auto* modal = QApplication::activeModalWidget()) modal->close();
                    return;
                }
                saveFailureShown = error->windowTitle() == QStringLiteral("保存标签失败");
                captureModalFullscreen(QStringLiteral("forest-tag-save-failed"));
                error->accept();
            });
            QTest::mouseClick(overviewAddConfirm, Qt::LeftButton);
            QVERIFY(saveFailureShown);
            QCOMPARE(features.tags()->count(), tagsBefore);
            QCOMPARE(overviewAddInput->text(), QStringLiteral("Popup add"));
            for (auto* option : overviewDialog->findChildren<QRadioButton*>())
                QVERIFY(option->text() != QStringLiteral("Popup add"));
            overviewAddInput->setFocus();
            QTest::keyClick(overviewAddInput, Qt::Key_Return);
            QVERIFY(overviewDialog->isVisible());
            QVERIFY(!overviewAddInput->isVisible());
            QCOMPARE(features.tags()->count(), tagsBefore + 1);
            bool addedTagVisible = false;
            for (QRadioButton* option : overviewDialog->findChildren<QRadioButton*>()) {
                if (option->text() == QStringLiteral("Popup add")) {
                    addedTagVisible = true;
                    break;
                }
            }
            QVERIFY(addedTagVisible);
            auto* tagGrid = qobject_cast<QGridLayout*>(overviewAddTag->parentWidget()->layout());
            auto* tagScroll = overviewDialog->findChild<QScrollArea*>("forestOverviewTagScroll");
            QVERIFY(tagGrid && tagScroll);
            QCOMPARE(tagGrid->indexOf(overviewAddTag), tagGrid->count() - 1);
            QCOMPARE(tagScroll->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
            int extraTag = 0;
            while (tagGrid->count() < 17) {
                QTest::mouseClick(overviewAddTag, Qt::LeftButton);
                overviewAddInput->setText(QStringLiteral("标签 %1").arg(++extraTag));
                QTest::keyClick(overviewAddInput, Qt::Key_Return);
                QVERIFY(overviewDialog->isVisible());
                QVERIFY(!overviewAddInput->isVisible());
                QCOMPARE(tagGrid->indexOf(overviewAddTag), tagGrid->count() - 1);
                if (tagGrid->count() == 16) {
                    QCOMPARE(tagScroll->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
                    captureModalFullscreen(QStringLiteral("forest-tags-four-rows"));
                }
            }
            QCOMPARE(tagScroll->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
            QTRY_VERIFY(tagScroll->verticalScrollBar()->maximum() > 0);
            tagScroll->verticalScrollBar()->setValue(tagScroll->verticalScrollBar()->maximum());
            captureModalFullscreen(QStringLiteral("forest-tags-scroll-bottom"));
            tagScroll->verticalScrollBar()->setValue(0);
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
            captureModalFullscreen(QStringLiteral("forest-overview-filter"));
            QTest::mouseClick(overviewApply, Qt::LeftButton);
            overviewFilterApplied = true;
        });
        forestDashboard->repaint();
        QVERIFY(!forestDashboard->overviewRect_.isEmpty());
        QTest::mouseClick(forestDashboard, Qt::LeftButton, Qt::NoModifier,
                          forestDashboard->overviewRect_.center().toPoint());
        QTRY_VERIFY(overviewFilterApplied);
        QTRY_COMPARE(forestDashboard->recordFilter().tagId, smokeTagId);
        forestDashboard->setRecordFilter({});
        const int focusTagIndex = tagSelector->findData(smokeTagId);
        QVERIFY(focusTagIndex >= 0);
        tagSelector->setCurrentIndex(focusTagIndex);
        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 0);
        window.showFullScreen();

        QTest::qWait(50);
        QVERIFY(timer->timeSelectorAvailable());
        QSignalSpy durationSpy(timer, &PlantTimerWidget::sig_timeSelected);
        QSignalSpy ringPlantSpy(timer, &PlantTimerWidget::sig_plantClicked);
        const QRectF ringBounds = timer->ringBounds();
        const qreal radius = ringBounds.width() / 2.0;
        const qreal centerY = ringBounds.center().y();
        const qreal angle = (timer->selectedMinutes() - 10) * 2.0 * 3.141592653589793 / 110.0;
        const QPoint knob(qRound(timer->width() / 2.0 + radius * std::sin(angle)),
                          qRound(centerY - radius * std::cos(angle)));
        QTest::mousePress(timer, Qt::LeftButton, Qt::NoModifier, knob);
        QTest::mouseMove(timer, QPoint(qRound(timer->width() / 2.0 + radius), qRound(centerY)));
        QTest::mouseRelease(timer, Qt::LeftButton);
        QCOMPARE(timer->selectedMinutes(), uint32_t(38));
        QVERIFY(durationSpy.count() > 0);
        QCOMPARE(ringPlantSpy.count(), 0);
        timer->setSelectedMinutes(25);
        QTest::qWait(50);

        const auto closeMessageBox = []() {
            QTimer::singleShot(0, []() {
                if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    dialog->accept();
                }
            });
        };

        auto* shopNavigation = window.findChild<QPushButton*>("navShop");
        QVERIFY(shopNavigation);
        QTest::mouseClick(shopNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 2);
        window.showFullScreen();
        QTest::qWait(100);
        auto* shopPage = window.findChild<QWidget*>("shopPage");
        auto* shopScroll = window.findChild<QScrollArea*>("shopScroll");
        auto* shopBalance = window.findChild<QLabel*>("shopBalanceLabel");
        QVERIFY(shopPage && shopScroll && shopBalance);
        const PlantDefinition& pine = PlantCatalog::byType(1);
        const PlantDefinition& cactus = PlantCatalog::byType(5);
        const auto cardForPlant = [shopPage](uint32_t type) {
            for (auto* card : shopPage->findChildren<QFrame*>(QStringLiteral("shopCard"))) {
                if (card->property("plantType").toUInt() == type) return card;
            }
            return static_cast<QFrame*>(nullptr);
        };
        const auto hasTransparentPixel = [](const QLabel* imageSlot) {
            if (!imageSlot) return false;
            const QPixmap pixmap = imageSlot->pixmap();
            if (pixmap.isNull()) return false;
            const QImage image = pixmap.toImage();
            if (!image.hasAlphaChannel()) return false;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(image.pixel(x, y)) < 255) return true;
                }
            }
            return false;
        };
        auto* pineCardBefore = cardForPlant(pine.type);
        auto* purchase = buttonByTestId(window, QStringLiteral("shopPurchase_1"));
        auto* pineImageSlot = pineCardBefore
            ? pineCardBefore->findChild<QLabel*>(QStringLiteral("plantImageSlot")) : nullptr;
        auto* pineState = shopPage->findChild<QLabel*>(QStringLiteral("shopState_1"));
        auto* pineDetails = buttonByTestId(window, QStringLiteral("shopDetails_1"));
        QVERIFY(pineCardBefore && purchase && pineImageSlot && pineState && pineDetails);
        QVERIFY(purchase->isEnabled());
        QVERIFY(shopScroll->isVisible());
        QCOMPARE(pineCardBefore->property("plantType").toUInt(), pine.type);
        QCOMPARE(pineImageSlot->size(), QSize(120, 120));
        QCOMPARE(purchase->size(), QSize(148, 40));
        QVERIFY(pineCardBefore->height() >= 116);
        const int pineCardHeight = pineCardBefore->height();
        QCOMPARE(pineCardBefore->parentWidget()->height(), pineCardHeight);
        QVERIFY(hasTransparentPixel(pineImageSlot));
        QVERIFY(shopBalance->text().contains(QString::number(coins.balance())));

        const uint32_t beforeFailedPurchase = coins.balance();
        int failedPurchaseFeedbackCount = 0;
        QString failedPurchaseFeedbackTitle;
        QString failedPurchaseFeedbackText;
        StorageEnvelope::failNextWriteForTesting(QDir(userDir).filePath("coins.dat"));
        QTimer::singleShot(0, [&]() {
            auto* feedback = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!feedback) return;
            ++failedPurchaseFeedbackCount;
            failedPurchaseFeedbackTitle = feedback->windowTitle();
            failedPurchaseFeedbackText = feedback->text() + feedback->informativeText();
            captureModalFullscreen(QStringLiteral("shop-save-failed"));
            feedback->accept();
        });
        QTest::mouseClick(purchase, Qt::LeftButton);
        QCOMPARE(coins.balance(), beforeFailedPurchase);
        QVERIFY(!coins.isPlantUnlocked(pine.type));
        QCOMPARE(failedPurchaseFeedbackCount, 1);
        QCOMPARE(failedPurchaseFeedbackTitle, QStringLiteral("购买未完成"));
        QVERIFY(failedPurchaseFeedbackText.contains(QStringLiteral("保存")));
        QCOMPARE(pineCardBefore->height(), pineCardHeight);
        QVERIFY(pineState->text().contains(QStringLiteral("未改变")));
        captureFullscreen(QStringLiteral("shop-save-failed-state"));
        CoinManager persistedAfterFailure;
        persistedAfterFailure.setDataPath(QDir(userDir).filePath("coins.dat").toStdString());
        QVERIFY(persistedAfterFailure.load());
        QCOMPARE(persistedAfterFailure.balance(), beforeFailedPurchase);
        QVERIFY(!persistedAfterFailure.isPlantUnlocked(pine.type));

        int successFeedbackCount = 0;
        QString successFeedbackTitle;
        QString successFeedbackText;
        QTimer::singleShot(0, [&]() {
            auto* feedback = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!feedback) return;
            ++successFeedbackCount;
            successFeedbackTitle = feedback->windowTitle();
            successFeedbackText = feedback->text() + feedback->informativeText();
            feedback->accept();
        });
        QTest::mouseClick(purchase, Qt::LeftButton);
        QVERIFY(coins.isPlantUnlocked(pine.type));
        QCOMPARE(successFeedbackCount, 1);
        QCOMPARE(successFeedbackTitle, QStringLiteral("解锁成功"));
        QVERIFY(successFeedbackText.contains(QStringLiteral("新植物已解锁")));
        CoinManager persistedAfterPurchase;
        persistedAfterPurchase.setDataPath(QDir(userDir).filePath("coins.dat").toStdString());
        QVERIFY(persistedAfterPurchase.load());
        QCOMPARE(persistedAfterPurchase.balance(), coins.balance());
        QVERIFY(persistedAfterPurchase.isPlantUnlocked(pine.type));

        QTRY_VERIFY(buttonByTestId(window, QStringLiteral("shopPurchase_1")) &&
                    !buttonByTestId(window, QStringLiteral("shopPurchase_1"))->isEnabled());
        auto* pineCardAfter = cardForPlant(pine.type);
        auto* ownedPurchase = buttonByTestId(window, QStringLiteral("shopPurchase_1"));
        auto* pineDetailsAfter = buttonByTestId(window, QStringLiteral("shopDetails_1"));
        auto* pineImageSlotAfter = pineCardAfter
            ? pineCardAfter->findChild<QLabel*>(QStringLiteral("plantImageSlot")) : nullptr;
        auto* ownedState = shopPage->findChild<QLabel*>(QStringLiteral("shopState_1"));
        QVERIFY(pineCardAfter && ownedPurchase && pineDetailsAfter && pineImageSlotAfter && ownedState);
        QTRY_COMPARE(pineCardAfter->height(), pineCardHeight);
        QCOMPARE(pineImageSlotAfter->size(), QSize(120, 120));
        QCOMPARE(ownedPurchase->size(), QSize(148, 40));
        QVERIFY(!ownedPurchase->isEnabled());
        QVERIFY(ownedState->text().contains(QStringLiteral("已拥有")));
        QVERIFY(shopBalance->text().contains(QString::number(coins.balance())));
        captureFullscreen(QStringLiteral("shop-after-purchase"));

        auto* cactusPurchase = buttonByTestId(window, QStringLiteral("shopPurchase_5"));
        auto* cactusState = shopPage->findChild<QLabel*>(QStringLiteral("shopState_5"));
        QVERIFY(cactusPurchase && cactusState);
        QVERIFY(coins.balance() < cactus.storeCost);
        QVERIFY(!cactusPurchase->isEnabled());
        QVERIFY(cactusPurchase->toolTip().contains(QString::number(cactus.storeCost)));
        QVERIFY(cactusPurchase->toolTip().contains(QString::number(coins.balance())));
        QVERIFY(cactusState->text().contains(QStringLiteral("金币不足")));
        const uint32_t beforeUnaffordablePurchase = coins.balance();
        QTest::mouseClick(cactusPurchase, Qt::LeftButton);
        QCOMPARE(coins.balance(), beforeUnaffordablePurchase);
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(shopBalance->text().contains(QString::number(coins.balance())));
        captureFullscreen(QStringLiteral("shop-insufficient-state"));

        int detailDialogObserved = 0;
        int detailImageReadable = 0;
        int detailContentReadable = 0;
        int detailCloseFound = 0;
        QTimer::singleShot(0, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) {
                if (auto* modal = QApplication::activeModalWidget()) modal->close();
                return;
            }
            detailDialogObserved = dialog->objectName() == QStringLiteral("shopPlantDetailDialog") &&
                dialog->windowTitle() == pine.displayName;
            auto* detailImage = dialog->findChild<QLabel*>(QStringLiteral("shopPlantDetailImage"));
            auto* detailContent = dialog->findChild<QLabel*>(QStringLiteral("shopPlantDetailContent"));
            detailImageReadable = detailImage && !detailImage->pixmap().isNull();
            if (detailContent) {
                const QString content = detailContent->text();
                detailContentReadable = detailContent->isVisible() && detailContent->wordWrap() &&
                    detailContent->textFormat() == Qt::PlainText &&
                    content.contains(QString::number(pine.storeCost));
            }
            captureModalFullscreen(QStringLiteral("shop-plant-detail"));
            auto* close = buttonByTestId(*dialog, QStringLiteral("shopPlantDetailClose"));
            detailCloseFound = close != nullptr;
            if (close) QTest::mouseClick(close, Qt::LeftButton);
            else dialog->close();
        });
        QTest::mouseClick(pineDetailsAfter, Qt::LeftButton);
        QCOMPARE(detailDialogObserved, 1);
        QCOMPARE(detailImageReadable, 1);
        QCOMPARE(detailContentReadable, 1);
        QCOMPARE(detailCloseFound, 1);
        QVERIFY(!QApplication::activeModalWidget());

        auto* gachaNavigation = window.findChild<QPushButton*>("navGacha");
        QVERIFY(gachaNavigation);
        QTest::mouseClick(gachaNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 5);
        auto* gachaPull = buttonByTestId(window, QStringLiteral("gachaPullButton"));
        auto* gachaResult = window.findChild<QLabel*>("gachaResultTitle");
        auto* gachaPage = window.findChild<QWidget*>("gachaPage");
        auto* gachaDescription = window.findChild<QLabel*>("gachaResultDescription");
        auto* gachaIcon = window.findChild<QLabel*>("gachaResultIcon");
        auto* collectionSummary = window.findChild<QLabel*>("gachaCollectionSummary");
        auto* collectionScroll = gachaPage ? gachaPage->findChild<QScrollArea*>("gachaScroll") : nullptr;
        QVERIFY(gachaPull && gachaResult && gachaPage && gachaDescription && gachaIcon && collectionSummary && collectionScroll);
        const auto hasGachaText = [gachaPage](const QString& text) {
            for (auto* label : gachaPage->findChildren<QLabel*>()) {
                if (label->isVisible() && label->text() == text) return true;
            }
            return false;
        };
        const auto variantCard = [gachaPage](const QString& id) {
            for (QFrame* card : gachaPage->findChildren<QFrame*>()) {
                if (!card->isHidden() && card->property("variantId").toString() == id) return card;
            }
            return static_cast<QFrame*>(nullptr);
        };
        QCOMPARE(gachaPage->property("resultState").toString(), QStringLiteral("ready"));
        QCOMPARE(features.gacha()->unlockedCount(), 0);
        for (const auto& variant : features.gacha()->allVariants()) {
            QTRY_VERIFY(variantCard(variant.id));
            QVERIFY(!variantCard(variant.id)->findChild<QLabel*>("variantDescription"));
            for (QLabel* label : variantCard(variant.id)->findChildren<QLabel*>()) {
                QVERIFY(!label->text().contains(variant.displayName));
            }
        }
        const uint32_t beforeFailedPull = coins.balance();
        features.gacha()->failVariantsSaveAfterForTesting(0);
        QTest::mouseClick(gachaPull, Qt::LeftButton);
        QCOMPARE(gachaPage->property("resultState").toString(), QStringLiteral("error"));
        QCOMPARE(gachaResult->text(), QStringLiteral("上次发掘未完成"));
        QCOMPARE(gachaDescription->text(), features.gacha()->lastError());
        QCOMPARE(coins.balance(), beforeFailedPull);
        QCOMPARE(features.gacha()->unlockedCount(), 0);
        QVERIFY(!QApplication::activeModalWidget());
        captureFullscreen(QStringLiteral("gacha-save-failed"));

        QTest::mouseClick(gachaPull, Qt::LeftButton);
        QCOMPARE(gachaPage->property("resultState").toString(), QStringLiteral("new"));
        QCOMPARE(features.gacha()->unlockedCount(), 1);
        QVERIFY(hasTransparentPixel(gachaIcon));
        QCOMPARE(gachaDescription->textFormat(), Qt::PlainText);
        QCOMPARE(collectionSummary->text(), QStringLiteral("已发现 1 / %1 种").arg(features.gacha()->variantCount()));
        QTRY_VERIFY(hasGachaText(QStringLiteral("当前金币：🪙 %1").arg(coins.balance())));
        for (const auto& variant : features.gacha()->allVariants()) {
            if (!variant.unlocked) continue;
            QTRY_VERIFY(variantCard(variant.id) && variantCard(variant.id)->findChild<QLabel*>("variantDescription"));
            QCOMPARE(variantCard(variant.id)->findChild<QLabel*>("variantDescription")->text(), variant.description);
            QVERIFY(gachaDescription->text().contains(variant.description));
        }
        captureFullscreen(QStringLiteral("gacha-after-pull"));

        for (const auto& variant : features.gacha()->allVariants()) {
            if (!variant.unlocked) QVERIFY(features.gacha()->unlockVariant(variant.id));
        }
        QTest::mouseClick(gachaPull, Qt::LeftButton);
        QCOMPARE(gachaPage->property("resultState").toString(), QStringLiteral("duplicate"));
        QCOMPARE(gachaResult->text(), QStringLiteral("重复获得，已返还金币"));
        QVERIFY(gachaDescription->text().contains(QStringLiteral("返还 🪙 %1 金币").arg(GachaManager::duplicateCoinRefund())));
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(collectionSummary->text(), QStringLiteral("已发现 %1 / %1 种").arg(features.gacha()->variantCount()));
        QCOMPARE(features.gacha()->variantCount(), 12);
        int transparentVariantIconCount = 0;
        for (const auto& variant : features.gacha()->allVariants()) {
            QTRY_VERIFY(variantCard(variant.id) && variantCard(variant.id)->findChild<QLabel*>("variantDescription"));
            QCOMPARE(variantCard(variant.id)->findChild<QLabel*>("variantDescription")->text(), variant.description);
            auto* variantIcon = variantCard(variant.id)->findChild<QLabel*>("variantIcon");
            QVERIFY(variantIcon && !variantIcon->pixmap().isNull());
            QVERIFY(hasTransparentPixel(variantIcon));
            ++transparentVariantIconCount;
        }
        QCOMPARE(transparentVariantIconCount, 12);
        QTRY_VERIFY(hasGachaText(QStringLiteral("当前金币：🪙 %1").arg(coins.balance())));
        captureFullscreen(QStringLiteral("gacha-duplicate"));
        collectionScroll->verticalScrollBar()->setValue(collectionScroll->verticalScrollBar()->maximum() / 2);
        captureFullscreen(QStringLiteral("gacha-collection-middle"));
        collectionScroll->verticalScrollBar()->setValue(collectionScroll->verticalScrollBar()->maximum());
        captureFullscreen(QStringLiteral("gacha-collection-bottom"));
        collectionScroll->verticalScrollBar()->setValue(0);
        auto* achievementNavigation = window.findChild<QPushButton*>("navAchievements");
        QVERIFY(achievementNavigation);
        QTest::mouseClick(achievementNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 4);
        captureFullscreen(QStringLiteral("achievements-state"));
        auto* achievementPage = window.findChild<QWidget*>("achievementsPage");
        auto* achievementScroll = achievementPage ? achievementPage->findChild<QScrollArea*>("achievementScroll") : nullptr;
        QVERIFY(achievementScroll);
        int issuedRewardCards = 0;
        for (QFrame* card : achievementPage->findChildren<QFrame*>()) {
            if (card->isHidden() || !card->property("achievementId").isValid() || !card->property("unlocked").toBool()) continue;
            auto* state = card->findChild<QLabel*>("achievementState");
            QVERIFY(state);
            QVERIFY(state->text().contains(QStringLiteral("已解锁")));
            QVERIFY(state->text().contains(QStringLiteral("奖励已发放")));
            ++issuedRewardCards;
        }
        QVERIFY(issuedRewardCards > 0);
        achievementScroll->verticalScrollBar()->setValue(achievementScroll->verticalScrollBar()->maximum());
        captureFullscreen(QStringLiteral("achievements-unlocked-bottom"));
        QTest::mouseClick(gachaNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 5);

        const uint32_t beforeInsufficient = coins.balance();
        QVERIFY(coins.spend(beforeInsufficient));
        QTest::mouseClick(gachaPull, Qt::LeftButton);
        QCOMPARE(gachaPage->property("resultState").toString(), QStringLiteral("error"));
        QVERIFY(gachaDescription->text().contains(QStringLiteral("金币不足")));
        QCOMPARE(coins.balance(), uint32_t(0));
        QCOMPARE(features.gacha()->unlockedCount(), features.gacha()->variantCount());
        QVERIFY(!QApplication::activeModalWidget());
        captureFullscreen(QStringLiteral("gacha-insufficient"));
        QVERIFY(coins.refund(beforeInsufficient));
        QCOMPARE(coins.balance(), beforeInsufficient);

        auto* friendsNavigation = window.findChild<QPushButton*>("navFriends");
        QVERIFY(friendsNavigation);
        QTest::mouseClick(friendsNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 6);
        auto* friendPage = window.findChild<QWidget*>("friendsPage");
        auto* friendSearch = friendPage ? friendPage->findChild<QLineEdit*>("friendSearchInput") : nullptr;
        auto* friendList = friendPage ? friendPage->findChild<QListWidget*>("friendUserList") : nullptr;
        auto* sendRequest = friendPage ? buttonByTestId(*friendPage, QStringLiteral("friendSendRequestButton")) : nullptr;
        auto* friendTabs = friendPage ? friendPage->findChild<QTabWidget*>("friendTabs") : nullptr;
        auto* requestList = friendPage ? friendPage->findChild<QListWidget*>("friendRequestList") : nullptr;
        auto* acceptedFriends = friendPage ? friendPage->findChild<QListWidget*>("friendList") : nullptr;
        auto* browseEmpty = friendPage ? friendPage->findChild<QLabel*>("friendBrowseEmpty") : nullptr;
        auto* requestsEmpty = friendPage ? friendPage->findChild<QLabel*>("friendRequestsEmpty") : nullptr;
        auto* friendsEmpty = friendPage ? friendPage->findChild<QLabel*>("friendListEmpty") : nullptr;
        QVERIFY(friendSearch && friendList && sendRequest && friendTabs && requestList && acceptedFriends);
        QVERIFY(browseEmpty && requestsEmpty && friendsEmpty);
        QCOMPARE(friendTabs->count(), 3);
        QVERIFY(!sendRequest->isEnabled());
        friendSearch->setText(QStringLiteral("no-such-user"));
        QTest::keyClick(friendSearch, Qt::Key_Return);
        QTRY_COMPARE(friendList->count(), 0);
        QTRY_VERIFY(browseEmpty->isVisible());
        QVERIFY(!friendList->isVisible());
        captureFullscreen(QStringLiteral("friends-empty-browse"));
        friendTabs->setCurrentIndex(1);
        auto* acceptRequest = buttonByTestId(*friendPage, QStringLiteral("friendAcceptButton"));
        auto* rejectRequest = buttonByTestId(*friendPage, QStringLiteral("friendRejectButton"));
        QVERIFY(acceptRequest && rejectRequest);
        QTRY_VERIFY(requestsEmpty->isVisible());
        QCOMPARE(requestList->count(), 0);
        QVERIFY(!requestList->isVisible());
        QVERIFY(!acceptRequest->isEnabled() && !rejectRequest->isEnabled());
        captureFullscreen(QStringLiteral("friends-empty-requests"));
        friendTabs->setCurrentIndex(2);
        auto* challengeFriend = buttonByTestId(*friendPage, QStringLiteral("friendChallengeButton"));
        QVERIFY(challengeFriend);
        QTRY_VERIFY(friendsEmpty->isVisible());
        QCOMPARE(acceptedFriends->count(), 0);
        QVERIFY(!acceptedFriends->isVisible() && !challengeFriend->isEnabled());
        captureFullscreen(QStringLiteral("friends-empty-list"));
        friendTabs->setCurrentIndex(0);
        friendSearch->setText(QStringLiteral("ui-smoke-friend"));
        QTest::keyClick(friendSearch, Qt::Key_Return);
        QTRY_COMPARE(friendList->count(), 1);
        QTRY_VERIFY(friendList->isVisible());
        QTest::qWait(50);
        QTest::mouseClick(friendList->viewport(), Qt::LeftButton, Qt::NoModifier,
                          friendList->visualItemRect(friendList->item(0)).center());
        QTRY_COMPARE(friendList->currentItem(), friendList->item(0));
        QVERIFY(sendRequest->isEnabled());
        captureFullscreen(QStringLiteral("friends-before-request-send"));
        StorageEnvelope::failNextWriteForTesting(PathConfig::getAppDataFilePath("friend_reqs.dat"));
        bool friendSaveFailureShown = false;
        QTimer::singleShot(0, [&] {
            auto* modal = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(modal);
            friendSaveFailureShown = true;
            captureModalFullscreen(QStringLiteral("friends-request-save-failed"));
            QTest::keyClick(modal, Qt::Key_Escape);
        });
        QTest::mouseClick(sendRequest, Qt::LeftButton);
        QVERIFY(friendSaveFailureShown);
        QCOMPARE(features.friends()->outgoingRequests().size(), 0);
        QVERIFY(sendRequest->isEnabled());
        closeMessageBox();
        QTest::mouseClick(sendRequest, Qt::LeftButton);
        QTRY_COMPARE(features.friends()->outgoingRequests().size(), 1);
        QVERIFY(friendList->currentItem());
        QVERIFY(friendList->currentItem()->data(Qt::UserRole + 1).toBool());
        QVERIFY(!sendRequest->isEnabled());
        QVERIFY(!QApplication::activeModalWidget());
        captureFullscreen(QStringLiteral("friends-request-sent"));

        FriendManager peer(PathConfig::getAppDataFilePath("friend_reqs.dat"),
                           QDir(userDir).filePath("ui-peer-friends.dat"), 2, users);
        QVERIFY(peer.load() && peer.rejectRequest(1) && peer.sendRequest(1));
        QVERIFY(features.friends()->load());
        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTest::mouseClick(friendsNavigation, Qt::LeftButton);
        friendTabs->setCurrentIndex(1);
        QCOMPARE(requestList->count(), 1);
        QTRY_VERIFY(requestList->isVisible());
        QTest::qWait(50);
        QVERIFY(!acceptRequest->isEnabled() && !rejectRequest->isEnabled());
        QTest::mouseClick(requestList->viewport(), Qt::LeftButton, Qt::NoModifier,
                          requestList->visualItemRect(requestList->item(0)).center());
        QVERIFY(acceptRequest->isEnabled() && rejectRequest->isEnabled());
        captureFullscreen(QStringLiteral("friends-incoming-request"));
        QTest::mouseClick(rejectRequest, Qt::LeftButton);
        QCOMPARE(features.friends()->incomingRequests().size(), 0);
        QCOMPARE(requestList->count(), 0);
        QVERIFY(!rejectRequest->isEnabled());
        QVERIFY(peer.load() && peer.sendRequest(1));
        QVERIFY(features.friends()->load());
        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTest::mouseClick(friendsNavigation, Qt::LeftButton);
        friendTabs->setCurrentIndex(1);
        QCOMPARE(requestList->count(), 1);
        QTRY_VERIFY(requestList->isVisible());
        QTest::qWait(50);
        QTest::mouseClick(requestList->viewport(), Qt::LeftButton, Qt::NoModifier,
                          requestList->visualItemRect(requestList->item(0)).center());
        QTest::mouseClick(acceptRequest, Qt::LeftButton);
        QVERIFY(features.friends()->isFriend(2));
        QCOMPARE(requestList->count(), 0);
        friendTabs->setCurrentIndex(0);
        QVERIFY(!sendRequest->isEnabled());
        QVERIFY(friendList->currentItem()->data(Qt::UserRole + 1).toBool());
        friendTabs->setCurrentIndex(2);
        QCOMPARE(acceptedFriends->count(), 1);
        QTRY_VERIFY(acceptedFriends->isVisible());
        QTest::qWait(50);
        QVERIFY(!challengeFriend->isEnabled());
        QTest::mouseClick(acceptedFriends->viewport(), Qt::LeftButton, Qt::NoModifier,
                          acceptedFriends->visualItemRect(acceptedFriends->item(0)).center());
        QVERIFY(challengeFriend->isEnabled());
        captureFullscreen(QStringLiteral("friends-accepted-list"));
        QTest::mouseClick(challengeFriend, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 7);

        auto* challengeNavigation = window.findChild<QPushButton*>("navChallenges");
        QVERIFY(challengeNavigation);
        QTest::mouseClick(challengeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 7);
        auto* challenge = window.findChild<ChallengeDashboardWidget*>("challengeDashboard");
        QVERIFY(challenge);
        QTest::qWait(50);
        QSignalSpy checkinReward(challenge, &ChallengeDashboardWidget::rewardsChanged);
        const auto checkinData = ChallengeRewardService::progress(QDate::currentDate(), {}, {});
        const int currentCheckin = checkinData.currentCheckin;
        const int checkinCount = checkinData.checkins.size();
        QVERIFY(currentCheckin >= 0 && currentCheckin < checkinCount);
        const auto challengeActionCenter = [challenge](const char* action, int index) {
            for (QToolButton* button : challenge->findChildren<QToolButton*>()) {
                if (button->property("challengeAction").toString() == QLatin1String(action) &&
                    button->property("challengeIndex").toInt() == index && !button->isHidden()) {
                    return button->geometry().center();
                }
            }
            return QPoint(-1, -1);
        };
        const auto clickChallengePoint = [challenge](const QPoint& point) {
            for (QWidget* ancestor = challenge->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
                if (auto* scroll = qobject_cast<QScrollArea*>(ancestor)) {
                    scroll->ensureVisible(point.x(), point.y(), 20, 20);
                    break;
                }
            }
            QTest::mouseClick(challenge, Qt::LeftButton, Qt::NoModifier, point);
        };
        const QPoint checkinCenter = challengeActionCenter("checkinReward", currentCheckin);
        const QPoint checkinRefreshCenter = challengeActionCenter("checkinRefresh", -1);
        const QPoint taskChestCenter = challengeActionCenter("taskReward", 0);
        const QPoint taskArrowCenter = challengeActionCenter("taskAction", 0);
        const QPoint taskRefreshCenter = challengeActionCenter("checkinRefresh", 99);
        for (const QPoint& point : {checkinCenter, checkinRefreshCenter, taskChestCenter,
                                   taskArrowCenter, taskRefreshCenter}) QVERIFY(challenge->rect().contains(point));

        const auto beforeFailedClaim = coins.balance();
        StorageEnvelope::failNextWriteForTesting(QDir(userDir).filePath("coins.dat"));
        closeMessageBox();
        clickChallengePoint(checkinCenter);
        QCOMPARE(checkinReward.count(), 0);
        QCOMPARE(coins.balance(), beforeFailedClaim);
        closeMessageBox();
        clickChallengePoint(checkinCenter);
        QTRY_COMPARE(checkinReward.count(), 1);
        const uint32_t afterCheckin = beforeFailedClaim +
            static_cast<uint32_t>(checkinData.checkins[currentCheckin].coins);
        QCOMPARE(coins.balance(), afterCheckin);
        const QString checkinClaimKey = ChallengeRewardService::claimKey(
            ChallengeRewardService::Kind::Checkin, currentCheckin, QDate::currentDate());
        QVERIFY(coins.hasChallengeReward(checkinClaimKey));
        closeMessageBox();
        clickChallengePoint(checkinCenter);
        QTest::qWait(50);
        QCOMPARE(checkinReward.count(), 1);
        QCOMPARE(coins.balance(), afterCheckin);
        captureFullscreen(QStringLiteral("challenge-after-checkin"));
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        QMouseEvent checkinHover(QEvent::MouseMove, QPointF(checkinCenter),
            QPointF(challenge->mapToGlobal(checkinCenter)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(challenge, &checkinHover);
        QCOMPARE(challenge->cursor().shape(), Qt::PointingHandCursor);
        QVERIFY2(QToolTip::text().contains(QStringLiteral("已领取")), qPrintable(QToolTip::text()));

        const auto taskProgress =
            ChallengeRewardService::progress(QDate::currentDate(), {}, database.getAllRecords());
        QVERIFY(!taskProgress.tasks.isEmpty());
        QVERIFY(taskProgress.tasks[0].progress < taskProgress.tasks[0].target);
        const QString taskClaimKey = ChallengeRewardService::claimKey(
            ChallengeRewardService::Kind::Daily, 0, QDate::currentDate());
        QVERIFY(!coins.hasChallengeReward(taskClaimKey));
        const uint32_t beforeTaskPreview = coins.balance();
        bool previewShown = false;
        bool previewIconVisible = false;
        QString previewTitle;
        QTimer::singleShot(0, [&]() {
            auto* preview = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!preview) return;
            previewShown = preview->isVisible();
            previewTitle = preview->windowTitle();
            for (QLabel* label : preview->findChildren<QLabel*>()) {
                if (label->isVisible() && label->text() == QStringLiteral("🪙")) {
                    previewIconVisible = true;
                    QVERIFY(label->font().pixelSize() >= 54);
                }
            }
            auto* closeButton = preview->findChild<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
            if (closeButton && closeButton->text() == QStringLiteral("关闭")) {
                captureModalFullscreen(QStringLiteral("challenge-task-reward-preview"));
                QTest::mouseClick(closeButton, Qt::LeftButton);
            } else {
                preview->accept();
            }
        });
        clickChallengePoint(taskChestCenter);
        QVERIFY(previewShown);
        QCOMPARE(previewTitle, QStringLiteral("奖励预览"));
        QVERIFY(previewIconVisible);
        QCOMPARE(coins.balance(), beforeTaskPreview);
        QCOMPARE(checkinReward.count(), 1);

        QSignalSpy taskAction(challenge, &ChallengeDashboardWidget::openFocusRequested);
        QVERIFY(taskAction.isValid());
        clickChallengePoint(taskArrowCenter);
        QTRY_COMPARE(taskAction.count(), 1);
        QTRY_COMPARE(pageStack->currentIndex(), 0);
        QTest::mouseClick(challengeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 7);

        const auto clickAndVerifyRefreshDetail = [&](const QPoint& point, const QString& screenshotName) {
            bool dialogShown = false;
            QString dialogTitle;
            QString dialogText;
            QTimer::singleShot(0, [&]() {
                auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!dialog) return;
                dialogShown = true;
                dialogTitle = dialog->windowTitle();
                dialogText = dialog->text() + dialog->informativeText();
                captureModalFullscreen(screenshotName);
                dialog->accept();
            });
            clickChallengePoint(point);
            QVERIFY(dialogShown);
            QCOMPARE(dialogTitle, QStringLiteral("每日刷新规则"));
            QVERIFY(dialogText.contains(QStringLiteral("每日签到和每日挑战")));
        };
        clickAndVerifyRefreshDetail(checkinRefreshCenter, QStringLiteral("challenge-checkin-refresh-detail"));
        clickAndVerifyRefreshDetail(taskRefreshCenter, QStringLiteral("challenge-task-refresh-detail"));

        bool challengePlantDetailShown = false;
        QTimer::singleShot(0, [&]() {
            auto* detail = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(detail);
            QCOMPARE(detail->windowTitle(), QStringLiteral("植物详情"));
            QVERIFY(detail->windowFlags() & Qt::FramelessWindowHint);
            bool catalogNameShown = false;
            for (QLabel* label : detail->findChildren<QLabel*>()) {
                catalogNameShown |= label->text().contains(PlantCatalog::byType(checkinData.plantType).displayName);
            }
            QVERIFY(catalogNameShown);
            challengePlantDetailShown = true;
            captureModalFullscreen(QStringLiteral("challenge-catalog-plant-detail"));
            auto* shopButton = detail->findChild<QPushButton*>(QStringLiteral("btnPrimary"));
            QVERIFY(shopButton);
            QTest::mouseClick(shopButton, Qt::LeftButton);
        });
        clickChallengePoint(challengeActionCenter("plant", -1));
        QVERIFY(challengePlantDetailShown);
        QTRY_COMPARE(pageStack->currentIndex(), 2);
        QTest::mouseClick(challengeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 7);

        auto* guardianNavigation = window.findChild<QPushButton*>("navGuardian");
        QVERIFY(guardianNavigation);
        QTest::mouseClick(guardianNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 8);
        auto* goalSpin = window.findChild<QSpinBox*>("guardianGoalSpin");
        QVERIFY(goalSpin);
        const int originalGoal = goalSpin->value();
        StorageEnvelope::failNextWriteForTesting(QDir(userDir).filePath("guardian.dat"));
        closeMessageBox();
        goalSpin->setValue(originalGoal + 5);
        QCOMPARE(static_cast<int>(features.guardian()->dailyGoalMinutes()), originalGoal);
        QCOMPARE(goalSpin->value(), originalGoal);
        QCOMPARE(goalSpin->text(), QStringLiteral("%1 分钟").arg(originalGoal));
        captureFullscreen(QStringLiteral("guardian-goal-save-failed"));
        const int newGoal = goalSpin->value() + 5;
        goalSpin->setValue(newGoal);
        QTRY_COMPARE(static_cast<int>(features.guardian()->dailyGoalMinutes()), newGoal);
        QTRY_COMPARE(goalSpin->value(), newGoal);
        QCOMPARE(goalSpin->text(), QStringLiteral("%1 分钟").arg(newGoal));
        QStyleOptionSpinBox goalOption;
        goalOption.initFrom(goalSpin);
        goalOption.buttonSymbols = QAbstractSpinBox::UpDownArrows;
        goalOption.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
        const QRect stepUp = goalSpin->style()->subControlRect(
            QStyle::CC_SpinBox, &goalOption, QStyle::SC_SpinBoxUp, goalSpin);
        const QRect stepDown = goalSpin->style()->subControlRect(
            QStyle::CC_SpinBox, &goalOption, QStyle::SC_SpinBoxDown, goalSpin);
        QVERIFY(!stepUp.isEmpty() && !stepDown.isEmpty());
        QTest::mouseClick(goalSpin, Qt::LeftButton, Qt::NoModifier, stepUp.center());
        QTRY_COMPARE(goalSpin->value(), newGoal + 1);
        QCOMPARE(static_cast<int>(features.guardian()->dailyGoalMinutes()), newGoal + 1);
        QTest::mouseClick(goalSpin, Qt::LeftButton, Qt::NoModifier, stepDown.center());
        QTRY_COMPARE(goalSpin->value(), newGoal);
        const QImage goalImage = goalSpin->grab().toImage();
        for (const QRect& step : {stepUp, stepDown}) {
            int visibleArrowPixels = 0;
            const QRect center = step.adjusted(step.width() / 4, step.height() / 4,
                                               -step.width() / 4, -step.height() / 4);
            for (int y = center.top(); y <= center.bottom(); ++y) {
                for (int x = center.left(); x <= center.right(); ++x) {
                    const QColor pixel = goalImage.pixelColor(qFloor(x * goalImage.devicePixelRatio()),
                                                              qFloor(y * goalImage.devicePixelRatio()));
                    if (pixel.red() + pixel.green() + pixel.blue() < 450) ++visibleArrowPixels;
                }
            }
            QVERIFY2(visibleArrowPixels > 0, "goal stepper must have visible native arrows");
        }
        captureFullscreen(QStringLiteral("guardian-after-goal-save"));

        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 0);
        window.showFullScreen();
        QTest::qWait(50);

        auto* modeSelector = window.findChild<QComboBox*>("focusModeSelector");
        QVERIFY(modeSelector);
        modeSelector->setCurrentIndex(1);
        QVERIFY(!timer->timeSelectorAvailable());
        QSignalSpy plantClickSpy(timer, &PlantTimerWidget::sig_plantClicked);
        bool plantDialogHandled = false;
        QTimer::singleShot(0, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            const auto cards = dialog->findChildren<QPushButton*>();
            for (QPushButton* card : cards) {
                if (!card->property("plantCard").toBool()) continue;
                QVERIFY(!card->toolTip().isEmpty());
                plantDialogHandled = true;
                captureModalFullscreen(QStringLiteral("plant-selector-choose"));
                QTest::mouseClick(card, Qt::LeftButton);
                return;
            }
        });
        QTest::mouseClick(timer, Qt::LeftButton, Qt::NoModifier,
                          QPoint(timer->width() / 2, qRound(centerY)));
        QCOMPARE(plantClickSpy.count(), 1);
        QVERIFY(plantDialogHandled);
        modeSelector->setCurrentIndex(0);
        QVERIFY(timer->timeSelectorAvailable());
        QCOMPARE(timer->selectedMinutes(), uint32_t(25));

        window.activateWindow();
        window.setFocus(Qt::OtherFocusReason);
        QTest::qWait(50);
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::RUNNING));
        QVERIFY(!features.saveForBackup(controller, coins, &backupSaveError));
        QCOMPARE(controller.currentState(), FocusController::State::RUNNING);
        QVERIFY(!start->isVisible());
        QVERIFY(pause->isVisible() && pause->isEnabled());
        QVERIFY(abandon->isVisible() && abandon->isEnabled());
        QVERIFY(!timer->timeSelectorAvailable());
        captureFullscreen(QStringLiteral("focus-running"));
        controller.handleViolationDetected(QStringLiteral("test-blocked"));
        auto* warningOverlay = window.findChild<QWidget*>("warningOverlay");
        QVERIFY(warningOverlay && warningOverlay->isVisible());
        QCOMPARE(warningOverlay->geometry(), window.centralWidget()->rect());
        captureFullscreen(QStringLiteral("focus-warning"));
        focusClock.advanceSeconds(9);
        controller.handleSafeWindowDetected();
        QCOMPARE(controller.violationCount(), uint32_t(0));

        QTest::keyClick(&window, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::PAUSED));
        auto* pauseOverlay = window.findChild<QWidget*>("pauseBreakOverlay");
        auto* continueButton = window.findChild<QPushButton*>("pauseBreakContinue");
        QVERIFY(pauseOverlay && pauseOverlay->isVisible());
        QVERIFY(continueButton && continueButton->isVisible());
        captureFullscreen(QStringLiteral("focus-paused"));
        QTest::mouseClick(continueButton, Qt::LeftButton);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::RUNNING));

        QTest::keyClick(&window, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(controller.currentState(), FocusController::State::PAUSED);
        focusClock.advanceSeconds(300);
        controller.tick();
        QCOMPARE(controller.currentState(), FocusController::State::RUNNING);
        QVERIFY(!pauseOverlay->isVisible());
        QVERIFY(monitor.isMonitoring());

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
        bool abandonResultOpened = false;
        bool capturingAbandon = false;
        qApp->setProperty("forest.uiSmokeAutoClose", false);
        QTimer abandonResultCloser;
        abandonResultCloser.setInterval(1);
        QObject::connect(&abandonResultCloser, &QTimer::timeout, [&]() {
            if (capturingAbandon) return;
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog && dialog->property("testId").toString() == "focusResultDialog") {
                capturingAbandon = true;
                const auto closeResult = qScopeGuard([&]() {
                    dialog->reject();
                    capturingAbandon = false;
                });
                abandonResultOpened = true;
                auto* summary = dialog->findChild<QLabel*>("focusResultSummary");
                QVERIFY(summary && summary->text().contains(QStringLiteral("本次不发放专注奖励")));
                auto* plant = dialog->findChild<QLabel*>("focusResultPlant");
                QVERIFY(plant && !plant->pixmap().isNull());
                QVERIFY(qAlpha(plant->pixmap().toImage().pixel(0, 0)) == 0);
                auto* backdrop = window.findChild<QWidget*>("focusResultBackdrop");
                QVERIFY(backdrop && backdrop->isVisible());
                QVERIFY(!buttonByTestId(*dialog, QStringLiteral("focusAgainButton")));
                captureModalFullscreen(QStringLiteral("focus-abandoned-result"));
            }
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
        QVERIFY(abandonResultOpened);
        qApp->setProperty("forest.uiSmokeAutoClose", true);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::FAILED));
        QTRY_VERIFY(start->isVisible() && start->isEnabled());

        const auto records = database.getAllRecords();
        QCOMPARE(records.size(), size_t(1));
        QCOMPARE(records.back().status, FocusRecordStatus::Abandoned);
        QCOMPARE(records.back().tagId, smokeTagId);

        QTest::mouseClick(forestNavigation, Qt::LeftButton);
        bool recordDetailChecked = false;
        QTimer::singleShot(0, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto close = qScopeGuard([dialog] { dialog->reject(); });
            QCOMPARE(dialog->objectName(), QStringLiteral("forestRecordDetailDialog"));
            auto* content = dialog->findChild<QLabel*>("forestRecordDetailContent");
            auto* plant = dialog->findChild<QLabel*>("forestRecordDetailPlant");
            QVERIFY(content && plant);
            QVERIFY(content->wordWrap());
            QCOMPARE(content->textFormat(), Qt::PlainText);
            QVERIFY(content->text().contains(QStringLiteral("状态：放弃")));
            QVERIFY(content->text().contains(QStringLiteral("奖励：🪙 0")));
            QVERIFY(isFullyVisible(content) && isFullyVisible(plant));
            const QImage icon = plant->pixmap().toImage();
            QVERIFY(!icon.isNull());
            bool transparent = false;
            for (int y = 0; y < icon.height(); ++y) {
                for (int x = 0; x < icon.width(); ++x) {
                    const QColor pixel = icon.pixelColor(x, y);
                    transparent = transparent || pixel.alpha() == 0;
                    if (pixel.alpha() > 0) QCOMPARE(pixel.red(), pixel.green());
                }
            }
            QVERIFY(transparent);
            captureModalFullscreen(QStringLiteral("forest-record-detail-abandoned"));
            recordDetailChecked = true;
        });
        forestDashboard->focusRecordRequested(records.back().recordId);
        QVERIFY(recordDetailChecked);

        bool filtersChecked = false;
        QTimer::singleShot(0, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto close = qScopeGuard([dialog] { dialog->reject(); });
            QCOMPARE(dialog->objectName(), QStringLiteral("forestRecordFilterDialog"));
            auto* status = dialog->findChild<QComboBox*>("forestRecordFilterStatus");
            auto* apply = buttonByTestId(*dialog, QStringLiteral("forestRecordFilterApply"));
            QVERIFY(status && apply);
            status->setCurrentIndex(status->findData(1));
            QVERIFY(isFullyVisible(status) && isFullyVisible(apply));
            captureModalFullscreen(QStringLiteral("forest-record-filter"));
            QTest::mouseClick(apply, Qt::LeftButton);
            filtersChecked = true;
        });
        forestDashboard->filtersRequested();
        QVERIFY(filtersChecked);
        QCOMPARE(forestDashboard->recordFilter().terminalState, 1);
        QCOMPARE(forestDashboard->buildData().focusCount, 1);
        bool filterCancelled = false;
        QTimer::singleShot(0, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto close = qScopeGuard([dialog] { dialog->reject(); });
            auto* status = dialog->findChild<QComboBox*>("forestRecordFilterStatus");
            auto* cancel = buttonByTestId(*dialog, QStringLiteral("forestRecordFilterCancel"));
            QVERIFY(status && cancel);
            status->setCurrentIndex(status->findData(0));
            QVERIFY(cancel->isDefault());
            QTest::keyClick(dialog, Qt::Key_Escape);
            filterCancelled = true;
        });
        forestDashboard->filtersRequested();
        QVERIFY(filterCancelled);
        QCOMPARE(forestDashboard->recordFilter().terminalState, 1);
        QTimer::singleShot(0, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog) {
                auto* reset = buttonByTestId(*dialog, QStringLiteral("forestRecordFilterReset"));
                if (reset) QTest::mouseClick(reset, Qt::LeftButton);
                else dialog->reject();
            }
        });
        forestDashboard->filtersRequested();
        QVERIFY(!forestDashboard->recordFilter().isActive());
        QTest::mouseClick(homeNavigation, Qt::LeftButton);

        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_COMPARE(static_cast<int>(controller.currentState()),
                     static_cast<int>(FocusController::State::RUNNING));
        qApp->setProperty("forest.uiSmokeAutoClose", false);
        bool resultOpened = false;
        bool restOpened = false;
        QTimer resultDriver;
        resultDriver.setInterval(10);
        bool capturingResult = false;
        QObject::connect(&resultDriver, &QTimer::timeout, [&]() {
            if (capturingResult) return;
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (!resultOpened && dialog->property("testId").toString() == "focusResultDialog") {
                capturingResult = true;
                resultOpened = true;
                auto* title = dialog->findChild<QLabel*>("focusResultTitle");
                auto* summary = dialog->findChild<QLabel*>("focusResultSummary");
                auto* plant = dialog->findChild<QLabel*>("focusResultPlant");
                auto* again = buttonByTestId(*dialog, QStringLiteral("focusAgainButton"));
                QVERIFY(title && summary && plant && again);
                QVERIFY(title->wordWrap() && summary->wordWrap());
                QCOMPARE(summary->textFormat(), Qt::PlainText);
                QVERIFY(!plant->accessibleName().isEmpty());
                QVERIFY(!again->isDefault());
                QVERIFY(isFullyVisible(title) && isFullyVisible(summary) && isFullyVisible(again));
                captureModalFullscreen(QStringLiteral("focus-result"));
                auto* restButton = buttonByTestId(*dialog, QStringLiteral("focusRestButton"));
                QVERIFY(restButton);
                QTest::mouseClick(restButton, Qt::LeftButton);
                capturingResult = false;
            } else if (!restOpened && dialog->property("testId").toString() == "focusRestDialog") {
                capturingResult = true;
                restOpened = true;
                auto* remaining = dialog->findChild<QLabel*>("focusRestRemaining");
                QVERIFY(remaining && remaining->text() == "05:00");
                auto* description = dialog->findChild<QLabel*>("focusRestDescription");
                auto* returnButton = buttonByTestId(*dialog, QStringLiteral("focusRestReturnButton"));
                QVERIFY(description && description->wordWrap());
                QVERIFY(returnButton && returnButton->isDefault());
                QVERIFY(isFullyVisible(remaining) && isFullyVisible(description) && isFullyVisible(returnButton));
                captureModalFullscreen(QStringLiteral("focus-rest"));
                focusClock.advanceSeconds(300);
                capturingResult = false;
            }
        });
        QTimer modalTimeout;
        modalTimeout.setSingleShot(true);
        QObject::connect(&modalTimeout, &QTimer::timeout, [&]() {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        resultDriver.start();
        modalTimeout.start(10000);
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
        for (int second = 0; second < 25 * 60 &&
             controller.currentState() == FocusController::State::RUNNING; ++second) {
            focusClock.advanceSeconds(1);
            controller.tick();
        }
        modalCloser.stop();
        resultDriver.stop();
        modalTimeout.stop();
        QVERIFY(resultOpened && restOpened);
        QVERIFY(!window.findChild<QWidget*>("focusResultBackdrop"));
        qApp->setProperty("forest.uiSmokeAutoClose", true);
        QCOMPARE(static_cast<int>(controller.currentState()),
                 static_cast<int>(FocusController::State::SUCCESS));
        QTRY_VERIFY(start->isVisible() && start->isEnabled());
        QVERIFY(timer->timeSelectorAvailable());
        QCOMPARE(timer->selectedMinutes(), uint32_t(25));

        const auto completedRecords = database.getAllRecords();
        QCOMPARE(completedRecords.size(), size_t(2));
        QCOMPARE(completedRecords.back().status, FocusRecordStatus::Success);
        QCOMPARE(completedRecords.back().actualSeconds, uint32_t(1500));
        QCOMPARE(completedRecords.back().tagId, smokeTagId);

        QTest::mouseClick(challengeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 7);
        QTest::qWait(50);
        const auto completedTasks = ChallengeRewardService::progress(QDate::currentDate(), {}, completedRecords);
        QCOMPARE(completedTasks.tasks[1].progress, completedTasks.tasks[1].target);
        const QPoint completedChest = challengeActionCenter("taskReward", 1);
        QVERIFY(challenge->rect().contains(completedChest));
        const uint32_t beforeTaskClaim = coins.balance();
        captureFullscreen(QStringLiteral("challenge-task-available"));
        closeMessageBox();
        clickChallengePoint(completedChest);
        QTRY_COMPARE(coins.balance(), beforeTaskClaim + static_cast<uint32_t>(completedTasks.tasks[1].coins));
        const QString completedTaskKey = ChallengeRewardService::claimKey(
            ChallengeRewardService::Kind::Daily, 1, QDate::currentDate());
        QVERIFY(coins.hasChallengeReward(completedTaskKey));
        const uint32_t afterTaskClaim = coins.balance();
        closeMessageBox();
        clickChallengePoint(completedChest);
        QCOMPARE(coins.balance(), afterTaskClaim);
        captureFullscreen(QStringLiteral("challenge-task-claimed"));
        QScrollArea* challengeScroll = nullptr;
        for (QWidget* ancestor = challenge->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            if ((challengeScroll = qobject_cast<QScrollArea*>(ancestor))) break;
        }
        QVERIFY(challengeScroll);
        QCOMPARE(challengeScroll->horizontalScrollBar()->maximum(), 0);
        challengeScroll->verticalScrollBar()->setValue(challengeScroll->verticalScrollBar()->maximum());
        captureFullscreen(QStringLiteral("challenge-tasks-bottom"));
        challengeScroll->verticalScrollBar()->setValue(0);
        QTest::mouseClick(guardianNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 8);
        captureFullscreen(QStringLiteral("guardian-after-focus"));
        auto* goalEditor = goalSpin->findChild<QLineEdit*>();
        QVERIFY(goalEditor);
        goalEditor->setFocus();
        goalEditor->selectAll();
        captureFullscreen(QStringLiteral("guardian-goal-selected"));
        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 0);

        window.activateWindow();
        window.setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(window.isActiveWindow());
        QTest::qWait(100);
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(window.isEnabled());
        QShortcut* startShortcut = nullptr;
        for (auto* shortcut : window.findChildren<QShortcut*>()) {
            if (shortcut->key() == QKeySequence(QStringLiteral("Ctrl+Alt+F"))) startShortcut = shortcut;
        }
        QVERIFY(startShortcut && startShortcut->isEnabled());
        QSignalSpy restarted(startShortcut, &QShortcut::activated);
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(restarted.count(), 1);
        QTRY_COMPARE(controller.currentState(), FocusController::State::RUNNING);
        QCOMPARE(database.count(), uint32_t(3));
        database.failNextTerminalApplyForTesting();
        closeMessageBox();
        controller.abandonFocus();
        QCOMPARE(controller.currentState(), FocusController::State::SAVE_FAILED);
        QVERIFY(!timer->timeSelectorAvailable());
        QVERIFY(start->isVisible() && start->isEnabled());
        QCOMPARE(start->text(), QStringLiteral("重试保存"));
        QVERIFY(!abandon->isVisible());
        QVERIFY(!features.saveForBackup(controller, coins, &backupSaveError));
        window.showFullScreen();
        captureFullscreen(QStringLiteral("focus-terminal-save-failed"));
        const auto frozenSeconds = controller.actualSeconds();
        controller.tick();
        QCOMPARE(controller.actualSeconds(), frozenSeconds);
        database.failNextTerminalApplyForTesting();
        closeMessageBox();
        window.close();
        QVERIFY(window.isVisible());
        QVERIFY(controller.hasPendingFinalization());
        auto* tray = window.findChild<QSystemTrayIcon*>();
        QVERIFY(tray && tray->contextMenu());
        auto* quitAction = tray->contextMenu()->findChild<QAction*>("trayQuitAction");
        QVERIFY(quitAction);
        database.failNextTerminalApplyForTesting();
        closeMessageBox();
        quitAction->trigger();
        QVERIFY(window.isVisible());
        QVERIFY(controller.hasPendingFinalization());
        database.failNextTerminalApplyForTesting();
        closeMessageBox();
        QTest::mouseClick(start, Qt::LeftButton);
        QVERIFY(controller.hasPendingFinalization());
        QCOMPARE(database.count(), uint32_t(3));
        modalCloser.start();
        window.activateWindow();
        window.setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(window.isActiveWindow());
        QTest::qWait(100);
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
        modalCloser.stop();
        QCOMPARE(controller.currentState(), FocusController::State::FAILED);
        QVERIFY(!controller.hasPendingFinalization());
        const auto retriedRecord = database.readById(2);
        QVERIFY(retriedRecord);
        QCOMPARE(retriedRecord->status, FocusRecordStatus::Abandoned);
        QCOMPARE(retriedRecord->actualSeconds, frozenSeconds);

        // Keyboard uses the same page signals and services as pointer actions.
        timer->clearFocus();
        timer->setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(timer->hasFocus());
        QCOMPARE(timer->accessibleName(), QStringLiteral("项目标签"));
        QSignalSpy keyboardTag(timer, &PlantTimerWidget::sig_tagClicked);
        QTimer::singleShot(0, [&] {
            QVERIFY(QApplication::activeModalWidget());
            captureModalFullscreen(QStringLiteral("keyboard-project-tags"));
            QTest::keyClick(QApplication::activeModalWidget(), Qt::Key_Escape);
        });
        QTest::keyClick(timer, Qt::Key_Return);
        QCOMPARE(keyboardTag.count(), 1);
        QTRY_VERIFY(timer->hasFocus());
        QTest::keyClick(timer, Qt::Key_Tab);
        QCOMPARE(timer->accessibleName(), QStringLiteral("选择植物"));
        QSignalSpy keyboardPlant(timer, &PlantTimerWidget::sig_plantClicked);
        QTimer::singleShot(0, [&] {
            QVERIFY(QApplication::activeModalWidget());
            captureModalFullscreen(QStringLiteral("keyboard-plant-selector"));
            QTest::keyClick(QApplication::activeModalWidget(), Qt::Key_Escape);
        });
        QTest::keyClick(timer, Qt::Key_Space);
        QCOMPARE(keyboardPlant.count(), 1);
        QTRY_VERIFY(timer->hasFocus());
        QTest::keyClick(timer, Qt::Key_Tab);
        QCOMPARE(timer->accessibleName(), QStringLiteral("专注时长"));
        const uint32_t beforeKeyboardTime = timer->selectedMinutes();
        QTest::keyClick(timer, Qt::Key_Right);
        QCOMPARE(timer->selectedMinutes(), beforeKeyboardTime + 1);
        QTest::keyClick(timer, Qt::Key_Left);
        QCOMPARE(timer->selectedMinutes(), beforeKeyboardTime);
        captureFullscreen(QStringLiteral("keyboard-timer-focus"));
        QTest::keyClick(timer, Qt::Key_Backtab);
        QCOMPARE(timer->accessibleName(), QStringLiteral("选择植物"));

        QTest::mouseClick(forestNavigation, Qt::LeftButton);
        QTest::qWait(100);
        auto* keyboardMonth = forestDashboard->findChild<QToolButton*>("forestKeyboardAction3");
        QVERIFY(keyboardMonth && keyboardMonth->isVisible());
        QCOMPARE(keyboardMonth->accessibleName(), QStringLiteral("月统计"));
        auto* monthInterface = QAccessible::queryAccessibleInterface(keyboardMonth);
        QVERIFY(monthInterface && monthInterface->role() == QAccessible::Button);
        keyboardMonth->setFocus();
        QTest::keyClick(keyboardMonth, Qt::Key_Return);
        QCOMPARE(forestDashboard->periodMode_, ForestDashboardWidget::PeriodMode::Month);
        captureFullscreen(QStringLiteral("keyboard-forest-month"));
        auto* keyboardDay = forestDashboard->findChild<QToolButton*>("forestKeyboardAction1");
        keyboardDay->setFocus();
        QTest::keyClick(keyboardDay, Qt::Key_Space);
        QCOMPARE(forestDashboard->periodMode_, ForestDashboardWidget::PeriodMode::Day);

        QTest::mouseClick(challengeNavigation, Qt::LeftButton);
        QTest::qWait(100);
        auto* keyboardResource = challenge->findChild<QToolButton*>("challengeKeyboardAction0");
        QVERIFY(keyboardResource && !keyboardResource->accessibleName().isEmpty());
        keyboardResource->setFocus();
        QTimer::singleShot(0, [&] {
            auto* modal = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(modal && modal->defaultButton() == modal->button(QMessageBox::No));
            captureModalFullscreen(QStringLiteral("keyboard-coin-confirm"));
            QTest::keyClick(modal, Qt::Key_Return);
        });
        QTest::keyClick(keyboardResource, Qt::Key_Return);
        QCOMPARE(pageStack->currentIndex(), 7);
        QVERIFY(!window.findChild<QWidget*>("forestModalBackdrop"));

        const auto popupKeyboardCheck = [&](bool action) {
            QTimer::singleShot(0, [&] {
                auto* modal = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                QVERIFY(modal && modal->textFormat() == Qt::PlainText);
                QVERIFY(!modal->mask().isEmpty());
                QVERIFY(modal->defaultButton() && modal->defaultButton()->text() == QStringLiteral("关闭"));
                captureModalFullscreen(QStringLiteral("keyboard-action-detail"));
                QTest::keyClick(modal, Qt::Key_Return);
            });
            return DialogPresenter::detailWithAction(&window, QStringLiteral("详情与操作"),
                QStringLiteral("保留当前页面和数据。只有明确选择主要操作才会跳转，回车和关闭均返回当前页面。"),
                action ? QStringLiteral("前往专注") : QStringLiteral("查看详情"));
        };
        QVERIFY(!popupKeyboardCheck(true));

        keyboardResource->setFocus();
        QTRY_VERIFY(keyboardResource->hasFocus());
        QTimer::singleShot(0, [&] {
            auto* chooser = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            QVERIFY(chooser && chooser->testOption(QFileDialog::DontUseNativeDialog));
            QVERIFY(chooser->windowFlags() & Qt::FramelessWindowHint);
            captureModalFullscreen(QStringLiteral("backup-directory-chooser"));
            QTest::keyClick(chooser, Qt::Key_Escape);
        });
        QVERIFY(DialogPresenter::chooseDirectory(&window, QStringLiteral("选择备份保存位置")).isEmpty());
        QTRY_VERIFY(keyboardResource->hasFocus());
        QTimer::singleShot(0, [&] {
            auto* chooser = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            QVERIFY(chooser && chooser->acceptMode() == QFileDialog::AcceptSave);
            captureModalFullscreen(QStringLiteral("export-file-chooser"));
            QTest::keyClick(chooser, Qt::Key_Escape);
        });
        QVERIFY(DialogPresenter::chooseSaveFile(&window, QStringLiteral("导出专注记录"),
            QDir(smokeSandbox_.path()).filePath("focus.csv"), QStringLiteral("CSV 文件 (*.csv)")).isEmpty());

        auto* accessibilityScale = window.findChild<QComboBox*>("accessibilityFontScale");
        auto* reducedMotion = window.findChild<QCheckBox*>("accessibilityReducedMotion");
        auto* highContrast = window.findChild<QCheckBox*>("accessibilityHighContrast");
        QVERIFY(accessibilityScale && reducedMotion && highContrast);
        accessibilityScale->setCurrentIndex(accessibilityScale->findData(125));
        reducedMotion->setChecked(true);
        highContrast->setChecked(true);
        QCOMPARE(UserPreferences::instance().accessibilityOptions().fontScalePercent, 125);
        QVERIFY(UserPreferences::instance().accessibilityOptions().reducedMotion);
        QCOMPARE(AppStyle::scaledFont(QFont("Microsoft YaHei", 12)).pointSizeF(), qreal(15.0));
        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 0);
        captureFullscreen(QStringLiteral("accessible-focus-home"));
        verifyHomeLayout();
        for (const auto& pageCheck : pageChecks) {
            auto* navigation = window.findChild<QPushButton*>(pageCheck.first);
            QTest::mouseClick(navigation, Qt::LeftButton);
            QTRY_COMPARE(pageStack->currentIndex(), pageCheck.second);
            captureFullscreen(QStringLiteral("accessible-%1").arg(pageCheck.first));
            if (pageCheck.first == QStringLiteral("navForest")) {
                auto* scroll = window.findChild<QScrollArea*>("forestScroll");
                QVERIFY(scroll);
                QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
                captureFullscreen(QStringLiteral("forest-statistics-max-font-bottom"));
                scroll->verticalScrollBar()->setValue(0);
            }
            if (pageCheck.first == QStringLiteral("navChallenges")) {
                QCOMPARE(challengeScroll->horizontalScrollBar()->maximum(), 0);
                const auto actions = challenge->findChildren<QToolButton*>();
                for (QToolButton* button : actions) {
                    if (button->isHidden()) continue;
                    QVERIFY(!button->toolTip().isEmpty());
                    QVERIFY(challenge->rect().contains(button->geometry()));
                    if (button->property("challengeAction").toString() == QStringLiteral("taskReward")) {
                        const int index = button->property("challengeIndex").toInt();
                        const QPoint arrow = challengeActionCenter("taskAction", index);
                        QVERIFY(!button->geometry().contains(arrow));
                    }
                }
                challengeScroll->verticalScrollBar()->setValue(challengeScroll->verticalScrollBar()->maximum());
                captureFullscreen(QStringLiteral("challenge-max-font-bottom"));
                challengeScroll->verticalScrollBar()->setValue(0);
            }
            if (pageCheck.first == QStringLiteral("navGuardian")) {
                QVERIFY(goalSpin->parentWidget()->rect().contains(goalSpin->geometry()));
                QVERIFY(goalSpin->height() >= goalSpin->fontMetrics().height() + 12);
                goalSpin->setValue(600);
                QCOMPARE(goalSpin->text(), QStringLiteral("600 分钟"));
                captureFullscreen(QStringLiteral("guardian-max-font-maximum-goal"));
                goalSpin->setValue(newGoal);
            }
            if (pageCheck.first == QStringLiteral("navFriends")) {
                friendTabs->setCurrentIndex(0);
                QTest::qWait(50);
                QVERIFY(friendSearch->isVisible());
                QVERIFY(friendSearch->parentWidget()->rect().contains(friendSearch->geometry()));
                QVERIFY(friendTabs->rect().contains(friendTabs->tabBar()->geometry()));
                captureFullscreen(QStringLiteral("friends-max-font-browse"));
                QTimer::singleShot(0, [&] {
                    auto* modal = window.findChild<QDialog*>("forestOverviewDialog");
                    QVERIFY(modal);
                    auto* scroll = modal->findChild<QScrollArea*>("forestOverviewTagScroll");
                    QVERIFY(scroll && scroll->horizontalScrollBar()->maximum() == 0);
                    for (auto* option : modal->findChildren<QRadioButton*>()) {
                        QVERIFY(option->height() >= option->fontMetrics().height() + 16);
                        QVERIFY(!option->toolTip().isEmpty());
                    }
                    captureModalFullscreen(QStringLiteral("forest-tags-max-font"));
                    QTest::keyClick(modal, Qt::Key_Escape);
                });
                forestDashboard->overviewRequested();
                QTimer::singleShot(0, [&] {
                    QVERIFY(QApplication::activeModalWidget());
                    captureModalFullscreen(QStringLiteral("plant-selector-max-font"));
                    QTest::keyClick(QApplication::activeModalWidget(), Qt::Key_Escape);
                });
                timer->sig_plantClicked();
            }
            if (pageCheck.first == QStringLiteral("navAchievements") || pageCheck.first == QStringLiteral("navGacha")) {
                QWidget* content = window.findChild<QWidget*>(pageCheck.first == QStringLiteral("navAchievements")
                    ? "achievementContent" : "gachaVariants");
                QVERIFY(content);
                if (pageCheck.first == QStringLiteral("navGacha")) {
                    verifyVariantRows(content, 2);
                    auto* digPanel = window.findChild<QFrame*>("gachaDigPanel");
                    QCOMPARE(digPanel->width(), standardGachaDigWidth);
                    verifyAccessibleButton(buttonByTestId(window, QStringLiteral("gachaPullButton")), digPanel);
                    QCOMPARE(window.findChild<QScrollArea*>("gachaWorkspaceScroll")->horizontalScrollBar()->maximum(), 0);
                }
                QRect previousCard;
                for (QFrame* card : content->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
                    if (!card->isVisible()) continue;
                    QVERIFY(card->height() >= (pageCheck.first == QStringLiteral("navAchievements") ? 124 : 116));
                    QVERIFY(!previousCard.intersects(card->geometry()));
                    for (QLabel* label : card->findChildren<QLabel*>()) {
                        if (label->objectName() != QStringLiteral("achievementIcon")) QVERIFY(labelTextFits(label));
                    }
                    previousCard = card->geometry();
                }
            }
            navigation->setFocus();
            if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(navigation->graphicsEffect())) {
                QCOMPARE(effect->opacity(), qreal(1.0));
            }
        }
        QTest::mouseClick(settingsNavigation, Qt::LeftButton);
        settingsCategories->setCurrentRow(0);
        QTest::qWait(100);
        auto* focusSettingsScroll = qobject_cast<QScrollArea*>(settingsStack->currentWidget());
        QVERIFY(focusSettingsScroll && focusSettingsScroll->horizontalScrollBar()->maximum() == 0);
        captureFullscreen(QStringLiteral("accessible-focus-settings"));
        for (int category : {1, 3}) {
            settingsCategories->setCurrentRow(category);
            QTest::qWait(100);
            auto* section = qobject_cast<QScrollArea*>(settingsStack->currentWidget());
            QVERIFY(section && section->horizontalScrollBar()->maximum() == 0);
            QVERIFY(section->widget()->width() <= 920);
            captureFullscreen(QStringLiteral("settings-%1-max-font").arg(category));
        }
        settingsCategories->setCurrentRow(2);
        QTest::qWait(100);
        QCOMPARE(accessibilityScale->font().pixelSize(), 18);
        auto* appearanceScroll = qobject_cast<QScrollArea*>(settingsStack->currentWidget());
        QVERIFY(appearanceScroll && appearanceScroll->horizontalScrollBar()->maximum() == 0);
        captureFullscreen(QStringLiteral("accessible-settings-max-font"));
        QVERIFY(reducedMotion->text().endsWith(QStringLiteral("（开）")));
        QVERIFY(highContrast->text().endsWith(QStringLiteral("（开）")));
        QTimer::singleShot(0, [&] {
            auto* modal = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(modal);
            captureModalFullscreen(QStringLiteral("accessible-dialog-max-font"));
            QTest::keyClick(modal, Qt::Key_Escape);
        });
        DialogPresenter::information(&window, QStringLiteral("设置已保存"),
            QStringLiteral("文字大小、焦点和安静体验已应用。此提示使用统一弹窗；关闭后返回当前页面，不改变专注记录或奖励。"));
        auto* menuToggle = window.findChild<QPushButton*>("btnToggle");
        auto* sidebar = window.findChild<QWidget*>("sidebarWidget");
        QVERIFY(menuToggle && sidebar);
        QTest::mouseClick(menuToggle, Qt::LeftButton);
        QTRY_COMPARE(sidebar->width(), 60);
        QVERIFY(!menuToggle->toolTip().isEmpty());
        captureFullscreen(QStringLiteral("accessible-sidebar-collapsed"));
        QTest::mouseClick(menuToggle, Qt::LeftButton);
        QTRY_COMPARE(sidebar->width(), 180);
        accessibilityScale->setCurrentIndex(accessibilityScale->findData(100));
        reducedMotion->setChecked(false);
        highContrast->setChecked(false);
        QCOMPARE(accessibilityScale->font().pixelSize(), 14);
    }

    QVERIFY(features.save());
    database.close();
    users.close();
}

void UiSmokeTest::terminalStatisticsFullscreen()
{
    const auto today = QDate::currentDate();
    std::vector<FocusRecord> records;
    const auto selectedPlant = ChallengeRewardService::progress(today, {}, {}).plantType;
    for (uint32_t i = 0; i < 7; ++i) {
        FocusRecord record;
        record.recordId = i;
        record.tagId = i % 4 + 1;
        record.plantType = selectedPlant;
        record.startTimestamp = static_cast<uint64_t>(QDateTime(today, QTime(10 + i, 0), Qt::LocalTime).toSecsSinceEpoch());
        record.actualSeconds = 3600;
        record.growthStage = i < 4 ? 3 : 4;
        record.status = i < 4 ? FocusRecordStatus::Success : i == 4 ? FocusRecordStatus::Failed : FocusRecordStatus::Abandoned;
        if (i == 6) std::memcpy(record.reserved, "INT1", 4);
        records.push_back(record);
    }
    const QString configuredDirectory = qEnvironmentVariable("FOREST_UI_SCREENSHOT_DIR");
    const QString directory = configuredDirectory.isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).filePath("ui-fullscreen-snapshots")
        : configuredDirectory;
    QVERIFY(QDir().mkpath(directory));
    if (manifestPath_.isEmpty()) {
        manifestPath_ = QDir(directory).filePath(QStringLiteral("manifest.txt"));
    }
    QFile manifest(manifestPath_);
    QVERIFY(manifest.open(QIODevice::Append | QIODevice::Text));
    QTextStream stream(&manifest);
    writeManifestRecord(stream, QJsonObject{{"type", "data-fixture"}, {"fixtureId", "stage4-terminal-v1"},
        {"recordCount", 7}, {"source", "synthetic terminal records; no user data"}});

    ForestDashboardWidget forest;
    forest.setRecords(records);
    forest.setTagNames({{1, QStringLiteral("学习")}, {2, QStringLiteral("阅读")},
                        {3, QStringLiteral("项目")}, {4, QStringLiteral("练习")}});
    forest.showFullScreen();
    activateTestWindow(forest);
    QVERIFY(QTest::qWaitForWindowExposed(&forest));
    QTRY_VERIFY(forest.isFullScreen());
    QTest::qWait(150);
    QTRY_VERIFY(isExpectedWindowForeground(forest));
    if (shouldCaptureScreenshot(QStringLiteral("forest-terminal-categories"))) {
        const QPixmap all = forest.screen()->grabWindow(0);
        QVERIFY(isExpectedWindowForeground(forest));
        QVERIFY(all.save(QDir(directory).filePath("forest-terminal-categories.png")));
        writeScreenshotManifest(stream, "forest-terminal-categories", forest, all, true);
    }
    QSignalSpy requested(&forest, &ForestDashboardWidget::focusRecordRequested);
    for (const auto entry : {qMakePair(1, uint32_t(5)), qMakePair(2, uint32_t(4)), qMakePair(3, uint32_t(6))}) {
        ForestDashboardWidget::RecordFilter filter;
        filter.terminalState = entry.first;
        forest.setRecordFilter(filter);
        QTest::qWait(50);
        requested.clear();
        for (int y = 130; y < 450 && requested.isEmpty(); y += 12)
            for (int x = 50; x < forest.width() * 0.55 && requested.isEmpty(); x += 12)
                QTest::mouseClick(&forest, Qt::LeftButton, Qt::NoModifier, QPoint(x, y));
        QVERIFY2(!requested.isEmpty(), "each filtered terminal plant remains clickable");
        QCOMPARE(requested.first().first().toUInt(), entry.second);
    }
    forest.close();

    auto* challenge = new ChallengeDashboardWidget;
    challenge->setSnapshot(50, {}, records);
    std::unique_ptr<QWidget> challengeHost(DashboardPageHost::create(
        challenge, QStringLiteral("terminalChallengeHost"), QString(), true));
    challengeHost->showFullScreen();
    activateTestWindow(*challengeHost);
    QVERIFY(QTest::qWaitForWindowExposed(challengeHost.get()));
    QTRY_VERIFY(challengeHost->isFullScreen());
    QCOMPARE(challengeHost->size(), challengeHost->screen()->geometry().size());
    auto* challengeScroll = challengeHost->findChild<QScrollArea*>();
    QVERIFY(challengeScroll);
    QCOMPARE(challengeScroll->horizontalScrollBar()->maximum(), 0);
    QTest::qWait(150);
    QTRY_VERIFY(isExpectedWindowForeground(*challengeHost));
    if (shouldCaptureScreenshot(QStringLiteral("challenge-real-task-semantics"))) {
        const QPixmap taskImage = challengeHost->screen()->grabWindow(0);
        QVERIFY(isExpectedWindowForeground(*challengeHost));
        QVERIFY(taskImage.save(QDir(directory).filePath("challenge-real-task-semantics.png")));
        writeScreenshotManifest(stream, "challenge-real-task-semantics", *challengeHost, taskImage, true);
    }
    challengeHost->close();
}

void UiSmokeTest::forestIslandPaginationFullscreen()
{
    const QVector<PlantDefinition>& catalog = PlantCatalog::all();
    QVERIFY(!catalog.isEmpty());

    const QDate today = QDate::currentDate();
    const qint64 startTimestamp =
        QDateTime(today, QTime(9, 0), Qt::LocalTime).toSecsSinceEpoch();
    std::vector<FocusRecord> records;
    records.reserve(10);
    std::set<uint32_t> representedPlants;
    int successCount = 0;
    int abandonedCount = 0;
    int failedCount = 0;
    for (uint32_t i = 0; i < 10; ++i) {
        FocusRecord record;
        record.recordId = 100 + i;
        record.plantType =
            catalog.at(static_cast<int>(i % static_cast<uint32_t>(catalog.size()))).type;
        record.plannedMinutes = 25;
        record.actualSeconds = 60 + i;
        record.startTimestamp = static_cast<uint64_t>(startTimestamp + i * 60);
        record.growthStage = 3;
        record.tagId = 700 + i;
        switch (i % 3) {
        case 0:
            record.status = FocusRecordStatus::Success;
            ++successCount;
            break;
        case 1:
            record.status = FocusRecordStatus::Abandoned;
            ++abandonedCount;
            break;
        default:
            record.status = FocusRecordStatus::Failed;
            ++failedCount;
            break;
        }
        representedPlants.insert(record.plantType);
        records.push_back(record);
    }
    QCOMPARE(successCount, 4);
    QCOMPARE(abandonedCount, 3);
    QCOMPARE(failedCount, 3);
    for (const PlantDefinition& plant : catalog) {
        QVERIFY(representedPlants.count(plant.type) > 0);
    }

    const QString configuredDirectory = qEnvironmentVariable("FOREST_UI_SCREENSHOT_DIR");
    const QString directory = configuredDirectory.isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).filePath("ui-fullscreen-snapshots")
        : configuredDirectory;
    QVERIFY(QDir().mkpath(directory));
    if (manifestPath_.isEmpty()) {
        manifestPath_ = QDir(directory).filePath(QStringLiteral("manifest.txt"));
    }
    QFile manifest(manifestPath_);
    QVERIFY2(manifest.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text),
             qPrintable(manifest.errorString()));
    QTextStream stream(&manifest);
    writeManifestRecord(stream, QJsonObject{
        {QStringLiteral("type"), QStringLiteral("run")},
        {QStringLiteral("format"), QStringLiteral("forest-ui-smoke-manifest-v2")},
        {QStringLiteral("runUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("screenshotFixtureId"), QStringLiteral("ui-smoke-v1")},
        {QStringLiteral("revision"), QString::fromUtf8(ForestBuild::revision)},
        {QStringLiteral("sourceFingerprint"), QString::fromUtf8(ForestBuild::sourceFingerprint)}
    });
    writeManifestRecord(stream, QJsonObject{
        {QStringLiteral("type"), QStringLiteral("data-fixture")},
        {QStringLiteral("fixtureId"), QStringLiteral("forest-island-pagination-v1")},
        {QStringLiteral("recordCount"), 10},
        {QStringLiteral("source"), QStringLiteral("synthetic records; no user data")}
    });

    ForestDashboardWidget forest;
    forest.setRecords(records);
    forest.setTagNames({{700, QStringLiteral("用于检查长项目名称的换行与省略效果的专注标签")}});
    forest.showFullScreen();
    activateTestWindow(forest);
    QVERIFY(QTest::qWaitForWindowExposed(&forest));
    QTRY_VERIFY(forest.isFullScreen());
    QTest::qWait(100);

    auto* previous = forest.findChild<QToolButton*>(QStringLiteral("forestIslandPrevious"));
    auto* next = forest.findChild<QToolButton*>(QStringLiteral("forestIslandNext"));
    auto* pageLabel = forest.findChild<QLabel*>(QStringLiteral("forestIslandPage"));
    auto* summary = forest.findChild<QLabel*>(QStringLiteral("forestIslandSummary"));
    QVERIFY(previous);
    QVERIFY(next);
    QVERIFY(pageLabel);
    QVERIFY(summary);

    const auto capturePage = [&forest, &directory, &stream](const QString& name) {
        if (!shouldCaptureScreenshot(name)) return;
        QTest::qWait(50);
        QVERIFY(forest.isFullScreen());
        QVERIFY(forest.screen());
        QCOMPARE(forest.size(), forest.screen()->geometry().size());
        QVERIFY(isExpectedWindowForeground(forest));
        const QPixmap screenshot = forest.screen()->grabWindow(0);
        QVERIFY(isExpectedWindowForeground(forest));
        QVERIFY(!screenshot.isNull());
        QVERIFY(screenshot.save(QDir(directory).filePath(name + QStringLiteral(".png"))));
        writeScreenshotManifest(stream, name, forest, screenshot, true);
    };

    QCOMPARE(forest.islandPage_, 0);
    QCOMPARE(static_cast<int>(forest.islandPlantHitRects_.size()), 9);
    QVERIFY(!previous->isEnabled());
    QVERIFY(next->isEnabled());
    QCOMPARE(pageLabel->text(), QStringLiteral("1 / 2 页"));
    QVERIFY(summary->text().contains(QStringLiteral("共 10 棵")));

    std::set<uint32_t> expectedFirstPageIds;
    for (uint32_t id = 101; id <= 109; ++id) {
        expectedFirstPageIds.insert(id);
    }
    std::set<uint32_t> reachedRecordIds;
    for (const auto& hit : forest.islandPlantHitRects_) {
        QVERIFY(expectedFirstPageIds.count(hit.second) == 1);
        QVERIFY(reachedRecordIds.insert(hit.second).second);
    }
    QVERIFY(reachedRecordIds == expectedFirstPageIds);
    capturePage(QStringLiteral("forest-island-page-1"));

    const auto hoverHit = forest.islandPlantHitRects_.front();
    const auto moveOverForest = [&forest](const QPoint& position) {
        QMouseEvent move(QEvent::MouseMove, position, forest.mapToGlobal(position),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&forest, &move);
    };
    moveOverForest(hoverHit.first.center().toPoint());
    QTRY_COMPARE(forest.hoveredRecordId_, hoverHit.second);
    QVERIFY(forest.toolTip().contains(QStringLiteral("秒")));
    QCOMPARE(forest.cursor().shape(), Qt::PointingHandCursor);
    capturePage(QStringLiteral("forest-island-hover"));
    moveOverForest(QPoint(10, 10));
    QTRY_COMPARE(forest.hoveredRecordId_, std::numeric_limits<uint32_t>::max());
    QVERIFY(forest.toolTip().isEmpty());
    moveOverForest(forest.prevDateRect_.center().toPoint());
    QTRY_VERIFY(forest.toolTip().contains(QStringLiteral("上一个")));
    QCOMPARE(forest.cursor().shape(), Qt::PointingHandCursor);
    moveOverForest(QPoint(10, 10));

    QSignalSpy requested(&forest, &ForestDashboardWidget::focusRecordRequested);
    QVERIFY(requested.isValid());
    for (const auto& hit : forest.islandPlantHitRects_) {
        QVERIFY(forest.rect().contains(hit.first.center().toPoint()));
        requested.clear();
        QTest::mouseClick(&forest, Qt::LeftButton, Qt::NoModifier,
                          hit.first.center().toPoint());
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(requested.takeFirst().at(0).toUInt(), hit.second);
    }

    const int focusCount = forest.buildData().focusCount;
    const uint64_t totalSeconds = forest.buildData().totalSeconds;
    QCOMPARE(focusCount, 10);
    QCOMPARE(totalSeconds, uint64_t(258));
    QTest::mouseClick(next, Qt::LeftButton);
    QTRY_COMPARE(forest.islandPage_, 1);
    QTRY_COMPARE(static_cast<int>(forest.islandPlantHitRects_.size()), 1);
    QVERIFY(previous->isEnabled());
    QVERIFY(!next->isEnabled());
    QCOMPARE(pageLabel->text(), QStringLiteral("2 / 2 页"));
    QVERIFY(summary->text().contains(QStringLiteral("共 10 棵")));
    const auto lastHit = forest.islandPlantHitRects_.at(0);
    QCOMPARE(lastHit.second, uint32_t(100));
    QVERIFY(reachedRecordIds.insert(lastHit.second).second);
    QCOMPARE(reachedRecordIds.size(), records.size());
    for (const FocusRecord& record : records) {
        QVERIFY(reachedRecordIds.count(record.recordId) == 1);
    }
    requested.clear();
    QTest::mouseClick(&forest, Qt::LeftButton, Qt::NoModifier,
                      lastHit.first.center().toPoint());
    QTRY_COMPARE(requested.count(), 1);
    QCOMPARE(requested.takeFirst().at(0).toUInt(), uint32_t(100));
    QCOMPARE(forest.buildData().focusCount, focusCount);
    QCOMPARE(forest.buildData().totalSeconds, totalSeconds);
    capturePage(QStringLiteral("forest-island-page-2"));

    previous->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(previous->hasFocus());
    QTest::keyClick(previous, Qt::Key_Space);
    QTRY_COMPARE(forest.islandPage_, 0);
    QCOMPARE(forest.buildData().focusCount, focusCount);
    QCOMPARE(forest.buildData().totalSeconds, totalSeconds);
    next->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(next->hasFocus());
    QTest::keyClick(next, Qt::Key_Space);
    QTRY_COMPARE(forest.islandPage_, 1);
    QCOMPARE(forest.buildData().focusCount, focusCount);
    QCOMPARE(forest.buildData().totalSeconds, totalSeconds);

    ForestDashboardWidget::RecordFilter singleRecordFilter;
    singleRecordFilter.tagId = records.front().tagId;
    forest.setRecordFilter(singleRecordFilter);
    QTRY_COMPARE(forest.islandPage_, 0);
    QTRY_COMPARE(static_cast<int>(forest.islandPlantHitRects_.size()), 1);
    QCOMPARE(forest.islandPlantHitRects_.at(0).second, records.front().recordId);
    QVERIFY(!previous->isEnabled());
    QVERIFY(!next->isEnabled());
    QCOMPARE(pageLabel->text(), QStringLiteral("1 / 1 页"));
    QVERIFY(summary->text().contains(QStringLiteral("共 1 棵")));
    forest.setRecords({});
    QVERIFY(forest.islandPlantHitRects_.isEmpty());
    QTRY_VERIFY(summary->text().contains(QStringLiteral("暂无专注植物")));
    QVERIFY(!previous->isEnabled());
    QVERIFY(!next->isEnabled());
    capturePage(QStringLiteral("forest-island-empty"));
    stream.flush();
    forest.close();
}

void UiSmokeTest::baselineFixture()
{
    struct RestoreSmokePreferences {
        QString directory;
        ~RestoreSmokePreferences()
        {
            UserPreferences& preferences = UserPreferences::instance();
            preferences.configure(directory);
            preferences.load();
        }
    } restore{smokeUserDirectory()};

    QTemporaryDir fixtureSandbox;
    QVERIFY2(fixtureSandbox.isValid(), "Baseline fixture needs an independent temporary sandbox");
    QVERIFY(QDir::cleanPath(fixtureSandbox.path()) != QDir::cleanPath(smokeSandbox_.path()));

    const QString userStorePath = QDir(fixtureSandbox.path()).filePath(QStringLiteral("users.dat"));
    {
        UserManager users(userStorePath.toStdString());
        QVERIFY(users.open());
        QVERIFY(users.registerUser("baseline-user-1", "fixture-password"));
        QVERIFY(users.registerUser("baseline-user-2", "fixture-password"));
        QCOMPARE(static_cast<int>(users.getAllUsers().size()), BaselineFixture::kAccountCount);
        users.close();
    }
    UserManager reopenedUsers(userStorePath.toStdString());
    QVERIFY(reopenedUsers.open());
    const auto accounts = reopenedUsers.getAllUsers();
    QCOMPARE(static_cast<int>(accounts.size()), BaselineFixture::kAccountCount);
    QCOMPARE(accounts[0].first, uint32_t(1));
    QCOMPARE(accounts[1].first, uint32_t(2));

    const QString longChineseLabel =
        QStringLiteral("阶段零基线专注历史验证标签_包含较长中文名称和跨年月数据");
    const QDate claimedDate(2024, 6, 15);
    for (int accountIndex = 0; accountIndex < BaselineFixture::kAccountCount; ++accountIndex) {
        const QString userDir = QDir(fixtureSandbox.path()).filePath(
            QStringLiteral("user_%1").arg(accountIndex + 1));
        QVERIFY(QDir().mkpath(userDir));

        TagManager tags(QDir(userDir).filePath(QStringLiteral("tags.dat")));
        QVERIFY(tags.load());
        const uint32_t tagId = tags.add(longChineseLabel, QStringLiteral("#4ECDC4"));
        QVERIFY(tags.save());
        TagManager reopenedTags(QDir(userDir).filePath(QStringLiteral("tags.dat")));
        QVERIFY(reopenedTags.load());
        QCOMPARE(reopenedTags.count(), 1);
        QCOMPARE(reopenedTags.name(tagId), longChineseLabel);

        const QString databasePath = QDir(userDir).filePath(QStringLiteral("sessions.dat"));
        {
            DatabaseManager database(databasePath.toStdString());
            QVERIFY2(database.open(), qPrintable(database.lastError()));
            QCOMPARE(database.count(), uint32_t(0));
            QVERIFY2(BaselineFixture::appendHistory(database, accountIndex, tagId),
                     qPrintable(database.lastError()));
            QCOMPARE(database.count(), uint32_t(BaselineFixture::kRecordsPerAccount));
            database.close();
        }
        DatabaseManager reopenedDatabase(databasePath.toStdString());
        QVERIFY2(reopenedDatabase.open(), qPrintable(reopenedDatabase.lastError()));
        QCOMPARE(reopenedDatabase.count(), uint32_t(BaselineFixture::kRecordsPerAccount));
        const std::vector<FocusRecord> records = reopenedDatabase.getAllRecords();
        QCOMPARE(static_cast<int>(records.size()), BaselineFixture::kRecordsPerAccount);
        int successCount = 0;
        int abandonedCount = 0;
        int failedCount = 0;
        for (const FocusRecord& record : records) {
            QCOMPARE(record.tagId, tagId);
            switch (record.status) {
            case FocusRecordStatus::Success: ++successCount; break;
            case FocusRecordStatus::Abandoned: ++abandonedCount; break;
            case FocusRecordStatus::Failed: ++failedCount; break;
            case FocusRecordStatus::Running:
                QFAIL("Baseline fixture must contain terminal records only");
            }
        }
        QCOMPARE(successCount, 167);
        QCOMPARE(abandonedCount, 167);
        QCOMPARE(failedCount, 166);
        const QDate firstDate = QDate(2024, 1, 1).addDays(accountIndex * BaselineFixture::kRecordsPerAccount);
        const QDate lastDate = firstDate.addDays(BaselineFixture::kRecordsPerAccount - 1);
        QCOMPARE(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(records.front().startTimestamp), Qt::UTC).date(),
                 firstDate);
        QCOMPARE(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(records.back().startTimestamp), Qt::UTC).date(),
                 lastDate);

        const QString walletPath = QDir(userDir).filePath(QStringLiteral("coins.dat"));
        const uint32_t expectedBalance = static_cast<uint32_t>(250 + accountIndex * 100);
        {
            CoinManager wallet;
            wallet.setDataPath(walletPath.toStdString());
            QVERIFY(wallet.load());
            QVERIFY(wallet.earn(expectedBalance));
            if (accountIndex == 1) QVERIFY(wallet.unlockPlant(1));
        }
        CoinManager reopenedWallet;
        reopenedWallet.setDataPath(walletPath.toStdString());
        QVERIFY(reopenedWallet.load());
        QCOMPARE(reopenedWallet.balance(), expectedBalance);
        QCOMPARE(reopenedWallet.isPlantUnlocked(1), accountIndex == 1);

        const QString variantsPath = QDir(userDir).filePath(QStringLiteral("variants.dat"));
        const QString variantId = accountIndex == 0 ? QStringLiteral("golden_oak")
                                                    : QStringLiteral("flame_cactus");
        {
            GachaManager variants(variantsPath);
            QVERIFY(variants.load());
            QVERIFY(variants.unlockVariant(variantId));
        }
        GachaManager reopenedVariants(variantsPath);
        QVERIFY(reopenedVariants.load());
        QCOMPARE(reopenedVariants.unlockedCount(), 1);
        QVERIFY(reopenedVariants.isUnlocked(variantId));

        UserPreferences& preferences = UserPreferences::instance();
        preferences.configure(userDir);
        QVERIFY(preferences.load());
        if (accountIndex == 0) {
            UserPreferences::ChallengeRewardState state;
            state.date = claimedDate;
            state.monthKey = claimedDate.toString(QStringLiteral("yyyyMM"));
            state.taskClaimed = {true, false, true};
            state.checkinClaimed = {false, true, false, true, false};
            state.monthlyClaimed = true;
            QVERIFY(preferences.saveChallengeRewardState(state));
        }
    }

    UserPreferences& preferences = UserPreferences::instance();
    const QVector<bool> expectedTasks = {true, false, true};
    const QVector<bool> expectedCheckins = {false, true, false, true, false};
    const QString firstUserDir = QDir(fixtureSandbox.path()).filePath(QStringLiteral("user_1"));
    preferences.configure(firstUserDir);
    QVERIFY(preferences.load());
    const auto firstClaims = preferences.challengeRewardState(claimedDate, 3, 5);
    QCOMPARE(firstClaims.taskClaimed, expectedTasks);
    QCOMPARE(firstClaims.checkinClaimed, expectedCheckins);
    QVERIFY(firstClaims.monthlyClaimed);

    const QString secondUserDir = QDir(fixtureSandbox.path()).filePath(QStringLiteral("user_2"));
    preferences.configure(secondUserDir);
    QVERIFY(preferences.load());
    const auto secondClaims = preferences.challengeRewardState(claimedDate, 3, 5);
    QCOMPARE(secondClaims.taskClaimed, QVector<bool>(3, false));
    QCOMPARE(secondClaims.checkinClaimed, QVector<bool>(5, false));
    QVERIFY(!secondClaims.monthlyClaimed);

    if (!manifestPath_.isEmpty() && QFileInfo::exists(manifestPath_)) {
        QFile manifest(manifestPath_);
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text));
        QTextStream manifestStream(&manifest);
        QJsonObject dataFixtureRecord;
        dataFixtureRecord.insert(QStringLiteral("type"), QStringLiteral("data-fixture"));
        dataFixtureRecord.insert(QStringLiteral("fixtureId"), QString::fromUtf8(BaselineFixture::kFixtureId));
        dataFixtureRecord.insert(QStringLiteral("source"), QStringLiteral("independent-QTemporaryDir"));
        dataFixtureRecord.insert(QStringLiteral("screenshots"), QJsonArray());
        dataFixtureRecord.insert(QStringLiteral("accountCount"), BaselineFixture::kAccountCount);
        dataFixtureRecord.insert(QStringLiteral("recordsPerAccount"), BaselineFixture::kRecordsPerAccount);
        dataFixtureRecord.insert(QStringLiteral("recordCount"), BaselineFixture::kRecordCount);
        dataFixtureRecord.insert(QStringLiteral("historyStartUtc"), QStringLiteral("2024-01-01T09:00:00Z"));
        dataFixtureRecord.insert(QStringLiteral("historyDateGeneration"), QStringLiteral("fixed UTC dates"));
        writeManifestRecord(manifestStream, dataFixtureRecord);
    }
}

QTEST_MAIN(UiSmokeTest)
#include "UiSmokeTest.moc"
