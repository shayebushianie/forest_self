#include "ui/GridSelectDialog.h"

GridSelectDialog::GridSelectDialog(DatabaseManager& db, QWidget* parent)
    : QDialog(parent), db_(db)
{
    setWindowTitle(QStringLiteral("选择小树的位置"));
    setFixedSize(360, 380);
    setObjectName("GridSelectDialog");

    auto* layout = new QGridLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(15, 15, 15, 15);

    std::vector<bool> occupied(64, false);
    auto records = db_.getAllRecords();
    for (const auto& rec : records) {
        if (rec.gridIndex < 64) occupied[rec.gridIndex] = true;
    }

    for (int i = 0; i < 64; ++i) {
        auto* btn = new QPushButton(this);
        btn->setFixedSize(36, 36);

        if (occupied[i]) {
            btn->setEnabled(false);
            btn->setStyleSheet("background-color: #24332b; border:1px solid #16201a; color:#4E9F3D; font-size:14px;");
            btn->setText(QStringLiteral("🌳"));
        } else {
            btn->setStyleSheet(
                "QPushButton { background-color:#121915; border:1px dashed #2e3f34; }"
                "QPushButton:hover { background-color:#4E9F3D; border-style:solid; }");
            QObject::connect(btn, &QPushButton::clicked, this, [this, i]() {
                selectedGridIndex_ = static_cast<uint8_t>(i);
                accept();
            });
        }
        layout->addWidget(btn, i / 8, i % 8);
    }
}
