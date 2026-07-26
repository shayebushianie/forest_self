#ifndef SPROUTTOGGLE_H
#define SPROUTTOGGLE_H

#include <QCheckBox>

class QVariantAnimation;

class SproutToggle final : public QCheckBox {
    Q_OBJECT

public:
    explicit SproutToggle(QWidget* parent = nullptr);

    QString statusText() const;
    void setReducedMotion(bool reduced);
    bool reducedMotion() const { return reducedMotion_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void updateVisualState(bool checked, bool animated);

    QVariantAnimation* animation_ = nullptr;
    qreal visualProgress_ = 0.0;
    bool reducedMotion_ = false;
    bool pointerPressActive_ = false;
    bool animateNextStateChange_ = false;
};

#endif // SPROUTTOGGLE_H
