#include "core/AchievementEngine.h"
#include "config/UserPreferences.h"
#include "config/PlantCatalog.h"
#include "core/CoinManager.h"
#include "core/DashboardSnapshotService.h"
#include "core/FocusController.h"
#include "core/FocusResultService.h"
#include "core/FriendManager.h"
#include "core/GachaManager.h"
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
#include "ui/CommercePages.h"
#include "ui/ForestDashboardWidget.h"
#include "ui/GachaDigSiteWidget.h"
#include "ui/GuardianDashboardWidget.h"
#include "ui/LoginDialog.h"
#include "ui/MainWindow.h"
#include "ui/DialogPresenter.h"
#include "ui/PlantImageUtils.h"
#include "ui/PlantTimerWidget.h"
#include "ui/SidebarNavigation.h"
#include "ui/SettingsPage.h"
#include "ui/SproutToggle.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAccessible>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGridLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPointer>
#include <QRadioButton>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextStream>
#include <QVariantAnimation>
#include <QWindow>
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

int highContrastTransitions(const QImage& image, const QRect& area)
{
    const QRect bounds = area.intersected(image.rect());
    int transitions = 0;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left() + 1; x <= bounds.right(); ++x) {
            const QColor left = image.pixelColor(x - 1, y);
            const QColor right = image.pixelColor(x, y);
            const int difference = qAbs(left.red() - right.red())
                + qAbs(left.green() - right.green())
                + qAbs(left.blue() - right.blue())
                + qAbs(left.alpha() - right.alpha());
            if (difference > 3) ++transitions;
        }
    }
    return transitions;
}

int colorDistance(const QColor& first, const QColor& second)
{
    return qAbs(first.red() - second.red())
        + qAbs(first.green() - second.green())
        + qAbs(first.blue() - second.blue())
        + qAbs(first.alpha() - second.alpha());
}

int differentPixels(const QImage& first, const QImage& second, const QRect& area)
{
    const QRect bounds = area.intersected(first.rect()).intersected(second.rect());
    int pixels = 0;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            if (colorDistance(first.pixelColor(x, y), second.pixelColor(x, y)) > 6) ++pixels;
        }
    }
    return pixels;
}

int pixelsNearColor(const QImage& image, const QColor& expected, int tolerance)
{
    int pixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor actual = image.pixelColor(x, y);
            const int distance = qAbs(actual.red() - expected.red())
                + qAbs(actual.green() - expected.green())
                + qAbs(actual.blue() - expected.blue());
            if (distance <= tolerance) ++pixels;
        }
    }
    return pixels;
}

QImage renderDigSite(const QPixmap& plant, const QColor& accent)
{
    GachaDigSiteWidget site;
    site.resize(600, 360);
    site.setDiscoveredPlant(plant, accent);
    site.ensurePolished();

    QImage rendered(site.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    site.render(&rendered);
    return rendered.convertToFormat(QImage::Format_ARGB32);
}

QImage renderEmptyDigSite()
{
    GachaDigSiteWidget site;
    site.resize(600, 360);
    site.clearDiscoveredPlant();
    site.ensurePolished();

    QImage rendered(site.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    site.render(&rendered);
    return rendered.convertToFormat(QImage::Format_ARGB32);
}

QPixmap faintPlantReference(const QPixmap& plant)
{
    QImage reference = plant.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < reference.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(reference.scanLine(y));
        for (int x = 0; x < reference.width(); ++x)
            line[x] = qAlpha(line[x]) > 8 ? qRgba(0, 0, 0, 9) : qRgba(0, 0, 0, 0);
    }
    return QPixmap::fromImage(reference);
}

int paintedBackgroundPixels(QWidget* widget)
{
    QImage background(widget->size(), QImage::Format_ARGB32_Premultiplied);
    background.fill(Qt::transparent);
    QPainter painter(&background);
    widget->render(&painter, QPoint(), QRegion(), QWidget::DrawWindowBackground);
    painter.end();

    int painted = 0;
    for (int y = 0; y < background.height(); ++y) {
        for (int x = 0; x < background.width(); ++x) {
            if (background.pixelColor(x, y).alpha() > 8) ++painted;
        }
    }
    return painted;
}

} // namespace

class UiSmokeTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void focusIndicatorFollowsKeyboardModality();
    void pointerFocusDoesNotPaintEnclosingFrames();
    void plantTimerUsesSimpleAmbientVisuals();
    void plantTimerDrawsTimeWithoutCard();
    void plantTimerKeepsGuideOutsideRing();
    void plantTimerSemanticActionsStayVisuallyTransparent();
    void gachaDigSiteBlendsSyntheticRootWithoutSoilOcclusion();
    void gachaDigSiteTreatsTransparentPlantAsEmpty();
    void gachaDigSiteBlendsSixPlantShapes();
    void gachaDigSiteKeepsSoilSurfaceContinuous();
    void plantTimerSupportsKeyboardAndSemanticActions();
    void paintedDashboardsExposeKeyboardActions();
    void forestPlantHitTargetsFollowVisibleSprites();
    void forestPlantHoverUsesCachedStaticLayer();
    void reducedMotionStopsLayoutAndGuardianAnimation();
    void appStyleHonorsAccessibilityScale();
    void sproutToggleSupportsDirectAndAccessibleInteraction();
    void settingsPageUsesOneContinuousSurface();
    void settingsPageExposesGroupedAccessibleControls();
    void mainWindowFocusAndNavigation();
};

void UiSmokeTest::initTestCase()
{
    AppStyle::installFocusVisibility(*qApp);
}

void UiSmokeTest::focusIndicatorFollowsKeyboardModality()
{
    AppStyle::installFocusVisibility(*qApp);

    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* button = new QPushButton(QStringLiteral("测试操作"), &window);
    auto* input = new QLineEdit(&window);
    button->setFocusPolicy(Qt::StrongFocus);
    input->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(button);
    layout->addWidget(input);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY(button->hasFocus());
    QVERIFY(!AppStyle::keyboardFocusVisible(button));

    input->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(AppStyle::keyboardFocusVisible(input));

    QTest::mouseClick(input, Qt::LeftButton);
    QVERIFY(input->hasFocus());
    QVERIFY(!AppStyle::keyboardFocusVisible(input));
    QTest::keyClicks(input, QStringLiteral("forest"));
    QCOMPARE(input->text(), QStringLiteral("forest"));

    button->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(AppStyle::keyboardFocusVisible(button));
    QTest::mousePress(button, Qt::LeftButton);
    QVERIFY(!AppStyle::keyboardFocusVisible(button));
    QTest::mouseRelease(button, Qt::LeftButton);
}

void UiSmokeTest::pointerFocusDoesNotPaintEnclosingFrames()
{
    AppStyle::installFocusVisibility(*qApp);
    const QString originalStyleSheet = qApp->styleSheet();
    qApp->setStyleSheet(AppStyle::styleSheet(100, false));
    const auto render = [](QWidget& widget) {
        QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        widget.render(&image);
        return image.convertToFormat(QImage::Format_ARGB32);
    };

    QPushButton button(QStringLiteral("测试操作"));
    button.resize(180, 52);
    button.show();
    QVERIFY(QTest::qWaitForWindowExposed(&button));
    button.clearFocus();
    QCoreApplication::processEvents();
    const QImage idleButton = render(button);
    button.setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(differentPixels(idleButton, render(button), button.rect()), 0);

    qApp->setStyleSheet(AppStyle::styleSheet(125, true));
    button.clearFocus();
    QCoreApplication::processEvents();
    const QImage idleHighContrastButton = render(button);
    button.setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(differentPixels(idleHighContrastButton, render(button), button.rect()), 0);

    qApp->setStyleSheet(AppStyle::styleSheet(100, false));
    PlantTimerWidget timer;
    timer.resize(700, 760);
    timer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&timer));
    auto* plantAction = timer.findChild<QAbstractButton*>("timerPlantAction");
    QVERIFY(plantAction);
    plantAction->clearFocus();
    QCoreApplication::processEvents();
    const QImage idleTimer = render(timer);
    plantAction->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(differentPixels(idleTimer, render(timer), timer.rect()), 0);

    SettingsPage settings;
    settings.resize(1180, 760);
    settings.setPlantOptions({{QStringLiteral("橡树"), 1}}, 1);
    settings.show();
    QVERIFY(QTest::qWaitForWindowExposed(&settings));
    auto* plantRow = settings.findChild<QWidget*>("focusPlantRow");
    QVERIFY(plantRow);
    plantRow->clearFocus();
    QCoreApplication::processEvents();
    const QImage idleSettings = render(settings);
    plantRow->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(differentPixels(idleSettings, render(settings), settings.rect()), 0);

    qApp->setStyleSheet(originalStyleSheet);
}

void UiSmokeTest::sproutToggleSupportsDirectAndAccessibleInteraction()
{
    SproutToggle toggle;
    toggle.setAccessibleName(QStringLiteral("测试状态"));
    toggle.resize(toggle.sizeHint());
    toggle.show();
    QVERIFY(QTest::qWaitForWindowExposed(&toggle));

    QSignalSpy toggledSpy(&toggle, &QCheckBox::toggled);
    QCOMPARE(toggle.statusText(), QStringLiteral("未启用"));
    QVERIFY(!toggle.isChecked());

    QTest::mousePress(&toggle, Qt::LeftButton, Qt::NoModifier, toggle.rect().center());
    QTest::mouseMove(&toggle, QPoint(-8, -8));
    QTest::mouseRelease(&toggle, Qt::LeftButton, Qt::NoModifier, QPoint(-8, -8));
    QVERIFY(!toggle.isChecked());
    QCOMPARE(toggledSpy.count(), 0);

    QTest::mouseClick(&toggle, Qt::LeftButton, Qt::NoModifier, toggle.rect().center());
    QVERIFY(toggle.isChecked());
    QCOMPARE(toggle.statusText(), QStringLiteral("已启用"));
    const auto animations = toggle.findChildren<QVariantAnimation*>();
    QCOMPARE(animations.size(), 1);
    QCOMPARE(animations.first()->state(), QAbstractAnimation::Running);
    QTRY_COMPARE(animations.first()->state(), QAbstractAnimation::Stopped);

    toggle.setFocus();
    QTest::keyClick(&toggle, Qt::Key_Space);
    QVERIFY(!toggle.isChecked());
    QCOMPARE(animations.first()->state(), QAbstractAnimation::Stopped);
    QTest::keyClick(&toggle, Qt::Key_Return);
    QVERIFY(toggle.isChecked());
    QCOMPARE(animations.first()->state(), QAbstractAnimation::Stopped);
    QTest::keyClick(&toggle, Qt::Key_Enter);
    QVERIFY(!toggle.isChecked());
    QCOMPARE(animations.first()->state(), QAbstractAnimation::Stopped);

    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&toggle);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::CheckBox);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("测试状态"));

    toggle.setReducedMotion(true);
    toggle.setChecked(true);
    for (QVariantAnimation* animation : animations)
        QVERIFY(animation->state() != QAbstractAnimation::Running);
}

void UiSmokeTest::settingsPageExposesGroupedAccessibleControls()
{
    const QString originalStyleSheet = qApp->styleSheet();
    qApp->setStyleSheet(AppStyle::styleSheet(100, false));
    SettingsPage page;
    page.resize(1180, 760);
    page.setPlantOptions({{QStringLiteral("橡树"), 1}, {QStringLiteral("松树"), 2}}, 1);
    page.setTagOptions({{QStringLiteral("无标签"), 0}, {QStringLiteral("学习"), 7}}, 0);
    page.setBlacklist({QStringLiteral("chrome.exe")});
    page.setDataSummary(QStringLiteral("安装模式"), 3);

    SettingsPage::FocusSettings focus;
    focus.minutes = 25;
    focus.plantType = 1;
    focus.tagId = 0;
    focus.stopwatch = false;
    focus.deepFocus = true;
    focus.allowPause = true;
    focus.oath = QStringLiteral("完成今天的阅读");
    page.setFocusSettings(focus);

    SettingsPage::AccessibilitySettings accessibility;
    accessibility.fontScalePercent = 100;
    accessibility.reducedMotion = false;
    accessibility.highContrast = false;
    page.setAccessibilitySettings(accessibility);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));

    auto* categories = page.findChild<QListWidget*>("settingsCategoryNav");
    auto* stack = page.findChild<QStackedWidget*>("settingsCategoryStack");
    QVERIFY(categories);
    QVERIFY(stack);
    QCOMPARE(categories->count(), 4);
    QCOMPARE(stack->count(), 4);
    categories->setFocus();
    QTest::keyClick(categories, Qt::Key_Down);
    QCOMPARE(categories->currentRow(), 1);
    QCOMPARE(stack->currentIndex(), 1);
    categories->setCurrentRow(0);

    const QStringList interactiveNames = {
        QStringLiteral("focusPlantRow"),
        QStringLiteral("focusModeRow"),
        QStringLiteral("focusTagRow"),
        QStringLiteral("focusMinutesSelector"),
        QStringLiteral("focusDeepToggle"),
        QStringLiteral("focusPauseToggle"),
        QStringLiteral("guardianGoalSelector"),
        QStringLiteral("accessibilityFontScale"),
        QStringLiteral("accessibilityReducedMotion"),
        QStringLiteral("accessibilityHighContrast"),
        QStringLiteral("settingsBlacklistInput"),
        QStringLiteral("settingsBlacklistAdd"),
        QStringLiteral("settingsBlacklistRemove"),
        QStringLiteral("settingsBackupAction"),
        QStringLiteral("settingsRestoreAction"),
        QStringLiteral("settingsExportAction"),
        QStringLiteral("settingsOpenDataAction")
    };
    for (const QString& objectName : interactiveNames) {
        auto* widget = page.findChild<QWidget*>(objectName);
        QVERIFY2(widget, qPrintable(QStringLiteral("missing settings control %1").arg(objectName)));
        QVERIFY2(widget->focusPolicy() != Qt::NoFocus,
                 qPrintable(QStringLiteral("%1 is not keyboard focusable").arg(objectName)));
        QVERIFY2(!widget->accessibleName().trimmed().isEmpty(),
                 qPrintable(QStringLiteral("%1 has no accessible name").arg(objectName)));
    }

    auto* modeCombo = page.findChild<QComboBox*>("focusModeSelector");
    auto* plantCombo = page.findChild<QComboBox*>("focusPlantSelector");
    auto* plantRow = page.findChild<QWidget*>("focusPlantRow");
    auto* minutesSpin = page.findChild<QSpinBox*>("focusMinutesSelector");
    QVERIFY(modeCombo);
    QVERIFY(plantCombo);
    QVERIFY(plantRow);
    QVERIFY(minutesSpin);
    QTest::mouseMove(&page, QPoint(page.width() - 2, page.height() - 2));
    QCoreApplication::processEvents();
    const QPoint feedbackSample(plantRow->width() / 2, plantRow->height() / 2);
    const QColor idleFeedback = plantRow->grab().toImage().pixelColor(feedbackSample);
    QTest::mousePress(plantRow, Qt::LeftButton, Qt::NoModifier,
                      QPoint(30, plantRow->rect().center().y()));
    QTest::mouseMove(plantRow, QPoint(-4, -4));
    QCoreApplication::processEvents();
    QCOMPARE(plantRow->grab().toImage().pixelColor(feedbackSample), idleFeedback);
    QTest::mouseRelease(plantRow, Qt::LeftButton, Qt::NoModifier, QPoint(-4, -4));
    QTest::mouseClick(plantRow, Qt::LeftButton, Qt::NoModifier,
                      QPoint(30, plantRow->rect().center().y()));
    QTRY_VERIFY(plantCombo->view()->isVisible());
    const QRect comboGlobal(plantCombo->mapToGlobal(QPoint(0, 0)), plantCombo->size());
    const QPoint popupTopLeft = plantCombo->view()->window()->mapToGlobal(QPoint(0, 0));
    QVERIFY(popupTopLeft.y() >= comboGlobal.top());
    QVERIFY(popupTopLeft.y() <= comboGlobal.bottom() + 8);
    plantCombo->hidePopup();
    modeCombo->setCurrentIndex(modeCombo->findData(1));
    QVERIFY(!minutesSpin->isEnabled());

    const SettingsPage::FocusSettings actual = page.focusSettings();
    QCOMPARE(actual.plantType, uint32_t(1));
    QCOMPARE(actual.tagId, uint32_t(0));
    QCOMPARE(actual.stopwatch, true);
    QCOMPARE(actual.oath, QStringLiteral("完成今天的阅读"));

    QSignalSpy backupSpy(&page, &SettingsPage::backupRequested);
    auto* backup = page.findChild<QPushButton*>("settingsBackupAction");
    QVERIFY(backup);
    QTest::mouseClick(backup, Qt::LeftButton);
    QCOMPARE(backupSpy.count(), 1);

    const QImage rendered = page.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QColor outerBackground = rendered.pixelColor(6, rendered.height() - 6);
    const QColor contentBackground = rendered.pixelColor(rendered.width() - 60, rendered.height() - 60);
    QVERIFY2(contentBackground.lightness() >= outerBackground.lightness() + 20,
             "the settings content area must paint a light structural glass layer");
    auto* dataSummary = page.findChild<QLabel*>("settingsDataSummary");
    QVERIFY(dataSummary);
    const QColor summaryText = dataSummary->palette().color(QPalette::WindowText);
    QVERIFY(summaryText.lightness() < 130);
    qApp->setStyleSheet(originalStyleSheet);
}

void UiSmokeTest::settingsPageUsesOneContinuousSurface()
{
    const QString originalStyleSheet = qApp->styleSheet();
    qApp->setStyleSheet(AppStyle::styleSheet(100, false));
    SettingsPage page;
    page.resize(1180, 760);
    page.setPlantOptions({{QStringLiteral("橡树"), 1}, {QStringLiteral("松树"), 2}}, 1);
    page.setTagOptions({{QStringLiteral("无标签"), 0}, {QStringLiteral("学习"), 7}}, 0);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));

    auto* shell = page.findChild<QFrame*>("settingsGlassShell");
    auto* rail = page.findChild<QWidget*>("settingsRail");
    auto* content = page.findChild<QFrame*>("settingsContentPanel");
    const auto groups = page.findChildren<QFrame*>(QString(), Qt::FindChildrenRecursively);
    QVERIFY(shell);
    QVERIFY(rail);
    QVERIFY(content);
    QVERIFY(paintedBackgroundPixels(shell) > shell->width() * shell->height() / 2);
    for (QFrame* group : groups) {
        if (group->property("settingsListGroup").toBool())
            QCOMPARE(paintedBackgroundPixels(group), 0);
    }
    const QImage pageImage = page.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QPoint railSample =
        rail->mapTo(&page, QPoint(rail->width() / 2, rail->height() - 20));
    const QPoint contentSample =
        content->mapTo(&page, QPoint(content->width() / 2, content->height() - 20));
    const QColor railColor = pageImage.pixelColor(railSample);
    const QColor contentColor = pageImage.pixelColor(contentSample);
    const int surfaceDifference = qAbs(railColor.red() - contentColor.red())
        + qAbs(railColor.green() - contentColor.green())
        + qAbs(railColor.blue() - contentColor.blue());
    QVERIFY2(surfaceDifference <= 24,
             qPrintable(QStringLiteral("settings rail/content surface difference is %1")
                            .arg(surfaceDifference)));

    const QStringList rowNames = {
        QStringLiteral("focusPlantRow"),
        QStringLiteral("focusModeRow"),
        QStringLiteral("focusTagRow")
    };
    const QStringList comboNames = {
        QStringLiteral("focusPlantSelector"),
        QStringLiteral("focusModeSelector"),
        QStringLiteral("focusTagSelector")
    };
    for (int index = 0; index < rowNames.size(); ++index) {
        auto* row = page.findChild<QWidget*>(rowNames[index]);
        auto* combo = page.findChild<QComboBox*>(comboNames[index]);
        QVERIFY(row);
        QVERIFY(combo);
        QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier,
                          QPoint(30, row->rect().center().y()));
        QTRY_VERIFY(combo->view()->isVisible());
        combo->hidePopup();
    }

    auto* deepFocus = page.findChild<SproutToggle*>("focusDeepToggle");
    QVERIFY(deepFocus);
    QWidget* deepFocusRow = deepFocus->parentWidget();
    QVERIFY(deepFocusRow);
    const bool originalDeepFocus = deepFocus->isChecked();
    QTest::mouseClick(deepFocusRow, Qt::LeftButton, Qt::NoModifier,
                      QPoint(30, deepFocusRow->rect().center().y()));
    QCOMPARE(deepFocus->isChecked(), !originalDeepFocus);

    qApp->setStyleSheet(AppStyle::styleSheet(100, true));
    QCoreApplication::processEvents();
    const QImage highContrastImage =
        page.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QPoint shellTopLeft = shell->mapTo(&page, QPoint(0, 0));
    const QColor outsideCorner =
        highContrastImage.pixelColor(shellTopLeft + QPoint(-2, 2));
    const QColor roundedCorner =
        highContrastImage.pixelColor(shellTopLeft + QPoint(2, 2));
    const int cornerDifference = qAbs(outsideCorner.red() - roundedCorner.red())
        + qAbs(outsideCorner.green() - roundedCorner.green())
        + qAbs(outsideCorner.blue() - roundedCorner.blue());
    QVERIFY2(cornerDifference <= 24,
             qPrintable(QStringLiteral("settings rounded corner leaked by %1").arg(cornerDifference)));

    qApp->setStyleSheet(originalStyleSheet);
}

void UiSmokeTest::plantTimerUsesSimpleAmbientVisuals()
{
    PlantTimerWidget timer;
    timer.resize(700, 760);
    timer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&timer));

    const QImage rendered = timer.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!rendered.isNull());
    const int decorativeEdges = highContrastTransitions(rendered, QRect(0, 8, rendered.width(), 48));
    QVERIFY2(decorativeEdges <= 8,
             qPrintable(QStringLiteral("simple ambient area has %1 decorative edges")
                            .arg(decorativeEdges)));
}

void UiSmokeTest::plantTimerDrawsTimeWithoutCard()
{
    PlantTimerWidget timer;
    timer.setAttribute(Qt::WA_StyledBackground, true);
    timer.setStyleSheet(QStringLiteral("background-color:#4EA488;"));
    timer.resize(700, 620);
    timer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&timer));

    const QImage rendered = timer.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!rendered.isNull());
    const int cardEdgeTransitions = highContrastTransitions(rendered, QRect(170, 550, 54, 100));
    QVERIFY2(cardEdgeTransitions <= 8,
             qPrintable(QStringLiteral("time area still has %1 card-edge transitions")
                            .arg(cardEdgeTransitions)));
}

int sampledColorCount(const QImage& image)
{
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); y += 3) {
        for (int x = 0; x < image.width(); x += 3) {
            const QColor color = image.pixelColor(x, y);
            colors.insert(qRgb(color.red() / 8 * 8, color.green() / 8 * 8, color.blue() / 8 * 8));
        }
    }
    return colors.size();
}

void UiSmokeTest::plantTimerKeepsGuideOutsideRing()
{
    const auto renderTimer = [](bool stopwatch) {
        PlantTimerWidget timer;
        timer.setAttribute(Qt::WA_StyledBackground, true);
        timer.setStyleSheet(QStringLiteral("background-color:#4EA488;"));
        timer.resize(700, 760);
        timer.setSelectedMinutes(120);
        timer.setDisplaySeconds(0, stopwatch);
        timer.ensurePolished();

        QImage rendered(timer.size(), QImage::Format_ARGB32);
        rendered.fill(Qt::transparent);
        timer.render(&rendered);
        return rendered;
    };

    const QImage withGuide = renderTimer(false);
    const QImage withoutGuide = renderTimer(true);
    const QString screenshotDirectory = QDir(QCoreApplication::applicationDirPath())
                                            .filePath("ui-fullscreen-snapshots");
    QVERIFY(QDir().mkpath(screenshotDirectory));
    QVERIFY(withGuide.save(QDir(screenshotDirectory).filePath("timer-guide-outside-ring.png")));
    const int guidePixels = differentPixels(withGuide, withoutGuide, QRect(250, 50, 200, 45));
    QVERIFY2(guidePixels >= 1200,
             qPrintable(QStringLiteral("only %1 guide pixels are visible outside the timer ring")
                            .arg(guidePixels)));
}

void UiSmokeTest::gachaDigSiteBlendsSyntheticRootWithoutSoilOcclusion()
{
    const QColor stemColor("#26D94B");
    const QColor baseColor("#FF00A8");
    QPixmap syntheticPlant(120, 120);
    syntheticPlant.fill(Qt::transparent);
    {
        QPainter painter(&syntheticPlant);
        painter.setPen(Qt::NoPen);
        painter.setBrush(baseColor);
        painter.drawRoundedRect(QRectF(10, 91, 100, 29), 8, 8);
        painter.setBrush(stemColor);
        painter.drawRoundedRect(QRectF(45, 10, 30, 110), 12, 12);
    }

    const QImage rendered = renderDigSite(syntheticPlant, QColor("#8ED0AF"));
    const int visibleStemPixels = pixelsNearColor(rendered, stemColor, 28);
    const int visibleRootPixels = pixelsNearColor(rendered.copy(QRect(278, 191, 44, 34)), stemColor, 28);
    const int exposedBasePixels = pixelsNearColor(rendered, baseColor, 28);
    const QString screenshotDirectory = QDir(QCoreApplication::applicationDirPath())
                                            .filePath("ui-fullscreen-snapshots");
    QVERIFY(QDir().mkpath(screenshotDirectory));
    QVERIFY(rendered.save(QDir(screenshotDirectory).filePath("gacha-plant-blend-synthetic.png")));
    QVERIFY2(visibleStemPixels >= 900,
             qPrintable(QStringLiteral("plant body has only %1 visible pixels")
                            .arg(visibleStemPixels)));
    QVERIFY2(visibleRootPixels >= 100,
             qPrintable(QStringLiteral("land occlusion leaves only %1 visible root pixels at contact")
                            .arg(visibleRootPixels)));
    QVERIFY2(exposedBasePixels <= 120,
             qPrintable(QStringLiteral("plant-local blending leaves %1 magenta base pixels exposed")
                            .arg(exposedBasePixels)));
}

void UiSmokeTest::gachaDigSiteTreatsTransparentPlantAsEmpty()
{
    QPixmap transparentPlant(120, 120);
    transparentPlant.fill(Qt::transparent);
    const QImage rendered = renderDigSite(transparentPlant, QColor("#8ED0AF"));
    QCOMPARE(rendered, renderEmptyDigSite());
}

void UiSmokeTest::gachaDigSiteBlendsSixPlantShapes()
{
    const struct PlantCase {
        const char* id;
        QColor accent;
    } plants[] = {{"oak", QColor("#69B966")}, {"pine", QColor("#4E9C63")},
                  {"rose", QColor("#D18457")}, {"ginkgo", QColor("#E4BE45")},
                  {"sunflower", QColor("#F1C85B")}, {"cactus", QColor("#C87145")}};
    QImage contactSheet(1200, 1080, QImage::Format_ARGB32_Premultiplied);
    contactSheet.fill(Qt::transparent);
    QPainter sheetPainter(&contactSheet);

    for (int index = 0; index < 6; ++index) {
        const QString plantId = QString::fromLatin1(plants[index].id);
        const QPixmap plant = PlantImageUtils::loadPlantIcon(
            QStringLiteral(":/images/%1_final.png").arg(plantId));
        QVERIFY2(!plant.isNull(), qPrintable(QStringLiteral("missing final image for %1").arg(plantId)));

        const QImage rendered = renderDigSite(plant, plants[index].accent);
        const QImage reference = renderDigSite(faintPlantReference(plant), plants[index].accent);
        const int visibleBodyPixels = differentPixels(rendered, reference, QRect(195, 34, 210, 154));
        const int visibleRootPixels = differentPixels(rendered, reference, QRect(272, 198, 56, 31));
        const int exposedLeftBasePixels = differentPixels(rendered, reference, QRect(220, 198, 48, 31));
        const int exposedRightBasePixels = differentPixels(rendered, reference, QRect(332, 198, 48, 31));
        QVERIFY2(visibleBodyPixels >= 900,
                 qPrintable(QStringLiteral("%1 body has only %2 visible pixels")
                                .arg(plantId)
                                .arg(visibleBodyPixels)));
        QVERIFY2(visibleRootPixels >= 120,
                 qPrintable(QStringLiteral("%1 has only %2 visible root pixels at the soil contact")
                                .arg(plantId)
                                .arg(visibleRootPixels)));
        QVERIFY2(exposedLeftBasePixels + exposedRightBasePixels <= 300,
                 qPrintable(QStringLiteral("%1 leaves %2 lateral base pixels beside the root")
                                .arg(plantId)
                                .arg(exposedLeftBasePixels + exposedRightBasePixels)));
        sheetPainter.drawImage(QPoint((index % 2) * 600, (index / 2) * 360), rendered);
    }
    sheetPainter.end();

    const QString screenshotDirectory = QDir(QCoreApplication::applicationDirPath())
                                            .filePath("ui-fullscreen-snapshots");
    QVERIFY(QDir().mkpath(screenshotDirectory));
    QVERIFY(contactSheet.save(QDir(screenshotDirectory).filePath("gacha-plant-blend-six-species.png")));
}

void UiSmokeTest::gachaDigSiteKeepsSoilSurfaceContinuous()
{
    QPixmap syntheticPlant(100, 100);
    syntheticPlant.fill(Qt::transparent);
    {
        QPainter painter(&syntheticPlant);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#26D94B"));
        painter.drawRoundedRect(QRectF(35, 8, 30, 78), 12, 12);
        painter.setBrush(QColor("#FF00A8"));
        painter.drawRoundedRect(QRectF(12, 80, 76, 20), 7, 7);
    }

    const QImage rendered = renderDigSite(syntheticPlant, QColor("#8ED0AF"));
    const QColor leftSoil = rendered.pixelColor(230, 214);
    const QColor rightSoil = rendered.pixelColor(370, 214);
    const int leftDifference = colorDistance(leftSoil, rendered.pixelColor(180, 214));
    const int rightDifference = colorDistance(rightSoil, rendered.pixelColor(430, 214));
    int darkSeamPixels = 0;
    const QColor oldRimColor("#835136");
    for (int y = 195; y <= 222; ++y) {
        for (int x = 200; x <= 400; ++x) {
            if (x > 268 && x < 332) continue;
            if (colorDistance(rendered.pixelColor(x, y), oldRimColor) <= 36) ++darkSeamPixels;
        }
    }
    QVERIFY2(qMax(leftDifference, rightDifference) <= 12,
             qPrintable(QStringLiteral("soil beside the root differs by %1/%2 with %3 dark seam pixels")
                            .arg(leftDifference)
                            .arg(rightDifference)
                            .arg(darkSeamPixels)));
    QVERIFY2(darkSeamPixels <= 10,
             qPrintable(QStringLiteral("soil beside the root has %1 dark internal seam pixels")
                            .arg(darkSeamPixels)));
}

void UiSmokeTest::plantTimerSupportsKeyboardAndSemanticActions()
{
    PlantTimerWidget timer;
    timer.resize(700, 760);
    timer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&timer));

    timer.setFocus();
    const uint32_t initialMinutes = timer.selectedMinutes();
    QTest::keyClick(&timer, Qt::Key_Right);
    QCOMPARE(timer.selectedMinutes(), initialMinutes + 1);
    QTest::keyClick(&timer, Qt::Key_PageUp);
    QCOMPARE(timer.selectedMinutes(), initialMinutes + 6);

    auto* tagAction = timer.findChild<QAbstractButton*>("timerTagAction");
    auto* plantAction = timer.findChild<QAbstractButton*>("timerPlantAction");
    QVERIFY(tagAction);
    QVERIFY(plantAction);
    QVERIFY(tagAction->focusPolicy() != Qt::NoFocus);
    QVERIFY(plantAction->focusPolicy() != Qt::NoFocus);
    QVERIFY(!tagAction->accessibleName().isEmpty());
    QVERIFY(!plantAction->accessibleName().isEmpty());
    QVERIFY(!tagAction->toolTip().isEmpty());
    QVERIFY(!plantAction->toolTip().isEmpty());

    QSignalSpy tagClicked(&timer, &PlantTimerWidget::sig_tagClicked);
    tagAction->setFocus();
    QTest::keyClick(tagAction, Qt::Key_Return);
    QTest::keyClick(tagAction, Qt::Key_Space);
    QCOMPARE(tagClicked.count(), 2);
    QTest::mousePress(tagAction, Qt::LeftButton, Qt::NoModifier, tagAction->rect().center());
    QTest::mouseRelease(tagAction, Qt::LeftButton, Qt::NoModifier, QPoint(-4, -4));
    QCOMPARE(tagClicked.count(), 2);

    QSignalSpy plantClicked(&timer, &PlantTimerWidget::sig_plantClicked);
    plantAction->setFocus();
    QTest::keyClick(plantAction, Qt::Key_Return);
    QTest::keyClick(plantAction, Qt::Key_Space);
    QCOMPARE(plantClicked.count(), 2);

    QAccessibleInterface* accessibleTimer = QAccessible::queryAccessibleInterface(&timer);
    QVERIFY(accessibleTimer);
    QCOMPARE(accessibleTimer->role(), QAccessible::Slider);
    auto* valueInterface = static_cast<QAccessibleValueInterface*>(
        accessibleTimer->interface_cast(QAccessible::ValueInterface));
    QVERIFY(valueInterface);
    QCOMPARE(valueInterface->minimumValue().toInt(), 10);
    QCOMPARE(valueInterface->maximumValue().toInt(), 120);
    QCOMPARE(valueInterface->currentValue().toInt(), static_cast<int>(timer.selectedMinutes()));
    QCOMPARE(valueInterface->minimumStepSize().toInt(), 1);
}

void UiSmokeTest::paintedDashboardsExposeKeyboardActions()
{
    ForestDashboardWidget forest;
    forest.resize(1180, 760);
    const qint64 noon = QDateTime(QDate::currentDate(), QTime(12, 0)).toSecsSinceEpoch();
    std::vector<FocusRecord> records;
    for (uint32_t i = 0; i < 6; ++i) {
        FocusRecord record;
        record.recordId = i + 1;
        record.plantType = i % 6;
        record.actualSeconds = 25 * 60;
        record.startTimestamp = static_cast<uint64_t>(noon + i * 61);
        record.status = FocusRecordStatus::Success;
        records.push_back(record);
    }
    forest.setRecords(records);
    forest.show();
    QVERIFY(QTest::qWaitForWindowExposed(&forest));
    const auto forestActions = forest.findChildren<QAbstractButton*>();
    QVERIFY(forestActions.size() >= 7);
    for (QAbstractButton* action : forestActions) {
        QVERIFY(action->focusPolicy() != Qt::NoFocus);
        QVERIFY(!action->accessibleName().isEmpty());
        QVERIFY(!action->toolTip().isEmpty());
        QVERIFY(!action->geometry().isEmpty());
    }

    auto* overview = forest.findChild<QAbstractButton*>("forestOverviewAction");
    QVERIFY(overview);
    QSignalSpy overviewRequested(&forest, &ForestDashboardWidget::overviewRequested);
    overview->setFocus();
    QTest::keyClick(overview, Qt::Key_Return);
    QTest::keyClick(overview, Qt::Key_Space);
    QCOMPARE(overviewRequested.count(), 2);
    QTest::mousePress(overview, Qt::LeftButton, Qt::NoModifier, overview->rect().center());
    QTest::mouseRelease(overview, Qt::LeftButton, Qt::NoModifier, QPoint(-4, -4));
    QCOMPARE(overviewRequested.count(), 2);

    for (uint32_t i = 0; i < records.size(); ++i) {
        records[i].startTimestamp = static_cast<uint64_t>(noon + (records.size() - i) * 719);
    }
    forest.setRecords(records);
    QCoreApplication::processEvents();
    QStringList expectedPlantStack;
    for (QAbstractButton* action : forest.findChildren<QAbstractButton*>()) {
        if (action->objectName().startsWith("forestPlantRecord_")) expectedPlantStack.append(action->objectName());
    }
    std::sort(expectedPlantStack.begin(), expectedPlantStack.end(), [&forest](const QString& first, const QString& second) {
        return forest.findChild<QAbstractButton*>(first)->geometry().bottom()
            < forest.findChild<QAbstractButton*>(second)->geometry().bottom();
    });
    QStringList actualPlantStack;
    for (QObject* child : forest.children()) {
        if (child->objectName().startsWith("forestPlantRecord_")) actualPlantStack.append(child->objectName());
    }
    QCOMPARE(actualPlantStack, expectedPlantStack);

    ChallengeDashboardWidget challenge;
    challenge.resize(1180, 1080);
    challenge.show();
    QVERIFY(QTest::qWaitForWindowExposed(&challenge));
    const auto challengeActions = challenge.findChildren<QAbstractButton*>();
    QVERIFY(challengeActions.size() >= 8);
    for (QAbstractButton* action : challengeActions) {
        QVERIFY(action->focusPolicy() != Qt::NoFocus);
        QVERIFY(!action->accessibleName().isEmpty());
        QVERIFY(!action->toolTip().isEmpty());
        QVERIFY(!action->geometry().isEmpty());
    }
    auto* resourceAction = challenge.findChild<QAbstractButton*>("challengeAction_1:-1");
    auto* focusAction = challenge.findChild<QAbstractButton*>("challengeAction_14:0");
    QVERIFY(resourceAction);
    QVERIFY(focusAction);
    QSignalSpy openShop(&challenge, &ChallengeDashboardWidget::openShopRequested);
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog) dialog->button(QMessageBox::Yes)->click();
    });
    resourceAction->setFocus();
    QTest::keyClick(resourceAction, Qt::Key_Return);
    QCOMPARE(openShop.count(), 1);
    QSignalSpy openFocus(&challenge, &ChallengeDashboardWidget::openFocusRequested);
    focusAction->setFocus();
    QTest::keyClick(focusAction, Qt::Key_Return);
    QTest::keyClick(focusAction, Qt::Key_Space);
    QCOMPARE(openFocus.count(), 2);
    QTest::mousePress(focusAction, Qt::LeftButton, Qt::NoModifier, focusAction->rect().center());
    QTest::mouseRelease(focusAction, Qt::LeftButton, Qt::NoModifier, QPoint(-4, -4));
    QCOMPARE(openFocus.count(), 2);
}

void UiSmokeTest::reducedMotionStopsLayoutAndGuardianAnimation()
{
    QWidget sidebar;
    sidebar.setFixedWidth(180);
    QStackedWidget pages;
    QPushButton toggle;
    QPushButton coin;
    SidebarNavigation navigation(&sidebar, &pages, &toggle, &coin, {});
    navigation.toggle(100);
    QCOMPARE(sidebar.width(), 60);
    QVERIFY(sidebar.findChildren<QAbstractAnimation*>().isEmpty());
    navigation.toggle(100);
    QCOMPARE(sidebar.width(), 180);
    QVERIFY(sidebar.findChildren<QAbstractAnimation*>().isEmpty());

    navigation.setReducedMotion(true);
    navigation.toggle(100);
    QCOMPARE(sidebar.width(), 60);
    for (QAbstractAnimation* animation : sidebar.findChildren<QAbstractAnimation*>()) {
        QVERIFY(animation->state() != QAbstractAnimation::Running);
    }

    GuardianDashboardWidget guardian;
    guardian.resize(900, 700);
    guardian.show();
    QVERIFY(QTest::qWaitForWindowExposed(&guardian));
    guardian.setSnapshot(15, 30, 1, 2, 120);
    auto* progressAnimation = guardian.findChild<QPropertyAnimation*>();
    QVERIFY(progressAnimation);
    QCOMPARE(progressAnimation->duration(), 200);
    QCOMPARE(progressAnimation->easingCurve().type(), QEasingCurve::OutCubic);
    guardian.hide();
    QCoreApplication::processEvents();
    QCOMPARE(guardian.property("shownProgress").toReal(), 0.5);
    for (QAbstractAnimation* animation : guardian.findChildren<QAbstractAnimation*>()) {
        QVERIFY(animation->state() != QAbstractAnimation::Running);
    }

    guardian.setReducedMotion(true);
    guardian.setSnapshot(15, 30, 1, 2, 120);
    QCOMPARE(guardian.property("shownProgress").toReal(), 0.5);
    for (QAbstractAnimation* animation : guardian.findChildren<QAbstractAnimation*>()) {
        QVERIFY(animation->state() != QAbstractAnimation::Running);
    }
}

void UiSmokeTest::plantTimerSemanticActionsStayVisuallyTransparent()
{
    PlantTimerWidget timer;
    timer.resize(700, 760);
    timer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&timer));

    auto* tagAction = timer.findChild<QAbstractButton*>("timerTagAction");
    auto* plantAction = timer.findChild<QAbstractButton*>("timerPlantAction");
    QVERIFY(tagAction);
    QVERIFY(plantAction);
    tagAction->clearFocus();
    plantAction->clearFocus();
    tagAction->setAttribute(Qt::WA_UnderMouse, false);
    plantAction->setAttribute(Qt::WA_UnderMouse, false);
    timer.setFocus();
    QCoreApplication::processEvents();

    const auto renderTimer = [&timer](QWidget::RenderFlags flags) {
        QImage image(timer.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        timer.render(&image, QPoint(), QRegion(), flags);
        return image.convertToFormat(QImage::Format_ARGB32);
    };
    const QImage paintedOnly = renderTimer(QWidget::DrawWindowBackground);
    const QImage withSemanticActions = renderTimer(
        QWidget::DrawWindowBackground | QWidget::DrawChildren);
    const int leakedPixels = differentPixels(
        paintedOnly, withSemanticActions, tagAction->geometry().united(plantAction->geometry()));
    QVERIFY2(leakedPixels <= 32,
             qPrintable(QStringLiteral("透明语义按钮泄露了 %1 个可见像素").arg(leakedPixels)));
}

void UiSmokeTest::forestPlantHitTargetsFollowVisibleSprites()
{
    ForestDashboardWidget forest;
    forest.resize(1180, 760);
    const qint64 noon = QDateTime(QDate::currentDate(), QTime(12, 0)).toSecsSinceEpoch();
    std::vector<FocusRecord> records;
    for (uint32_t i = 0; i < 12; ++i) {
        FocusRecord record;
        record.recordId = i + 1;
        record.plantType = i % 6;
        record.actualSeconds = 25 * 60;
        record.startTimestamp = static_cast<uint64_t>(noon + i * 61);
        record.status = FocusRecordStatus::Success;
        records.push_back(record);
    }
    forest.setRecords(records);
    forest.show();
    QVERIFY(QTest::qWaitForWindowExposed(&forest));

    QList<QAbstractButton*> plantActions;
    for (QAbstractButton* action : forest.findChildren<QAbstractButton*>()) {
        if (action->objectName().startsWith(QStringLiteral("forestPlantRecord_"))) {
            plantActions.push_back(action);
        }
    }
    QVERIFY(plantActions.size() >= 6);

    QSignalSpy recordRequested(&forest, &ForestDashboardWidget::focusRecordRequested);
    for (QAbstractButton* action : plantActions) {
        const QVariant drawProperty = action->property("forestPlantDrawRect");
        const QVariant feedbackProperty = action->property("forestFeedbackRect");
        QVERIFY(drawProperty.isValid());
        QVERIFY(feedbackProperty.isValid());

        const QRect drawRect = drawProperty.toRect();
        const QRect feedbackRect = feedbackProperty.toRect();
        const QRect hitRect = action->geometry();
        QVERIFY(!drawRect.isEmpty());
        QCOMPARE(feedbackRect, drawRect.adjusted(-4, -4, 4, 4));
        QVERIFY(hitRect.contains(feedbackRect));
        QVERIFY(hitRect.width() >= 44);
        QVERIFY(hitRect.height() >= 44);
        QVERIFY(hitRect.width() - feedbackRect.width() <= 20);
        QVERIFY(hitRect.height() - feedbackRect.height() <= 20);

        bool idOk = false;
        const uint32_t recordId = action->objectName()
            .sliced(QStringLiteral("forestPlantRecord_").size()).toUInt(&idOk);
        QVERIFY(idOk);
        QTest::mouseClick(action, Qt::LeftButton, Qt::NoModifier, action->rect().center());
        QCOMPARE(recordRequested.count(), 1);
        QCOMPARE(recordRequested.takeFirst().at(0).toUInt(), recordId);
    }

    QAbstractButton* firstPlant = plantActions.first();
    firstPlant->setFocus();
    QTest::keyClick(firstPlant, Qt::Key_Return);
    QTest::keyClick(firstPlant, Qt::Key_Space);
    QCOMPARE(recordRequested.count(), 2);
    recordRequested.clear();
    QTest::mousePress(firstPlant, Qt::LeftButton, Qt::NoModifier, firstPlant->rect().center());
    QTest::mouseRelease(firstPlant, Qt::LeftButton, Qt::NoModifier, QPoint(-4, -4));
    QCOMPARE(recordRequested.count(), 0);

    const QString screenshotDirectory = QDir(QCoreApplication::applicationDirPath())
                                            .filePath("ui-fullscreen-snapshots");
    QVERIFY(QDir().mkpath(screenshotDirectory));
    const QSize screenshotSizes[] = {
        QSize(1366, 768), QSize(1440, 900), QSize(1920, 1080)
    };
    for (const QSize& size : screenshotSizes) {
        forest.resize(size);
        QCoreApplication::processEvents();

        firstPlant->clearFocus();
        firstPlant->setAttribute(Qt::WA_UnderMouse, false);
        forest.update();
        QCoreApplication::processEvents();
        const QString suffix = QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
        const QDir screenshots(screenshotDirectory);
        QVERIFY(forest.grab().save(screenshots.filePath(
            QStringLiteral("forest-plants-normal-%1.png").arg(suffix))));

        firstPlant->setFocus(Qt::MouseFocusReason);
        QCoreApplication::processEvents();
        QVERIFY(!AppStyle::keyboardFocusVisible(firstPlant));
        QVERIFY(forest.grab().save(screenshots.filePath(
            QStringLiteral("forest-plants-pointer-%1.png").arg(suffix))));

        firstPlant->setAttribute(Qt::WA_UnderMouse, true);
        forest.update(firstPlant->geometry().adjusted(-6, -6, 6, 6));
        QCoreApplication::processEvents();
        QVERIFY(forest.grab().save(screenshots.filePath(
            QStringLiteral("forest-plants-hover-%1.png").arg(suffix))));

        firstPlant->setAttribute(Qt::WA_UnderMouse, false);
        firstPlant->setFocus(Qt::TabFocusReason);
        QKeyEvent forestKeyboardNavigation(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
        QApplication::sendEvent(qApp, &forestKeyboardNavigation);
        QTRY_VERIFY(AppStyle::keyboardFocusVisible(firstPlant));
        QCoreApplication::processEvents();
        QVERIFY(forest.grab().save(screenshots.filePath(
            QStringLiteral("forest-plants-focus-%1.png").arg(suffix))));
    }
}

void UiSmokeTest::forestPlantHoverUsesCachedStaticLayer()
{
    ForestDashboardWidget forest;
    forest.resize(1180, 760);
    const qint64 noon = QDateTime(QDate::currentDate(), QTime(12, 0)).toSecsSinceEpoch();
    std::vector<FocusRecord> records;
    for (uint32_t i = 0; i < 12; ++i) {
        FocusRecord record;
        record.recordId = i + 1;
        record.plantType = i % 6;
        record.actualSeconds = 25 * 60;
        record.startTimestamp = static_cast<uint64_t>(noon + i * 61);
        record.status = FocusRecordStatus::Success;
        records.push_back(record);
    }
    forest.setRecords(records);
    forest.show();
    QVERIFY(QTest::qWaitForWindowExposed(&forest));

    QAbstractButton* plantAction = nullptr;
    for (QAbstractButton* action : forest.findChildren<QAbstractButton*>()) {
        if (action->objectName().startsWith(QStringLiteral("forestPlantRecord_"))) {
            plantAction = action;
            break;
        }
    }
    QVERIFY(plantAction);

    const QVariant generationProperty = forest.property("forestStaticRenderGeneration");
    const qulonglong initialGeneration = generationProperty.toULongLong();
    QElapsedTimer timer;
    timer.start();
    for (int cycle = 0; cycle < 10; ++cycle) {
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(plantAction, &enter);
        QCoreApplication::processEvents();
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(plantAction, &leave);
        QCoreApplication::processEvents();
    }
    QVERIFY2(timer.elapsed() < 500,
             qPrintable(QStringLiteral("10 次植物悬停循环耗时 %1ms").arg(timer.elapsed())));
    QVERIFY(generationProperty.isValid());
    QCOMPARE(forest.property("forestStaticRenderGeneration").toULongLong(), initialGeneration);
}

void UiSmokeTest::appStyleHonorsAccessibilityScale()
{
    const QString regular = AppStyle::styleSheet(100, false);
    const QString enlarged = AppStyle::styleSheet(125, false);
    QVERIFY2(regular != enlarged, "125 percent text scale must change the application style");
    const QString highContrast = AppStyle::styleSheet(125, true);
    QVERIFY2(highContrast != enlarged, "high contrast mode must change the application style");
}

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
        auto* plantSelector = window.findChild<QComboBox*>("focusPlantSelector");
        auto* modeSelector = window.findChild<QComboBox*>("focusModeSelector");
        auto* minutesSelector = window.findChild<QSpinBox*>("focusMinutesSelector");
        auto* settingsDeepFocus = window.findChild<SproutToggle*>("focusDeepToggle");
        auto* settingsAllowPause = window.findChild<SproutToggle*>("focusPauseToggle");
        QVERIFY(pageStack);
        QVERIFY(timer);
        QVERIFY(forestDashboard);
        QVERIFY(start);
        QVERIFY(pause);
        QVERIFY(abandon);
        QVERIFY(homeNavigation);
        QVERIFY(tagSelector);
        QVERIFY(plantSelector);
        QVERIFY(modeSelector);
        QVERIFY(minutesSelector);
        QVERIFY(settingsDeepFocus);
        QVERIFY(settingsAllowPause);
        QVERIFY(!window.findChild<QPushButton*>("navTags"));
        QCOMPARE(pageStack->currentIndex(), 0);
        QVERIFY(start->isVisible());
        for (int index = 0; index < plantSelector->count(); ++index) {
            const auto& definition = PlantCatalog::byType(plantSelector->itemData(index).toUInt());
            QCOMPARE(plantSelector->itemText(index), definition.displayName);
            QVERIFY(!plantSelector->itemText(index).contains(definition.internalName));
        }
        for (int index = 0; index < tagSelector->count(); ++index)
            QVERIFY(!tagSelector->itemText(index).contains(QString::number(tagSelector->itemData(index).toUInt())));

        timer->sig_timeSelected(42);
        QTRY_COMPARE(minutesSelector->value(), 42);
        modeSelector->setCurrentIndex(modeSelector->findData(1));
        QVERIFY(!minutesSelector->isEnabled());
        modeSelector->setCurrentIndex(modeSelector->findData(0));
        QVERIFY(minutesSelector->isEnabled());
        minutesSelector->setValue(25);

        auto* countdownMode = window.findChild<QPushButton*>("homeCountdownModeButton");
        auto* modeOverlay = window.findChild<QWidget*>("homeModeOverlay");
        auto* deepFocusOption = window.findChild<QCheckBox*>("homeDeepFocusOption");
        QVERIFY(countdownMode);
        QVERIFY(modeOverlay);
        QVERIFY(deepFocusOption);
        settingsDeepFocus->setChecked(false);
        QTRY_VERIFY(!deepFocusOption->isChecked());
        settingsDeepFocus->setChecked(true);
        QTRY_VERIFY(deepFocusOption->isChecked());
        settingsAllowPause->setChecked(false);
        QTRY_VERIFY(!UserPreferences::instance().allowPause());
        settingsAllowPause->setChecked(true);
        QTRY_VERIFY(UserPreferences::instance().allowPause());
        countdownMode->setFocus();
        QTest::mouseClick(countdownMode, Qt::LeftButton);
        QTRY_VERIFY(modeOverlay->isVisible());
        QTRY_VERIFY(deepFocusOption->hasFocus());
        QTest::keyClick(deepFocusOption, Qt::Key_Escape);
        QTRY_VERIFY(!modeOverlay->isVisible());
        QTRY_VERIFY(countdownMode->hasFocus());

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
        QVERIFY(loginDialog.testAttribute(Qt::WA_TranslucentBackground));
        loginDialog.show();
        QTest::qWait(50);
        const QPixmap loginScreenshot = loginDialog.grab();
        QVERIFY(!loginScreenshot.isNull());
        const QImage loginImage = loginScreenshot.toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(loginImage.pixelColor(0, 0).alpha() <= 8);
        QVERIFY(loginImage.pixelColor(loginImage.width() - 1, 0).alpha() <= 8);
        QVERIFY(loginImage.pixelColor(0, loginImage.height() - 1).alpha() <= 8);
        QVERIFY(loginImage.pixelColor(loginImage.width() - 1, loginImage.height() - 1).alpha() <= 8);
        QVERIFY(loginImage.pixelColor(loginImage.width() / 2, loginImage.height() / 2).alpha() >= 240);
        QVERIFY(loginScreenshot.save(QDir(screenshotDirectory).filePath(QStringLiteral("login-dialog.png"))));
        manifestStream << "login-dialog size=" << loginScreenshot.width() << "x"
                       << loginScreenshot.height() << "\n";
        for (const QSize& size : QList<QSize>{QSize(1366, 768), QSize(1440, 900), QSize(1920, 1080)}) {
            QPixmap loginCanvas(size);
            loginCanvas.fill(QColor("#58AD8F"));
            QPainter loginPainter(&loginCanvas);
            loginPainter.drawPixmap((size.width() - loginScreenshot.width()) / 2,
                                    (size.height() - loginScreenshot.height()) / 2,
                                    loginScreenshot);
            loginPainter.end();
            const QString name = QStringLiteral("login-dialog-%1x%2")
                                     .arg(size.width())
                                     .arg(size.height());
            QVERIFY(loginCanvas.save(QDir(screenshotDirectory).filePath(name + ".png")));
            manifestStream << name << " size=" << size.width() << "x" << size.height() << "\n";
        }
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
        const QList<QPair<QString, int>> pageChecks = {
            {QStringLiteral("navForest"), 1}, {QStringLiteral("navShop"), 2},
            {QStringLiteral("navAchievements"), 4}, {QStringLiteral("navGacha"), 5},
            {QStringLiteral("navFriends"), 6}, {QStringLiteral("navChallenges"), 7},
            {QStringLiteral("navGuardian"), 8}
        };
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
            auto* timerPlantFocusAction = timer->findChild<QAbstractButton*>("timerPlantAction");
            QVERIFY(timerPlantFocusAction);
            timerPlantFocusAction->setFocus(Qt::MouseFocusReason);
            QCoreApplication::processEvents();
            QVERIFY(!AppStyle::keyboardFocusVisible(timerPlantFocusAction));
            captureAtSize(QStringLiteral("focus-home-plant-pointer-%1x%2")
                              .arg(size.width()).arg(size.height()), size);
            timerPlantFocusAction->clearFocus();
            window.activateWindow();
            timerPlantFocusAction->setFocus(Qt::TabFocusReason);
            QTRY_VERIFY(timerPlantFocusAction->hasFocus());
            QKeyEvent plantKeyboardNavigation(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
            QApplication::sendEvent(qApp, &plantKeyboardNavigation);
            QTRY_VERIFY(AppStyle::keyboardFocusVisible(timerPlantFocusAction));
            timer->repaint();
            captureAtSize(QStringLiteral("focus-home-plant-keyboard-%1x%2")
                              .arg(size.width()).arg(size.height()), size);

            QTest::mouseClick(forestNavigation, Qt::LeftButton);
            QTRY_COMPARE(pageStack->currentIndex(), 1);
            const QRect forestHeader(0, 0, forestDashboard->width(), qMin(74, forestDashboard->height()));
            QVERIFY(forestDashboard->visibleRegion().contains(forestHeader));
            captureAtSize(QStringLiteral("navForest-%1x%2").arg(size.width()).arg(size.height()), size);

            for (const auto& pageCheck : pageChecks) {
                if (pageCheck.second == 1) continue;
                auto* navigation = window.findChild<QPushButton*>(pageCheck.first);
                QVERIFY(navigation);
                QTest::mouseClick(navigation, Qt::LeftButton);
                QTRY_COMPARE(pageStack->currentIndex(), pageCheck.second);
                captureAtSize(QStringLiteral("%1-%2x%3")
                                  .arg(pageCheck.first)
                                  .arg(size.width())
                                  .arg(size.height()),
                              size);
            }

            auto* settingsNavigationAtSize = window.findChild<QPushButton*>("navSettings");
            auto* settingsCategoriesAtSize = window.findChild<QListWidget*>("settingsCategoryNav");
            auto* settingsStackAtSize = window.findChild<QStackedWidget*>("settingsCategoryStack");
            QVERIFY(settingsNavigationAtSize);
            QVERIFY(settingsCategoriesAtSize);
            QVERIFY(settingsStackAtSize);
            QTest::mouseClick(settingsNavigationAtSize, Qt::LeftButton);
            QTRY_COMPARE(pageStack->currentIndex(), 3);
            for (int row = 0; row < settingsCategoriesAtSize->count(); ++row) {
                settingsCategoriesAtSize->setCurrentRow(row);
                QTRY_COMPARE(settingsStackAtSize->currentIndex(), row);
                captureAtSize(QStringLiteral("settings-%1-%2x%3")
                                  .arg(row)
                                  .arg(size.width())
                                  .arg(size.height()),
                              size);
            }
            QTest::mouseClick(settingsCategoriesAtSize->viewport(), Qt::LeftButton,
                              Qt::NoModifier,
                              settingsCategoriesAtSize->visualItemRect(
                                  settingsCategoriesAtSize->currentItem()).center());
            QVERIFY(!AppStyle::keyboardFocusVisible(settingsCategoriesAtSize));
            captureAtSize(QStringLiteral("settings-category-pointer-%1x%2")
                              .arg(size.width()).arg(size.height()), size);
            settingsCategoriesAtSize->clearFocus();
            window.activateWindow();
            settingsCategoriesAtSize->setFocus(Qt::TabFocusReason);
            QTRY_VERIFY(settingsCategoriesAtSize->hasFocus());
            QKeyEvent categoryKeyboardNavigation(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
            QApplication::sendEvent(qApp, &categoryKeyboardNavigation);
            QTRY_VERIFY(AppStyle::keyboardFocusVisible(settingsCategoriesAtSize));
            settingsCategoriesAtSize->repaint();
            captureAtSize(QStringLiteral("settings-category-keyboard-%1x%2")
                              .arg(size.width()).arg(size.height()), size);
        }
        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 0);
        window.showFullScreen();
        QTest::qWait(100);
        captureFullscreen(QStringLiteral("focus-home"));

        bool homeOverviewOpened = false;
        bool homeOverviewTranslucent = false;
        QTimer::singleShot(0, [&]() {
            auto* overviewDialog = window.findChild<QDialog*>("forestOverviewDialog");
            QVERIFY(overviewDialog);
            QVERIFY(overviewDialog->windowFlags() & Qt::FramelessWindowHint);
            homeOverviewTranslucent = overviewDialog->testAttribute(Qt::WA_TranslucentBackground);
            homeOverviewOpened = true;
            overviewDialog->reject();
        });
        timer->sig_tagClicked();
        QTRY_VERIFY(homeOverviewOpened);
        QVERIFY(homeOverviewTranslucent);
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
        for (const auto& pageCheck : pageChecks) {
            auto* navigation = window.findChild<QPushButton*>(pageCheck.first);
            QVERIFY(navigation);
            QTest::mouseClick(navigation, Qt::LeftButton);
            QTRY_COMPARE(pageStack->currentIndex(), pageCheck.second);
            if (pageCheck.second == 4) {
                const auto descriptions = window.findChildren<QLabel*>("achievementDescription");
                QVERIFY(!descriptions.isEmpty());
                for (QLabel* description : descriptions) {
                    description->ensurePolished();
                    QVERIFY(description->palette().color(QPalette::WindowText).alpha() >= 200);
                }
            }
            captureFullscreen(pageCheck.first);
        }
        QTest::mouseClick(window.findChild<QPushButton*>("navForest"), Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 1);
        bool overviewFilterApplied = false;
        bool overviewCornersTransparent = false;
        bool overviewSurfaceOpaque = false;
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
            const QImage overviewImage = overviewScreenshot.toImage().convertToFormat(QImage::Format_ARGB32);
            overviewCornersTransparent = overviewImage.pixelColor(0, 0).alpha() <= 8
                && overviewImage.pixelColor(overviewImage.width() - 1, 0).alpha() <= 8
                && overviewImage.pixelColor(0, overviewImage.height() - 1).alpha() <= 8
                && overviewImage.pixelColor(overviewImage.width() - 1,
                                             overviewImage.height() - 1).alpha() <= 8;
            overviewSurfaceOpaque = overviewImage.pixelColor(20, 100).alpha() >= 240;
            QVERIFY(overviewScreenshot.save(
                QDir(screenshotDirectory).filePath(QStringLiteral("forest-overview-filter.png"))));
            manifestStream << "forest-overview-filter size=" << overviewScreenshot.width() << "x"
                           << overviewScreenshot.height() << "\n";
            QTest::mouseClick(overviewApply, Qt::LeftButton);
            overviewFilterApplied = true;
        });
        auto* forestOverviewAction = forestDashboard->findChild<QAbstractButton*>("forestOverviewAction");
        QVERIFY(forestOverviewAction);
        QTest::mouseClick(forestOverviewAction, Qt::LeftButton);
        QTRY_VERIFY(overviewFilterApplied);
        QVERIFY(overviewCornersTransparent);
        QVERIFY(overviewSurfaceOpaque);
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
        auto* gachaSite = window.findChild<QWidget*>("gachaDigSite");
        auto* gachaCollection = window.findChild<QWidget*>("gachaCollectionGrid");
        auto* gachaCollectionPanel = window.findChild<QWidget*>("gachaCollectionPanel");
        auto* gachaReadyText = window.findChild<QLabel*>("gachaResultTitle");
        auto* gachaPull = buttonByTestId(window, QStringLiteral("gachaPullButton"));
        QVERIFY(gachaSite && gachaCollection && gachaCollectionPanel && gachaReadyText && gachaPull);
        QCOMPARE(gachaReadyText->text(), QStringLiteral("等待发掘"));
        const QImage siteImage = gachaSite->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(sampledColorCount(siteImage) >= 40);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        int variantCardCount = 0;
        int lockedLabelCount = 0;
        QWidget* firstVariantCard = nullptr;
        for (QWidget* widget : gachaCollection->findChildren<QWidget*>()) {
            if (widget->property("gachaVariantCard").toBool()) {
                if (!firstVariantCard) firstVariantCard = widget;
                ++variantCardCount;
            }
        }
        for (QLabel* label : gachaCollection->findChildren<QLabel*>())
            if (label->text() == QStringLiteral("未发现")) ++lockedLabelCount;
        QCOMPARE(variantCardCount, 12);
        QCOMPARE(lockedLabelCount, 12);
        auto* variantGrid = qobject_cast<QGridLayout*>(gachaCollection->layout());
        QVERIFY(variantGrid);
        QSet<int> variantColumns;
        QSet<int> variantRows;
        for (int itemIndex = 0; itemIndex < variantGrid->count(); ++itemIndex) {
            int row = 0;
            int column = 0;
            int rowSpan = 0;
            int columnSpan = 0;
            variantGrid->getItemPosition(itemIndex, &row, &column, &rowSpan, &columnSpan);
            variantRows.insert(row);
            variantColumns.insert(column);
        }
        QCOMPARE(variantColumns.size(), 4);
        QCOMPARE(variantRows.size(), 3);
        QVERIFY(firstVariantCard);
        QCOMPARE(firstVariantCard->objectName(), QStringLiteral("gachaVariantCard"));
        const QImage firstCardImage = firstVariantCard->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const QImage collectionImage = gachaCollection->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(colorDistance(firstCardImage.pixelColor(16, 16),
                              collectionImage.pixelColor(collectionImage.width() - 3,
                                                         collectionImage.height() - 3)) >= 20);
        for (const QSize& size : visualSizes) {
            window.showNormal();
            window.resize(size);
            QTest::qWait(100);
            QCOMPARE(window.size(), size);
            QVERIFY(isFullyVisible(gachaSite));
            QVERIFY(isFullyVisible(gachaPull));
            QVERIFY(isFullyVisible(gachaReadyText));
            QVERIFY(isFullyVisible(gachaCollectionPanel));
            captureAtSize(QStringLiteral("navGacha-%1x%2").arg(size.width()).arg(size.height()), size);
        }
        window.showFullScreen();
        QTest::qWait(100);
        auto* gachaResult = window.findChild<QLabel*>("gachaResultTitle");
        QVERIFY(gachaPull && gachaResult);
        QTest::mouseClick(gachaPull, Qt::LeftButton);
        QTRY_VERIFY(gachaResult->text() != QStringLiteral("等待发掘"));
        int refreshedCardCount = 0;
        int refreshedLockedCount = 0;
        for (QWidget* widget : gachaCollection->findChildren<QWidget*>())
            if (widget->property("gachaVariantCard").toBool()) ++refreshedCardCount;
        for (QLabel* label : gachaCollection->findChildren<QLabel*>())
            if (label->text() == QStringLiteral("未发现")) ++refreshedLockedCount;
        QCOMPARE(refreshedCardCount, 12);
        QCOMPARE(refreshedLockedCount, 11);
        QCOMPARE(features.gacha()->unlockedCount(), 1);
        const auto unlockedVariants = features.gacha()->allVariants();
        for (const auto& variant : unlockedVariants) {
            if (!variant.unlocked) continue;
            bool nameVisible = false;
            bool rarityVisible = false;
            for (QLabel* label : gachaCollection->findChildren<QLabel*>()) {
                nameVisible = nameVisible || label->text() == variant.displayName;
                rarityVisible = rarityVisible || label->text() == variant.rarity;
            }
            QVERIFY(nameVisible);
            QVERIFY(rarityVisible);
        }

        GachaPageController restoredGachaPage(coins, *features.gacha(), results, nullptr);
        restoredGachaPage.page()->resize(1000, 700);
        restoredGachaPage.page()->ensurePolished();
        auto* restoredSite = restoredGachaPage.page()->findChild<GachaDigSiteWidget*>("gachaDigSite");
        QVERIFY(restoredSite);
        restoredSite->setParent(nullptr);
        restoredSite->resize(600, 360);
        restoredSite->ensurePolished();
        QImage restoredSiteImage(restoredSite->size(), QImage::Format_ARGB32_Premultiplied);
        restoredSiteImage.fill(Qt::transparent);
        restoredSite->render(&restoredSiteImage);
        restoredSiteImage = restoredSiteImage.convertToFormat(QImage::Format_ARGB32);
        QVERIFY(restoredSiteImage.save(
            QDir(screenshotDirectory).filePath(QStringLiteral("gacha-restored-unlocked.png"))));
        const QImage emptySiteImage = renderEmptyDigSite();
        const int restoredPlantPixels = differentPixels(restoredSiteImage, emptySiteImage,
                                                         QRect(195, 34, 210, 198));
        QVERIFY2(restoredPlantPixels >= 900,
                 qPrintable(QStringLiteral("restored gacha page shows only %1 plant pixels")
                                .arg(restoredPlantPixels)));
        delete restoredSite;
        delete restoredGachaPage.page();

        for (const QSize& size : visualSizes) {
            window.showNormal();
            window.resize(size);
            QTest::qWait(100);
            QCOMPARE(window.size(), size);
            QVERIFY(isFullyVisible(gachaSite));
            QVERIFY(isFullyVisible(gachaPull));
            QVERIFY(isFullyVisible(gachaResult));
            QVERIFY(isFullyVisible(gachaCollectionPanel));
            captureAtSize(QStringLiteral("gacha-after-pull-%1x%2")
                              .arg(size.width())
                              .arg(size.height()),
                          size);
        }
        window.showFullScreen();
        QTest::qWait(100);
        QTest::qWait(50);
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
        checkpoint(QStringLiteral("challenges-navigation-ready"));
        QTest::mouseClick(challengeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 7);
        checkpoint(QStringLiteral("challenges-page-open"));
        auto* challenge = window.findChild<ChallengeDashboardWidget*>("challengeDashboard");
        QVERIFY(challenge);
        QTest::qWait(50);
        QSignalSpy checkinReward(challenge, &ChallengeDashboardWidget::rewardCoinsRequested);
        const int currentCheckin = (QDate::currentDate().day() - 1) % 5;
        auto* checkinAction = challenge->findChild<QAbstractButton*>(
            QStringLiteral("challengeAction_9:%1").arg(currentCheckin));
        QVERIFY(checkinAction);
        checkpoint(QStringLiteral("challenges-checkin-ready"));
        QTest::mouseClick(checkinAction, Qt::LeftButton);
        QTRY_COMPARE(checkinReward.count(), 1);
        checkpoint(QStringLiteral("challenges-checkin-rewarded"));
        QTest::mouseClick(checkinAction, Qt::LeftButton);
        QTest::qWait(50);
        QCOMPARE(checkinReward.count(), 1);
        checkpoint(QStringLiteral("challenges-second-checkin-complete"));
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
        QVERIFY(coins.unlockPlant(1));

        QSignalSpy plantClickSpy(timer, &PlantTimerWidget::sig_plantClicked);
        const uint32_t initialPlantType = plantSelector->currentData().toUInt();
        QTimer::singleShot(0, [&]() {
            if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                modal->reject();
        });
        auto* timerPlantAction = timer->findChild<QAbstractButton*>("timerPlantAction");
        QVERIFY(timerPlantAction);
        QTest::mouseClick(timerPlantAction, Qt::LeftButton);
        QCOMPARE(plantClickSpy.count(), 1);
        auto* plantPopup = qobject_cast<QDialog*>(QApplication::activePopupWidget());
        QVERIFY(plantPopup);
        QVERIFY(plantPopup->windowFlags().testFlag(Qt::Popup));
        QVERIFY(plantPopup->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!plantPopup->isModal());
        QVERIFY(!QApplication::activeModalWidget());

        QPointer<QDialog> outsideDismissedPopup = plantPopup;
        QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(window.width() - 8, 8));
        QTRY_VERIFY(outsideDismissedPopup.isNull() || !outsideDismissedPopup->isVisible());
        QCOMPARE(plantSelector->currentData().toUInt(), initialPlantType);

        QTest::mouseClick(timerPlantAction, Qt::LeftButton);
        plantPopup = qobject_cast<QDialog*>(QApplication::activePopupWidget());
        QVERIFY(plantPopup);
        QPointer<QDialog> escapeDismissedPopup = plantPopup;
        QTest::keyClick(plantPopup, Qt::Key_Escape);
        QTRY_VERIFY(escapeDismissedPopup.isNull() || !escapeDismissedPopup->isVisible());
        QCOMPARE(plantSelector->currentData().toUInt(), initialPlantType);

        QTest::mouseClick(timerPlantAction, Qt::LeftButton);
        plantPopup = qobject_cast<QDialog*>(QApplication::activePopupWidget());
        QVERIFY(plantPopup);
        QPushButton* alternatePlant = nullptr;
        const auto cards = plantPopup->findChildren<QPushButton*>();
        for (QPushButton* card : cards) {
            if (card->property("plantCard").toBool()
                && card->property("plantType").toUInt() != initialPlantType) {
                alternatePlant = card;
                break;
            }
        }
        QVERIFY(alternatePlant);
        const uint32_t alternatePlantType = alternatePlant->property("plantType").toUInt();
        QPointer<QDialog> selectedPopup = plantPopup;
        QTest::mouseClick(alternatePlant, Qt::LeftButton);
        QTRY_VERIFY(selectedPopup.isNull() || !selectedPopup->isVisible());
        QTRY_COMPARE(plantSelector->currentData().toUInt(), alternatePlantType);

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
        QTRY_VERIFY(continueButton->hasFocus());
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

        QTest::mouseClick(homeNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 0);
        window.showNormal();
        window.resize(1366, 768);
        const QFont originalApplicationFont = qApp->font();
        for (const int scalePercent : {90, 100, 125}) {
            QFont scaledFont = originalApplicationFont;
            if (originalApplicationFont.pointSizeF() > 0) {
                scaledFont.setPointSizeF(originalApplicationFont.pointSizeF() * scalePercent / 100.0);
            }
            qApp->setFont(scaledFont);
            qApp->setStyleSheet(AppStyle::styleSheet(scalePercent, false));
            QCoreApplication::processEvents();
            QTest::qWait(50);
            QVERIFY(isFullyVisible(timer));
            QVERIFY(isFullyVisible(start));
            captureAtSize(QStringLiteral("focus-home-text-%1-%2x%3")
                              .arg(scalePercent)
                              .arg(window.width())
                              .arg(window.height()),
                          window.size());
            if (scalePercent == 125) {
                for (const auto& pageCheck : pageChecks) {
                    auto* navigation = window.findChild<QPushButton*>(pageCheck.first);
                    QVERIFY(navigation);
                    QTest::mouseClick(navigation, Qt::LeftButton);
                    QTRY_COMPARE(pageStack->currentIndex(), pageCheck.second);
                    QTest::qWait(50);
                    if (pageCheck.second == 5) {
                        int visibleVariantCards = 0;
                        for (QWidget* widget : window.findChildren<QWidget*>()) {
                            if (widget->property("gachaVariantCard").toBool() && widget->isVisibleTo(&window)) {
                                ++visibleVariantCards;
                            }
                        }
                        QCOMPARE(visibleVariantCards, 12);
                    }
                    captureAtSize(QStringLiteral("%1-text-125-%2x%3")
                                      .arg(pageCheck.first)
                                      .arg(window.width())
                                      .arg(window.height()),
                                  window.size());
                }
                auto* settingsNavigation = window.findChild<QPushButton*>("navSettings");
                QVERIFY(settingsNavigation);
                QTest::mouseClick(settingsNavigation, Qt::LeftButton);
                QTRY_COMPARE(pageStack->currentIndex(), 3);
                for (int row = 0; row < settingsCategories->count(); ++row) {
                    settingsCategories->setCurrentRow(row);
                    QTRY_COMPARE(settingsStack->currentIndex(), row);
                    QTest::qWait(50);
                    captureAtSize(QStringLiteral("settings-%1-text-125-%2x%3")
                                      .arg(row)
                                      .arg(window.width())
                                      .arg(window.height()),
                                  window.size());
                }
                QTest::mouseClick(homeNavigation, Qt::LeftButton);
                QTRY_COMPARE(pageStack->currentIndex(), 0);
            }
        }
        qApp->setStyleSheet(AppStyle::styleSheet(100, true));
        window.showNormal();
        window.resize(1366, 768);
        QTest::qWait(50);
        auto* highContrastSettingsNavigation = window.findChild<QPushButton*>("navSettings");
        QVERIFY(highContrastSettingsNavigation);
        QTest::mouseClick(highContrastSettingsNavigation, Qt::LeftButton);
        QTRY_COMPARE(pageStack->currentIndex(), 3);
        for (int row = 0; row < settingsCategories->count(); ++row) {
            settingsCategories->setCurrentRow(row);
            QTRY_COMPARE(settingsStack->currentIndex(), row);
            captureAtSize(QStringLiteral("settings-%1-high-contrast-1366x768").arg(row),
                          QSize(1366, 768));
        }
        qApp->setFont(originalApplicationFont);
        qApp->setStyleSheet(AppStyle::styleSheet(100, false));
    }

    QVERIFY(features.save());
    database.close();
    users.close();
}

QTEST_MAIN(UiSmokeTest)
#include "UiSmokeTest.moc"
