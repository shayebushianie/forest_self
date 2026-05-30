#ifndef GARDENCANVAS_H
#define GARDENCANVAS_H

#include <QWidget>
#include <QMap>
#include <QPixmap>
#include <vector>
#include "common/DatabaseCommon.h"

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
    QRect computeCellRect(uint32_t index) const;

    std::vector<FocusRecord> records_;
    QMap<uint32_t, QPixmap> plantCache_;
    static constexpr int COLS = 8;
    static constexpr int CELL_SIZE = 64;
};

#endif // GARDENCANVAS_H
