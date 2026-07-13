#ifndef GARDENCANVAS_H
#define GARDENCANVAS_H

#include <QWidget>
#include <QMap>
#include <QPixmap>
#include <vector>
#include "common/DatabaseCommon.h"

// Paints persisted focus records as an 8-column garden grid.
class GardenCanvas : public QWidget {
    Q_OBJECT

public:
    explicit GardenCanvas(QWidget* parent = nullptr);

    void loadRecords(const std::vector<FocusRecord>& records);
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void initPixmapCache();
    // Keeps layout math in one place so painting can stay index-based.
    QRect computeCellRect(uint32_t index) const;

    // Load (and cache) the pixmap for a given plant type and stage.
    const QPixmap& plantPixmap(uint32_t plantType, uint32_t stage, bool withered);

    // Draw helpers that delegate to cached pixmaps.
    void drawGrowingPlant(QPainter& p, QRectF cell, uint32_t stage, uint32_t plantType);
    void drawWitheredPlant(QPainter& p, QRectF cell, uint32_t plantType);

    std::vector<FocusRecord> records_;
    QMap<QString, QPixmap> plantCache_;
    static constexpr int COLS = 8;
    static constexpr int CELL_SIZE = 64;
};

#endif // GARDENCANVAS_H
