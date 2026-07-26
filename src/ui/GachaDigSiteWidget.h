#ifndef GACHADIGSITEWIDGET_H
#define GACHADIGSITEWIDGET_H

#include <QColor>
#include <QPixmap>
#include <QRect>
#include <QWidget>

class GachaDigSiteWidget final : public QWidget {
public:
    explicit GachaDigSiteWidget(QWidget* parent = nullptr);

    void setDiscoveredPlant(const QPixmap& pixmap, const QColor& accent);
    void clearDiscoveredPlant();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap blendedPlant_;
    QRect discoveredPlantBounds_;
    QColor accent_ = QColor("#7BC7A7");
    bool hasResult_ = false;
};

#endif // GACHADIGSITEWIDGET_H
