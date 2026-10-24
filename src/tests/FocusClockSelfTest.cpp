#include "tests/TestFocusClock.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool testClocks(const QString& root)
{
    DatabaseManager db(QDir(root).filePath("clocks.sqlite").toStdString());
    if (!db.open()) return false;
    TestFocusClock clock;
    FocusController focus(db, clock.clock());
    int finalized = 0;
    QObject::connect(&focus, &FocusController::sig_focusFinalized,
                     [&finalized]() { ++finalized; });
    focus.startFocus(0, 10);
    const auto startTimestamp = db.readById(0)->startTimestamp;
    focus.tick();
    focus.tick();
    if (!expect(focus.actualSeconds() == 0, "callbacks alone do not add time")) return false;
    clock.milliseconds += 450;
    focus.tick();
    clock.milliseconds += 550;
    focus.tick();
    if (!expect(focus.actualSeconds() == 1, "subsecond callbacks retain their remainder")) return false;
    clock.epoch += 86400;
    clock.advanceSeconds(39);
    focus.tick();
    if (!expect(focus.actualSeconds() == 40 && focus.remainingSeconds() == 560,
                "delayed callback uses monotonic elapsed time despite wall-clock change")) return false;
    clock.epoch -= 172800;
    clock.advanceSeconds(3600);
    focus.tick();
    const auto completed = db.readById(0);
    if (!expect(completed && completed->status == FocusRecordStatus::Success &&
                completed->actualSeconds == 600 && completed->coinsEarned == 2 &&
                completed->growthStage == 3 && completed->startTimestamp == startTimestamp,
                "sleep finishes countdown once at its deadline without excess rewards")) return false;
    focus.tick();
    focus.abandonFocus();
    if (!expect(finalized == 1, "terminal callbacks cannot finalize twice")) return false;

    focus.startFocus(1, 10);
    clock.advanceSeconds(20);
    focus.pauseFocus();
    if (!expect(focus.actualSeconds() == 20 && focus.pauseRemainingSeconds() == 300,
                "pause synchronizes focus time and starts an unpausable five-minute break")) return false;
    clock.advanceSeconds(100);
    focus.pauseFocus();
    if (!expect(focus.pauseRemainingSeconds() == 200 && focus.actualSeconds() == 20,
                "repeated pause cannot extend or stop the break")) return false;
    clock.advanceSeconds(210);
    focus.pauseFocus();
    if (!expect(focus.currentState() == FocusController::State::RUNNING &&
                focus.actualSeconds() == 30,
                "late duplicate pause resumes at the deadline without starting another break")) return false;
    focus.pauseFocus();
    clock.advanceSeconds(50);
    focus.resumeFocus();
    clock.advanceSeconds(5);
    focus.abandonFocus();
    const auto pausedRecord = db.readById(1);
    if (!expect(pausedRecord && pausedRecord->actualSeconds == 35 &&
                pausedRecord->status == FocusRecordStatus::Abandoned && pausedRecord->coinsEarned == 0,
                "manual resume excludes the pause and abandonment synchronizes final time")) return false;

    focus.startFocus(2, 10, FocusController::TimerMode::COUNTDOWN,
                     FocusController::FocusMode::STRICT_MODE, 0, false);
    clock.advanceSeconds(15);
    focus.pauseFocus();
    if (!expect(focus.currentState() == FocusController::State::RUNNING && focus.actualSeconds() == 15,
                "pause permission is enforced by the domain controller")) return false;
    focus.handleViolationDetected(QStringLiteral("blocked"));
    clock.advanceSeconds(9);
    focus.handleViolationDetected(QStringLiteral("blocked"));
    focus.handleSafeWindowDetected();
    if (!expect(focus.currentState() == FocusController::State::RUNNING &&
                focus.actualSeconds() == 15 && focus.violationCount() == 0,
                "timely safe return excludes grace time and does not count a violation")) return false;
    clock.advanceSeconds(5);
    focus.handleViolationDetected(QStringLiteral("blocked"));
    clock.advanceSeconds(10);
    focus.handleSafeWindowDetected();
    const auto failed = db.readById(2);
    if (!expect(failed && failed->status == FocusRecordStatus::Failed &&
                failed->actualSeconds == 20 && failed->violationCount == 1 &&
                failed->growthStage == 4 && failed->coinsEarned == 0,
                "late safe event cannot bypass strict deadline or add warning time")) return false;

    focus.startFocus(3, 10, FocusController::TimerMode::COUNTDOWN,
                     FocusController::FocusMode::GENTLE_MODE);
    clock.advanceSeconds(50);
    focus.handleViolationDetected(QStringLiteral("blocked"));
    focus.handleViolationDetected(QStringLiteral("blocked"));
    clock.advanceSeconds(30);
    focus.handleSafeWindowDetected();
    clock.advanceSeconds(100);
    focus.tick();
    if (!expect(focus.actualSeconds() == 180 && focus.effectiveSeconds() == 150 &&
                focus.violationCount() == 1 && db.readById(3)->actualSeconds == 150,
                "safe intervals stop gentle deductions; checkpoint persists effective seconds")) return false;
    clock.advanceSeconds(420);
    focus.tick();
    const auto gentle = db.readById(3);
    if (!expect(gentle && gentle->actualSeconds == 570 && gentle->coinsEarned == 0,
                "gentle reward uses only effective seconds and existing multiplier")) return false;

    focus.startFocus(4, 10, FocusController::TimerMode::COUNTDOWN,
                     FocusController::FocusMode::STRICT_MODE, 0, true, true);
    clock.advanceSeconds(650);
    focus.tick();
    if (!expect(focus.currentState() == FocusController::State::RUNNING &&
                focus.timerMode() == FocusController::TimerMode::STOPWATCH && focus.actualSeconds() == 650 &&
                focus.growthStage() == 3,
                "automatic extension preserves elapsed time across the countdown boundary")) return false;
    focus.completeFocus();
    if (!expect(db.readById(4)->status == FocusRecordStatus::Success &&
                db.readById(4)->actualSeconds == 650,
                "extended countdown can complete normally")) return false;

    focus.startFocus(5, 0, FocusController::TimerMode::STOPWATCH);
    clock.advanceSeconds(599);
    focus.completeFocus();
    if (!expect(db.readById(5)->status == FocusRecordStatus::Failed,
                "stopwatch retains its ten-minute minimum")) return false;
    focus.startFocus(5, 0, FocusController::TimerMode::STOPWATCH);
    clock.advanceSeconds(9000);
    focus.tick();
    if (!expect(db.readById(6)->actualSeconds == 7200 && db.readById(6)->coinsEarned == 24 &&
                db.readById(6)->growthStage == 3,
                "stopwatch sleep is capped at its two-hour terminal boundary")) return false;

    focus.startFocus(0, 10, FocusController::TimerMode::COUNTDOWN,
                     FocusController::FocusMode::STRICT_MODE, 0, true, true);
    clock.advanceSeconds(9000);
    focus.tick();
    if (!expect(db.readById(7)->status == FocusRecordStatus::Success &&
                db.readById(7)->actualSeconds == 7200,
                "one delayed callback handles both auto-extension and its final limit")) return false;
    focus.startFocus(0, 10);
    db.failNextTerminalIntentForTesting();
    clock.advanceSeconds(600);
    focus.tick();
    const int beforeRetry = finalized;
    if (!expect(focus.currentState() == FocusController::State::SAVE_FAILED &&
                focus.actualSeconds() == 600 && focus.growthStage() == 3,
                "success intent failure freezes the completed time and mature plant")) return false;
    clock.advanceSeconds(900);
    focus.tick();
    focus.completeFocus();
    focus.abandonFocus();
    if (!expect(focus.actualSeconds() == 600 && finalized == beforeRetry &&
                focus.retryPendingFinalization() && finalized == beforeRetry + 1 &&
                db.readById(8)->status == FocusRecordStatus::Success &&
                db.readById(8)->coinsEarned == 2,
                "retry preserves success and emits exactly one durable result")) return false;

    focus.startFocus(0, 10, FocusController::TimerMode::COUNTDOWN,
                     FocusController::FocusMode::GENTLE_MODE);
    clock.milliseconds += 10750;
    focus.handleViolationDetected(QStringLiteral("blocked"));
    focus.pauseFocus();
    clock.milliseconds += 300250;
    focus.tick();
    focus.handleSafeWindowDetected();
    if (!expect(focus.currentState() == FocusController::State::RUNNING &&
                focus.actualSeconds() == 11 && focus.effectiveSeconds() == 11,
                "safe time after pause auto-resume is not charged to the prior violation")) return false;
    focus.abandonFocus();
    return true;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    if (!root.isValid() || !testClocks(root.path())) return 1;
    std::cout << "Focus clock tests passed\n";
    return 0;
}
