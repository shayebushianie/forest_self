#ifndef PLANTTIMERWIDGET_H
#define PLANTTIMERWIDGET_H

#include <QAbstractSlider>
#include <QColor>
#include <QMap>
#include <QPixmap>
#include <QString>
#include <cstdint>

class PaintedActionButton;

// Custom circular timer control. In setup mode the ring behaves like a minute
// selector; during focus it becomes a read-only progress display.
class PlantTimerWidget : public QAbstractSlider {
    Q_OBJECT

public:
    explicit PlantTimerWidget(QWidget* parent = nullptr);

    void setDisplaySeconds(uint32_t seconds, bool isStopwatch);
    void setQuote(const QString& quote);
    void setOath(const QString& oath);
    void setPlantType(uint32_t type);
    void setTagInfo(const QString& text, const QColor& color);

    uint32_t selectedMinutes() const { return selectedMinutes_; }
    void setSelectedMinutes(uint32_t minutes);

signals:
    void sig_timeSelected(uint32_t minutes);
    void sig_tagClicked();
    void sig_plantClicked();

protected:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    // Geometry helpers used by both painting and drag-to-select interaction.
    double angleFromPoint(const QPointF& p) const;
    double angleToMinutes(double angleDeg) const;
    double minutesToAngle(uint32_t minutes) const;
    QPointF knobCenter(double angleDeg) const;
    bool isOnKnob(const QPointF& p) const;
    bool selectorVisible() const;
    void updateSemanticActionGeometry();

    // Load (and cache) the pixmap for a given plant type and stage.
    const QPixmap& plantPixmap(uint32_t plantType, uint32_t stage, bool withered);

    uint32_t displaySeconds_ = 0;
    uint32_t totalSeconds_ = 0;
    uint32_t selectedMinutes_ = 25;
    uint32_t plantType_ = 0;
    bool isStopwatch_ = false;
    bool isRunning_ = false;
    bool dragging_ = false;

    QString quote_;
    QString oath_;
    QString tagText_ = QStringLiteral("无标签");
    QColor tagColor_ = QColor("#B8B6EA");
    QRectF ringRect_;
    QRectF tagRect_;
    QRectF plantRect_;
    double ringCenterX_ = 0, ringCenterY_ = 0;
    double ringRadius_ = 0;
    double knobAngle_ = 0;

    QMap<QString, QPixmap> plantCache_;
    PaintedActionButton* tagAction_ = nullptr;
    PaintedActionButton* plantAction_ = nullptr;
};

#endif // PLANTTIMERWIDGET_H
