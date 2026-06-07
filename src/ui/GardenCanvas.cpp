#include "ui/GardenCanvas.h"
#include <QPainter>

GardenCanvas::GardenCanvas(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(COLS * CELL_SIZE + 20, 200);
    initPixmapCache();
}

void GardenCanvas::initPixmapCache()
{
    for (uint32_t type = 0; type <= 3; ++type) {
        plantCache_[type] = QPixmap(CELL_SIZE, CELL_SIZE);
    }
}

void GardenCanvas::loadRecords(const std::vector<FocusRecord>& records)
{
    records_ = records;
    setMinimumHeight(8 * CELL_SIZE + 20);
    update();
}

void GardenCanvas::refresh()
{
    update();
}

void GardenCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor("#121915"));

    double cellW = static_cast<double>(width() - 20) / COLS;
    double cellH = static_cast<double>(height() - 20) / COLS;

    QPen gridPen;
    gridPen.setColor(QColor(46, 63, 52, 80));
    gridPen.setStyle(Qt::DotLine);
    gridPen.setWidth(1);
    painter.setPen(gridPen);

    for (int i = 1; i < COLS; ++i) {
        int x = static_cast<int>(i * cellW) + 10;
        painter.drawLine(x, 10, x, COLS * static_cast<int>(cellH) + 10);
    }
    for (int i = 1; i < COLS; ++i) {
        int y = static_cast<int>(i * cellH) + 10;
        painter.drawLine(10, y, COLS * static_cast<int>(cellW) + 10, y);
    }

    for (const auto& rec : records_) {
        if (rec.gridIndex >= 64) continue;
        int row = rec.gridIndex / COLS;
        int col = rec.gridIndex % COLS;
        QRect cell(static_cast<int>(col * cellW) + 12,
                   static_cast<int>(row * cellH) + 12,
                   static_cast<int>(cellW) - 8,
                   static_cast<int>(cellH) - 8);

        if (rec.status == 0) {
            painter.fillRect(cell, QColor("#2e5a2e"));
            painter.setPen(QPen(QColor("#4E9F3D"), 1));
        } else {
            painter.fillRect(cell, QColor("#3a3a32"));
            painter.setPen(QPen(QColor("#6b6b5e"), 1));
        }
        painter.drawRoundedRect(cell, 4, 4);
    }
}
