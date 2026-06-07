#ifndef PLANTTIMERWIDGET_H
#define PLANTTIMERWIDGET_H

#include <QWidget>
#include <QString>
#include <cstdint>

class PlantTimerWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlantTimerWidget(QWidget* parent = nullptr);

    void setDisplaySeconds(uint32_t seconds, bool isStopwatch);
    void setQuote(const QString& quote);
    void setOath(const QString& oath);
    void setPlantType(uint32_t type);

    uint32_t selectedMinutes() const { return selectedMinutes_; }

signals:
    void sig_timeSelected(uint32_t minutes);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    double angleFromPoint(const QPointF& p) const;
    double angleToMinutes(double angleDeg) const;
    QPointF knobCenter(double angleDeg) const;
    bool isOnKnob(const QPointF& p) const;

    uint32_t displaySeconds_ = 0;
    uint32_t totalSeconds_ = 0;
    uint32_t selectedMinutes_ = 25;
    uint32_t plantType_ = 0;
    bool isStopwatch_ = false;
    bool isRunning_ = false;
    bool dragging_ = false;

    QString quote_;
    QString oath_;
    QRectF ringRect_;
    double ringCenterX_ = 0, ringCenterY_ = 0;
    double ringRadius_ = 0;
    double knobAngle_ = 0;
};

#endif // PLANTTIMERWIDGET_H
