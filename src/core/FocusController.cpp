#include "core/FocusController.h"
#include "plant/PlantFactory.h"
#include "core/FocusClock.h"
#include "utils/Logger.h"
#include <QDateTime>
#include <limits>
#include <algorithm>

namespace {

uint64_t currentSystemEpochSeconds()
{
    return static_cast<uint64_t>(QDateTime::currentDateTime().toSecsSinceEpoch());
}

}

FocusController::FocusController(DatabaseManager& db, QObject* parent)
    : FocusController(db, Clock{FocusClock::monotonicMilliseconds, currentSystemEpochSeconds}, parent)
{
}

FocusController::FocusController(DatabaseManager& db, Clock clock, QObject* parent)
    : QObject(parent), clock_(std::move(clock)), db_(db)
{
    if (!clock_.monotonicMilliseconds) clock_.monotonicMilliseconds = FocusClock::monotonicMilliseconds;
    if (!clock_.epochSeconds) clock_.epochSeconds = currentSystemEpochSeconds;
}

void FocusController::startFocus(uint32_t plantType, uint32_t minutes,
                                  TimerMode timerMode, FocusMode focusMode,
                                  uint32_t tagId,
                                  bool allowPause,
                                  bool autoExtendCountdown)
{
    if (pendingFinalization_ || currentState_ == State::RUNNING || currentState_ == State::PAUSED ||
        currentState_ == State::WARNING) return;
    if (timerMode == TimerMode::COUNTDOWN && (minutes < 10 || minutes > 120)) return;

    // Reset all per-session counters before creating the running placeholder
    // record. The final result overwrites this same record by recordIndex_.
    currentMode_ = timerMode;
    focusMode_ = focusMode;
    currentTagId_ = tagId;
    allowPause_ = allowPause;
    autoExtendCountdown_ = (timerMode == TimerMode::COUNTDOWN) ? autoExtendCountdown : false;
    lastCoinsEarned_ = 0;
    lastPublishedGrowthStage_ = 0;
    actualSeconds_ = 0;
    lastCheckpointSeconds_ = 0;
    violationCount_ = 0;
    warningRemainingSeconds_ = 10;
    violating_ = false;
    elapsedMilliseconds_ = 0;
    violationMilliseconds_ = 0;
    lastObservedMilliseconds_ = clockMilliseconds();

    plannedMinutes_ = (timerMode == TimerMode::STOPWATCH) ? 0 : minutes;
    remainingSeconds_ = (timerMode == TimerMode::COUNTDOWN) ? minutes * 60 : 0;

    currentPlant_ = PlantFactory::create(plantType);
    if (!currentPlant_) return;

    FocusRecord record;
    record.plantType = plantType;
    record.plannedMinutes = plannedMinutes_;
    record.status = FocusRecordStatus::Running;
    record.focusMode = focusMode == FocusMode::STRICT_MODE
        ? PersistedFocusMode::Deep : PersistedFocusMode::GentleLegacy;
    record.tagId = currentTagId_;
    record.startTimestamp = clock_.epochSeconds();
    recordIndex_ = db_.append(record);
    if (recordIndex_ == std::numeric_limits<uint32_t>::max()) {
        Logger::instance().error("Running record could not be saved.", "focus", "start");
        currentPlant_.reset();
        return;
    }

    currentState_ = State::RUNNING;
    Logger::instance().info("record=" + std::to_string(recordIndex_) + " started.", "focus", "start");
    emit sig_stateChanged(currentState_);
    emit sig_focusStarted(recordIndex_);
}

void FocusController::pauseFocus() {
    const bool wasRunning = currentState_ == State::RUNNING;
    tick();
    if (!wasRunning || currentState_ != State::RUNNING) return;
    if (!allowPause_) return;
    checkpointRecord();
    violating_ = false;
    pauseDeadlineMilliseconds_ = clockMilliseconds() + 300000;
    currentState_ = State::PAUSED; emit sig_stateChanged(currentState_);
}

void FocusController::resumeFocus() {
    tick();
    if (currentState_ != State::PAUSED) return;
    lastObservedMilliseconds_ = clockMilliseconds();
    currentState_ = State::RUNNING; emit sig_stateChanged(currentState_);
}

void FocusController::abandonFocus() {
    tick();
    if (currentState_ != State::RUNNING && currentState_ != State::PAUSED
        && currentState_ != State::WARNING) return;
    if (currentPlant_) currentPlant_->wither();
    writeRecord(FocusRecordStatus::Abandoned);
}

void FocusController::completeFocus() {
    tick();
    if (currentState_ != State::RUNNING) return;
    if (currentMode_ != TimerMode::STOPWATCH) return;
    if (actualSeconds_ < 600) { handleFailure(); }
    else                      { handleSuccess(); }
}

void FocusController::handleViolationDetected(const QString& appName)
{
    tick();
    if (currentState_ != State::RUNNING && currentState_ != State::WARNING) return;

    if (focusMode_ == FocusMode::STRICT_MODE) {
        // Strict mode gives the user a short grace period to return to safety.
        if (currentState_ == State::RUNNING) {
            currentState_ = State::WARNING;
            warningRemainingSeconds_ = 10;
            warningDeadlineMilliseconds_ = clockMilliseconds() + 10000;
            emit sig_stateChanged(currentState_);
            emit sig_strictWarningTick(warningRemainingSeconds_);
        }
    } else {
        // Gentle mode keeps the session alive but discounts reward time later.
        if (!violating_) {
            violating_ = true;
            ++violationCount_;
            emit sig_softViolation(appName);
        }
    }
}

void FocusController::handleSafeWindowDetected()
{
    tick();
    violating_ = false;
    if (focusMode_ == FocusMode::STRICT_MODE && currentState_ == State::WARNING) {
        currentState_ = State::RUNNING;
        warningRemainingSeconds_ = 10;
        emit sig_stateChanged(currentState_);
    }
}

void FocusController::tick()
{
    const qint64 now = clockMilliseconds();
    if (now < lastObservedMilliseconds_) return;
    if (currentState_ == State::WARNING) {
        lastObservedMilliseconds_ = now;
        warningRemainingSeconds_ = static_cast<uint32_t>(
            std::max<qint64>(0, warningDeadlineMilliseconds_ - now + 999) / 1000);
        emit sig_strictWarningTick(warningRemainingSeconds_);
        if (warningRemainingSeconds_ == 0) {
            ++violationCount_;
            handleFailure();
        }
        return;
    }

    if (currentState_ == State::PAUSED) {
        if (now < pauseDeadlineMilliseconds_) {
            lastObservedMilliseconds_ = now;
            return;
        }
        lastObservedMilliseconds_ = pauseDeadlineMilliseconds_;
        currentState_ = State::RUNNING;
        emit sig_stateChanged(currentState_);
    }
    if (currentState_ != State::RUNNING) return;

    const qint64 limit = currentMode_ == TimerMode::COUNTDOWN && !autoExtendCountdown_
        ? static_cast<qint64>(plannedMinutes_) * 60000 : 7200000;
    const qint64 delta = std::min(now - lastObservedMilliseconds_,
                                 std::max<qint64>(0, limit - elapsedMilliseconds_));
    lastObservedMilliseconds_ = now;
    elapsedMilliseconds_ += delta;
    if (focusMode_ == FocusMode::GENTLE_MODE && violating_) violationMilliseconds_ += delta;
    actualSeconds_ = static_cast<uint32_t>(elapsedMilliseconds_ / 1000);

    if (currentMode_ == TimerMode::COUNTDOWN) {
        remainingSeconds_ = actualSeconds_ < plannedMinutes_ * 60
            ? plannedMinutes_ * 60 - actualSeconds_ : 0;
        emit sig_tick(remainingSeconds_, false);
        if (remainingSeconds_ == 0) {
            if (autoExtendCountdown_) {
                // Continue counting upward after the planned countdown ends.
                currentMode_ = TimerMode::STOPWATCH;
                autoExtendCountdown_ = false;
                emit sig_tick(actualSeconds_, true);
                emit sig_stateChanged(currentState_);
            } else {
                handleSuccess();
                return;
            }
        }
    } else {
        emit sig_tick(actualSeconds_, true);
        if (actualSeconds_ >= 7200) {
            handleSuccess();
            return;
        }
    }

    updateGrowth();
    if (currentMode_ == TimerMode::STOPWATCH && actualSeconds_ >= 7200) {
        handleSuccess();
        return;
    }
    if (actualSeconds_ - lastCheckpointSeconds_ >= 30) {
        checkpointRecord();
    }
}

void FocusController::updateGrowth()
{
    if (!currentPlant_) return;
    int total = plannedMinutes_ > 0 ? static_cast<int>(plannedMinutes_ * 60) : 7200;
    currentPlant_->grow(static_cast<int>(actualSeconds_), total);
    const uint32_t stage = currentPlant_->getGrowthStage();
    if (stage != lastPublishedGrowthStage_) {
        lastPublishedGrowthStage_ = stage;
        emit sig_growthStageChanged(stage);
    }
}

uint32_t FocusController::pauseRemainingSeconds() const
{
    if (currentState_ != State::PAUSED) return 0;
    return static_cast<uint32_t>(std::max<qint64>(0,
        pauseDeadlineMilliseconds_ - clockMilliseconds() + 999) / 1000);
}

void FocusController::handleSuccess()
{
    updateGrowth();
    writeRecord(FocusRecordStatus::Success);
}

void FocusController::handleFailure()
{
    if (currentPlant_) currentPlant_->wither();
    writeRecord(FocusRecordStatus::Failed);
}

void FocusController::writeRecord(FocusRecordStatus status)
{
    const auto existingRecord = db_.readById(recordIndex_);

    // Gentle-mode violation seconds are excluded from rewardable focus time.
    uint32_t pureSeconds = effectiveSeconds();

    FocusRecord record;
    record.recordId = recordIndex_;
    record.plantType = existingRecord ? existingRecord->plantType
                                      : (currentPlant_ ? currentPlant_->getPlantType() : 0);
    record.plannedMinutes = existingRecord ? existingRecord->plannedMinutes : plannedMinutes_;
    record.actualSeconds = pureSeconds;
    record.startTimestamp = existingRecord ? existingRecord->startTimestamp
                                           : clock_.epochSeconds();
    record.status = status;
    record.violationCount = violationCount_;
    record.growthStage = currentPlant_ ? currentPlant_->getGrowthStage() : 4;
    record.coinsEarned = (status == FocusRecordStatus::Success) ? calculateCoins() : 0;
    record.tagId = currentTagId_;
    record.focusMode = focusMode_ == FocusMode::STRICT_MODE
        ? PersistedFocusMode::Deep : PersistedFocusMode::GentleLegacy;
    record.gridIndex = existingRecord ? existingRecord->gridIndex : 0;
    pendingFinalization_ = record;
    retryPendingFinalization();
}

bool FocusController::retryPendingFinalization()
{
    if (!pendingFinalization_) return true;
    const FocusRecord record = *pendingFinalization_;
    if (!db_.finalizeRecord(record.recordId, record)) {
        if (currentState_ != State::SAVE_FAILED)
            Logger::instance().error("record=" + std::to_string(record.recordId) +
                                     " terminal save failed; decision frozen.", "focus", "terminal-save");
        currentState_ = State::SAVE_FAILED;
        emit sig_stateChanged(currentState_);
        emit sig_finalizationSaveFailed(db_.lastError());
        return false;
    }
    pendingFinalization_.reset();
    Logger::instance().info("record=" + std::to_string(record.recordId) +
        " status=" + std::to_string(static_cast<uint32_t>(record.status)) +
        " seconds=" + std::to_string(record.actualSeconds) +
        " interrupted=" + std::to_string(isInterruptedFocus(record)), "focus", "terminal-saved");
    lastCoinsEarned_ = record.coinsEarned;

    currentState_ = (record.status == FocusRecordStatus::Success) ? State::SUCCESS : State::FAILED;
    emit sig_stateChanged(currentState_);
    emit sig_focusFinalized(record.recordId, static_cast<uint32_t>(record.status));
    return true;
}

void FocusController::checkpointRecord()
{
    const auto existingRecord = db_.readById(recordIndex_);
    if (!existingRecord || currentState_ == State::IDLE) return;

    FocusRecord checkpoint = existingRecord.value();
    checkpoint.status = FocusRecordStatus::Running;
    checkpoint.actualSeconds = effectiveSeconds();
    checkpoint.violationCount = violationCount_;
    checkpoint.growthStage = currentPlant_ ? currentPlant_->getGrowthStage() : checkpoint.growthStage;
    checkpoint.tagId = currentTagId_;
    if (db_.updateById(recordIndex_, checkpoint)) {
        lastCheckpointSeconds_ = actualSeconds_;
    }
}

uint32_t FocusController::calculateCoins() const
{
    // Base reward: one coin per five clean focus minutes.
    uint32_t pureSeconds = effectiveSeconds();
    uint32_t rawCoins = pureSeconds / 300;

    if (focusMode_ == FocusMode::GENTLE_MODE) {
        double multiplier = 1.0;
        if (violationCount_ == 0)       multiplier = 1.0;
        else if (violationCount_ <= 3)  multiplier = 0.8;
        else if (violationCount_ <= 10) multiplier = 0.5;
        else                            multiplier = 0.2;
        return static_cast<uint32_t>(rawCoins * multiplier);
    }
    return rawCoins;
}
