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
    int rows = (static_cast<int>(records_.size()) + COLS - 1) / COLS;
    rows = qMax(4, rows);
    setMinimumHeight(rows * CELL_SIZE + 20);
    update();
}

void GardenCanvas::refresh()
{
    update();
}

QRect GardenCanvas::computeCellRect(uint32_t index) const
{
    int row = static_cast<int>(index) / COLS;
    int col = static_cast<int>(index) % COLS;
    int x = col * CELL_SIZE + 10;
    int y = row * CELL_SIZE + 10;
    return QRect(x, y, CELL_SIZE - 4, CELL_SIZE - 4);
}

void GardenCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor("#121915"));

    // 虚线草地网格
    int rows = qMax(4, (static_cast<int>(records_.size()) + COLS - 1) / COLS);
    double cellW = static_cast<double>(width() - 20) / COLS;
    double cellH = static_cast<double>(height() - 20) / rows;

    QPen gridPen;
    gridPen.setColor(QColor(46, 63, 52, 80));
    gridPen.setStyle(Qt::DotLine);
    gridPen.setWidth(1);
    painter.setPen(gridPen);

    for (int i = 1; i < COLS; ++i) {
        int x = static_cast<int>(i * cellW) + 10;
        painter.drawLine(x, 10, x, rows * static_cast<int>(cellH) + 10);
    }
    for (int i = 1; i < rows; ++i) {
        int y = static_cast<int>(i * cellH) + 10;
        painter.drawLine(10, y, COLS * static_cast<int>(cellW) + 10, y);
    }

    // 绘制植物记录
    for (size_t i = 0; i < records_.size(); ++i) {
        const auto& rec = records_[i];
        QRect cell = computeCellRect(static_cast<uint32_t>(i));

        if (rec.status == 0) {
            painter.fillRect(cell, QColor("#2e5a2e"));
            painter.setPen(QPen(QColor("#4E9F3D"), 1));
        } else {
            painter.fillRect(cell, QColor("#3a3a32"));
            painter.setPen(QPen(QColor("#6b6b5e"), 1));
        }

        painter.drawRoundedRect(cell, 4, 4);

        painter.setPen(QColor("#8A9A86"));
        painter.setFont(QFont("Microsoft YaHei", 7));
        painter.drawText(cell, Qt::AlignBottom | Qt::AlignHCenter, QString::number(i));
    }
}
