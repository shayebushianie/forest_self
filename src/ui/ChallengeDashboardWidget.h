#ifndef CHALLENGEDASHBOARDWIDGET_H
#define CHALLENGEDASHBOARDWIDGET_H

#include <QColor>
#include <QDate>
#include <QHash>
#include <QPixmap>
#include <QRectF>
#include <QVector>
#include <QWidget>
#include <vector>

#include "common/DatabaseCommon.h"
#include "core/ChallengeManager.h"

class QPainter;
class QRectF;
class QToolButton;

// Desktop game-like challenge center for rewards, daily check-in, and tasks.
class ChallengeDashboardWidget : public QWidget {
    Q_OBJECT

public:
    explicit ChallengeDashboardWidget(QWidget* parent = nullptr);

    void setSnapshot(uint32_t coins,
                     const QVector<Challenge>& challenges,
                     const std::vector<FocusRecord>& records);

signals:
    void openFocusRequested();
    void openShopRequested();
    void openPlantSettingsRequested();
    void growthRulesRequested();
    void rewardCoinsRequested(int amount);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class HitRole {
        None,
        Coin,
        Growth,
        MonthlyCard,
        MonthlyProgress,
        MonthlyTime,
        MonthlyReward,
        MonthlyPlant,
        CheckinRefresh,
        CheckinReward,
        TaskCard,
        TaskProgress,
        TaskChest,
        TaskPlus,
        TaskArrow
    };

    struct PlantInfo {
        QString name;
        QString rarity;
        QString description;
        QString condition;
        QString source;
        QString iconPath;
        uint32_t type = 0;
    };

    struct TaskItem {
        QString title;
        QString type;
        QString description;
        QString rewardIcon;
        QString actionText;
        int progress = 0;
        int target = 1;
        int rewardCoins = 0;
        QColor chestColor;
        bool plus = false;
        bool arrow = false;
    };

    struct CheckinReward {
        QString icon;
        QString text;
        QString state;
        int coinReward = 0;
    };

    struct DataModel {
        int todayMinutes = 0;
        int todaySessions = 0;
        int monthMinutes = 0;
        int monthTarget = 1;
        int rewardGrowth = 0;
        int remainingDays = 0;
        PlantInfo rewardPlant;
        QVector<CheckinReward> checkins;
        QVector<TaskItem> tasks;
    };

    struct HitRegion {
        QRectF rect;
        HitRole role = HitRole::None;
        int index = -1;
        QString tooltip;
    };

    DataModel buildData() const;
    QVector<PlantInfo> shopPlants() const;
    void resetDailyStateIfNeeded();
    void loadRewardState(const QDate& today);
    void saveRewardState() const;
    static QString boolVectorToString(const QVector<bool>& values);
    static QVector<bool> boolVectorFromString(const QString& text, int size);
    void addHit(const QRectF& rect, HitRole role, int index = -1, const QString& tooltip = QString()) const;
    QString hitKey(HitRole role, int index) const;
    bool isHovered(HitRole role, int index = -1) const;
    QToolButton* actionButton(HitRole role, int index, const QString& tooltip);
    void syncActionButtons();
    void activate(HitRole role, int index);
    void drawActionStates(QPainter& painter) const;
    int completionPercent(int progress, int target) const;
    void claimMonthlyReward(const DataModel& data);
    void claimCheckinReward(const DataModel& data, int index);
    void claimTaskReward(const DataModel& data, int index);
    void showResourceDialog(HitRole role, const DataModel& data);
    void showMonthlyDialog(const DataModel& data);
    void showRefreshDialog(bool monthly) const;
    void showPlantDialog(const PlantInfo& plant);
    void showTaskDialog(const DataModel& data, int index);
    void showTaskRewardPreview(const TaskItem& task) const;
    void showPlusDialog(const TaskItem& task) const;
    void routeTaskAction(const TaskItem& task);

    void drawHeader(QPainter& painter, const QRectF& rect, const DataModel& data) const;
    void drawMonthlyCard(QPainter& painter, const QRectF& rect, const DataModel& data) const;
    void drawCheckinCard(QPainter& painter, const QRectF& rect, const DataModel& data) const;
    void drawTaskPanel(QPainter& painter, const QRectF& rect, const DataModel& data) const;
    void drawTaskCard(QPainter& painter, const QRectF& rect, const TaskItem& task, int index) const;
    void drawResourcePill(QPainter& painter, const QRectF& rect, const QString& icon,
                          const QString& value, bool hovered) const;
    void drawRewardPlant(QPainter& painter, const QRectF& rect, const PlantInfo& plant) const;
    void drawChest(QPainter& painter, const QRectF& rect, const QColor& color) const;
    void drawProgressBar(QPainter& painter, const QRectF& rect, int progress, int target,
                         const QColor& fill) const;
    void drawButton(QPainter& painter, const QRectF& rect, const QString& text,
                    const QColor& color, bool enabled, bool hovered) const;
    void drawCard(QPainter& painter, const QRectF& rect, qreal radius = 26.0) const;
    void drawTextFit(QPainter& painter, const QRectF& rect, const QString& text,
                     int flags, int minPointSize = 8) const;

    uint32_t coins_ = 0;
    QVector<Challenge> challenges_;
    std::vector<FocusRecord> records_;
    mutable QVector<HitRegion> hitRegions_;
    QHash<QString, QToolButton*> actionButtons_;
    QVector<bool> taskClaimed_;
    QVector<bool> checkinClaimed_;
    bool monthlyClaimed_ = false;
    QDate stateDate_;
    QString monthKey_;
};

#endif // CHALLENGEDASHBOARDWIDGET_H
