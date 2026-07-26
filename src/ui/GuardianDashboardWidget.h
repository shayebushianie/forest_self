#ifndef GUARDIANDASHBOARDWIDGET_H
#define GUARDIANDASHBOARDWIDGET_H

#include <QRectF>
#include <QPointer>
#include <QWidget>
#include <QPixmap>
#include <cstdint>

class QPainter;
class QSpinBox;
class QPropertyAnimation;
class QHideEvent;

// Animated desktop dashboard for the time guardian page.
class GuardianDashboardWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal shownProgress READ shownProgress WRITE setShownProgress)

public:
    explicit GuardianDashboardWidget(QWidget* parent = nullptr);

    void setSnapshot(uint32_t todayMinutes,
                     uint32_t dailyGoalMinutes,
                     uint32_t currentStreak,
                     uint32_t longestStreak,
                     uint32_t totalMinutes);
    void setReducedMotion(bool reducedMotion);

    qreal shownProgress() const { return shownProgress_; }
    void setShownProgress(qreal progress);

signals:
    void dailyGoalChanged(int minutes);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct LayoutRects {
        QRectF header;
        QRectF hero;
        QRectF stats;
        QRectF goal;
        QRectF timeline;
    };

    LayoutRects calculateLayout() const;
    void syncGoalSpin();
    void drawBackground(QPainter& painter) const;
    void drawHeader(QPainter& painter, const QRectF& rect) const;
    void drawHero(QPainter& painter, const QRectF& rect) const;
    void drawStats(QPainter& painter, const QRectF& rect) const;
    void drawGoalCard(QPainter& painter, const QRectF& rect) const;
    void drawTimeline(QPainter& painter, const QRectF& rect) const;
    void drawPlantIcon(QPainter& painter, const QRectF& rect) const;
    void drawCard(QPainter& painter, const QRectF& rect, qreal radius = 24.0) const;
    void drawStatCard(QPainter& painter, const QRectF& rect, const QString& icon,
                      const QString& value, const QString& title) const;
    void drawTextFit(QPainter& painter, const QRectF& rect, const QString& text,
                     int flags, int minPointSize = 8) const;

    uint32_t todayMinutes_ = 0;
    uint32_t dailyGoalMinutes_ = 30;
    uint32_t currentStreak_ = 0;
    uint32_t longestStreak_ = 0;
    uint32_t totalMinutes_ = 0;

    qreal shownProgress_ = 0.0;
    qreal targetProgress_ = 0.0;
    QPixmap plantIcon_;
    mutable QPixmap scaledPlantIcon_;
    mutable QSize scaledPlantSize_;
    QPointer<QPropertyAnimation> progressAnimation_;
    QSpinBox* goalSpin_ = nullptr;
    bool syncingGoalSpin_ = false;
    bool reducedMotion_ = false;
};

#endif // GUARDIANDASHBOARDWIDGET_H
