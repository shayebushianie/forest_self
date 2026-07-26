#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QVector>
#include <QWidget>
#include <QStringList>
#include <cstdint>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class SproutToggle;

class SettingsPage final : public QWidget {
    Q_OBJECT

public:
    struct Option {
        QString label;
        uint32_t value = 0;
    };

    struct FocusSettings {
        int minutes = 25;
        uint32_t plantType = 0;
        uint32_t tagId = 0;
        bool stopwatch = false;
        bool deepFocus = true;
        bool allowPause = true;
        QString oath;
    };

    struct AccessibilitySettings {
        int fontScalePercent = 100;
        bool reducedMotion = false;
        bool highContrast = false;
    };

    explicit SettingsPage(QWidget* parent = nullptr);

    FocusSettings focusSettings() const;
    AccessibilitySettings accessibilitySettings() const;
    void setFocusSettings(const FocusSettings& settings);
    void setAccessibilitySettings(const AccessibilitySettings& settings);
    void setPlantOptions(const QVector<Option>& options, uint32_t selectedValue);
    void setTagOptions(const QVector<Option>& options, uint32_t selectedValue);
    void setBlacklist(const QStringList& entries);
    void setDataSummary(const QString& mode, int schemaVersion);
    void setGuardianGoalMinutes(int minutes);
    void setAllowPauseControlEnabled(bool enabled);
    void setReducedMotion(bool reduced);

signals:
    void focusSettingsChanged();
    void accessibilitySettingsChanged();
    void guardianGoalChanged(int minutes);
    void blacklistAddRequested(const QString& processName);
    void blacklistRemoveRequested(const QString& processName);
    void backupRequested();
    void restoreRequested();
    void exportRequested();
    void openDataDirectoryRequested();

private:
    void updateMinutesEnabled();
    void emitAccessibilityChange();

    QComboBox* plantCombo_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QSpinBox* minutesSpin_ = nullptr;
    QComboBox* tagCombo_ = nullptr;
    QLineEdit* oathInput_ = nullptr;
    SproutToggle* deepFocusToggle_ = nullptr;
    SproutToggle* allowPauseToggle_ = nullptr;
    QSpinBox* guardianGoalSpin_ = nullptr;
    QComboBox* fontScaleCombo_ = nullptr;
    SproutToggle* reducedMotionToggle_ = nullptr;
    SproutToggle* highContrastToggle_ = nullptr;
    QListWidget* blacklistWidget_ = nullptr;
    QLineEdit* blacklistInput_ = nullptr;
    QLabel* dataSummaryLabel_ = nullptr;
    bool updating_ = false;
};

#endif // SETTINGSPAGE_H
