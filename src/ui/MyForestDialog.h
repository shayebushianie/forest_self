#ifndef MYFORESTDIALOG_H
#define MYFORESTDIALOG_H

#include <QDialog>
#include "ui/DialogPresenter.h"
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVector>
#include <vector>
#include <cstdint>

#include "core/ForestLayoutManager.h"

class DatabaseManager;
struct FocusRecord;

class ForestCanvas : public QWidget {
    Q_OBJECT

public:
    explicit ForestCanvas(QWidget* parent = nullptr);

    void setData(const std::vector<FocusRecord>* records,
                 QVector<ForestPlacement>* placements);
    void setPendingRecordId(int id) { m_pendingRecordId = id; }
    int  pendingRecordId() const { return m_pendingRecordId; }
    void clearPending() { m_pendingRecordId = -1; }

    void refresh() { update(); }

signals:
    void treePlaced(int recordId, int posX, int posY);
    void treeMoved(int recordId, int posX, int posY);
    void treeRemoved(int recordId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int hitTest(QPoint pos) const;
    void drawPlant(QPainter& painter, int x, int y,
                   const FocusRecord& record) const;
    const FocusRecord* findRecord(int32_t recordId) const;

    const std::vector<FocusRecord>* m_records  = nullptr;
    QVector<ForestPlacement>*       m_placements = nullptr;

    int     m_pendingRecordId = -1;
    bool    m_dragging   = false;
    int     m_dragRecordId = -1;
    QPoint  m_dragOffset;
    QPoint  m_dragCurrentPos;
};

class MyForestDialog : public QDialog {
    Q_OBJECT

public:
    explicit MyForestDialog(ForestLayoutManager* layout,
                            DatabaseManager* db,
                            QWidget* parent = nullptr);

private slots:
    void onInventorySelectionChanged();
    void onTreePlaced(int recordId, int posX, int posY);
    void onTreeMoved(int recordId, int posX, int posY);
    void onTreeRemoved(int recordId);
    void onSave();

private:
    void setupUI();
    void populateInventory();
    QVector<int> availableRecordIds() const;
    static QString plantIconForType(uint32_t plantType);

    ForestLayoutManager*   m_layout;
    DatabaseManager*       m_db;

    std::vector<FocusRecord> m_allRecords;

    ForestCanvas*  m_canvas;
    QListWidget*   m_inventoryList;
    QPushButton*   m_saveBtn;
    QLabel*        m_hintLabel;
};

#endif
