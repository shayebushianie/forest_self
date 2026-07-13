#include "ui/GardenCanvas.h"
#include "config/PlantCatalog.h"
#include "ui/PlantImageUtils.h"
#include <QPainter>

GardenCanvas::GardenCanvas(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(COLS * CELL_SIZE + 20, 200);
    initPixmapCache();
}

void GardenCanvas::initPixmapCache()
{
    // Cache is populated lazily in plantPixmap().
    // Pre-populate with a few to avoid first-paint stutter.
    plantCache_.clear();
}

const QPixmap& GardenCanvas::plantPixmap(uint32_t plantType, uint32_t stage, bool withered)
{
    const QString path = PlantCatalog::stageImagePath(plantType, stage, withered);
    const QString key = path;

    auto it = plantCache_.find(key);
    if (it != plantCache_.end()) {
        return it.value();
    }

    QPixmap pm = PlantImageUtils::loadPlantIcon(path);
    if (pm.isNull()) {
        static const QPixmap nullPm;
        return nullPm;
    }
    plantCache_[key] = pm;
    return plantCache_[key];
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

    QLinearGradient bg(rect().topLeft(), rect().bottomRight());
    bg.setColorAt(0.0, QColor("#61B99B"));
    bg.setColorAt(0.62, QColor("#58AD8F"));
    bg.setColorAt(1.0, QColor("#4B967D"));
    painter.fillRect(rect(), bg);

    QRectF fieldRect(16, 12, width() - 32, height() - 24);
    painter.setBrush(QColor(255, 255, 255, 28));
    painter.setPen(QPen(QColor(255, 255, 255, 62), 1));
    painter.drawRoundedRect(fieldRect, 8, 8);

    double cellW = fieldRect.width() / COLS;
    double cellH = fieldRect.height() / COLS;

    QPen gridPen;
    gridPen.setColor(QColor(255, 255, 255, 45));
    gridPen.setStyle(Qt::DotLine);
    gridPen.setWidth(1);
    painter.setPen(gridPen);

    for (int i = 1; i < COLS; ++i) {
        int x = static_cast<int>(fieldRect.left() + i * cellW);
        painter.drawLine(x, static_cast<int>(fieldRect.top()),
                         x, static_cast<int>(fieldRect.bottom()));
    }
    for (int i = 1; i < COLS; ++i) {
        int y = static_cast<int>(fieldRect.top() + i * cellH);
        painter.drawLine(static_cast<int>(fieldRect.left()), y,
                         static_cast<int>(fieldRect.right()), y);
    }

    for (const auto& rec : records_) {
        if (rec.gridIndex >= 64) continue;
        int row = rec.gridIndex / COLS;
        int col = rec.gridIndex % COLS;
        QRectF cell(fieldRect.left() + col * cellW + 6,
                    fieldRect.top() + row * cellH + 6,
                    cellW - 12,
                    cellH - 12);

        QRectF shadow = cell.translated(0, 3);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(38, 94, 74, 65));
        painter.drawRoundedRect(shadow, 7, 7);

        QLinearGradient tileGrad(cell.topLeft(), cell.bottomRight());
        if (rec.status == FocusRecordStatus::Success) {
            tileGrad.setColorAt(0.0, QColor("#DDEB75"));
            tileGrad.setColorAt(1.0, QColor("#7FCB55"));
            painter.setPen(QPen(QColor("#F2F4C6"), 1));
        } else {
            tileGrad.setColorAt(0.0, QColor("#9B8D62"));
            tileGrad.setColorAt(1.0, QColor("#6F5B3D"));
            painter.setPen(QPen(QColor("#C2B181"), 1));
        }
        painter.setBrush(tileGrad);
        painter.drawRoundedRect(cell, 7, 7);

        if (rec.status == FocusRecordStatus::Success) {
            drawGrowingPlant(painter, cell, rec.growthStage, rec.plantType);
        } else {
            drawWitheredPlant(painter, cell, rec.plantType);
        }
    }
}

// ---------------------------------------------------------------------------
// Plant drawing — image-based (replaces procedural QPainter drawing)
// ---------------------------------------------------------------------------

void GardenCanvas::drawGrowingPlant(QPainter& p, QRectF cell,
                                     uint32_t stage, uint32_t plantType)
{
    const QPixmap& pm = plantPixmap(plantType, stage, false);
    if (pm.isNull()) return;

    // Scale the pixmap to fit the cell while keeping aspect ratio,
    // centered in the cell.
    QSizeF targetSize = pm.size().scaled(cell.width(), cell.height(),
                                          Qt::KeepAspectRatio);
    QRectF targetRect(
        cell.center().x() - targetSize.width() / 2,
        cell.center().y() - targetSize.height() / 2,
        targetSize.width(),
        targetSize.height());

    p.drawPixmap(targetRect.toRect(), pm);
}

void GardenCanvas::drawWitheredPlant(QPainter& p, QRectF cell, uint32_t plantType)
{
    const QPixmap& pm = plantPixmap(plantType, 0, true);
    if (pm.isNull()) return;

    QSizeF targetSize = pm.size().scaled(cell.width(), cell.height(),
                                          Qt::KeepAspectRatio);
    QRectF targetRect(
        cell.center().x() - targetSize.width() / 2,
        cell.center().y() - targetSize.height() / 2,
        targetSize.width(),
        targetSize.height());

    p.drawPixmap(targetRect.toRect(), pm);
}
