#include "ui/GridSelectDialog.h"

#include <QLabel>

GridSelectDialog::GridSelectDialog(DatabaseManager& db, QWidget* parent)
    : QDialog(parent), db_(db)
{
    DialogPresenter::prepare(*this);
    setWindowTitle(QStringLiteral("选择小树的位置"));
    setFixedSize(360, 380);
    setObjectName("GridSelectDialog");

    auto* layout = new QGridLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(15, 15, 15, 15);

    std::vector<bool> occupied(64, false);
    std::vector<FocusRecord> records;
    if (!db_.getAllRecords(records)) {
        auto* error = new QLabel(QStringLiteral("无法读取已有位置，请关闭后重试。\n%1").arg(db_.lastError()), this);
        error->setWordWrap(true);
        layout->addWidget(error, 0, 0);
        auto* close = new QPushButton(QStringLiteral("关闭"), this);
        DialogPresenter::setSecondary(close);
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        layout->addWidget(close, 1, 0);
        return;
    }
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
