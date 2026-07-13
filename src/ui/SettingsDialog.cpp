#include "ui/SettingsDialog.h"
#include "config/PlantCatalog.h"
#include "system/RuleEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>

SettingsDialog::SettingsDialog(RuleEngine& ruleEngine, QWidget* parent)
    : QDialog(parent), ruleEngine_(ruleEngine)
{
    DialogPresenter::prepare(*this);
    setWindowTitle(QStringLiteral("专注设置"));
    setMinimumWidth(400);

    auto* mainLayout = new QVBoxLayout(this);

    // -- 植物选择 --
    auto* plantGroup = new QGroupBox(QStringLiteral("植物选择"));
    auto* plantLayout = new QHBoxLayout(plantGroup);
    plantCombo_ = new QComboBox;
    for (const PlantDefinition& plant : PlantCatalog::all()) {
        plantCombo_->addItem(QStringLiteral("%1 (%2)").arg(plant.displayName, plant.internalName), plant.type);
    }
    plantLayout->addWidget(new QLabel(QStringLiteral("选择植物:")));
    plantLayout->addWidget(plantCombo_);
    mainLayout->addWidget(plantGroup);

    // -- 计时模式 --
    auto* modeGroup = new QGroupBox(QStringLiteral("计时模式"));
    auto* modeLayout = new QHBoxLayout(modeGroup);
    modeCombo_ = new QComboBox;
    modeCombo_->addItem(QStringLiteral("倒计时 (番茄钟)"), 0);
    modeCombo_->addItem(QStringLiteral("正计时 (自由专注)"), 1);
    modeLayout->addWidget(new QLabel(QStringLiteral("模式:")));
    modeLayout->addWidget(modeCombo_);
    mainLayout->addWidget(modeGroup);

    // -- 深度专注 --
    auto* focusGroup = new QGroupBox(QStringLiteral("深度专注"));
    auto* focusLayout = new QHBoxLayout(focusGroup);
    deepFocusCheck_ = new QCheckBox(QStringLiteral("开启后，离开允许窗口会触发倒计时警告"));
    deepFocusCheck_->setChecked(true);
    deepFocusCheck_->setCursor(Qt::PointingHandCursor);
    focusLayout->addWidget(deepFocusCheck_);
    mainLayout->addWidget(focusGroup);

    // -- 时长设置 --
    auto* timeGroup = new QGroupBox(QStringLiteral("专注时长"));
    auto* timeLayout = new QHBoxLayout(timeGroup);
    minutesSpin_ = new QSpinBox;
    minutesSpin_->setRange(10, 120);
    minutesSpin_->setValue(25);
    minutesSpin_->setSuffix(QStringLiteral(" 分钟"));
    timeLayout->addWidget(new QLabel(QStringLiteral("时长:")));
    timeLayout->addWidget(minutesSpin_);
    mainLayout->addWidget(timeGroup);

    // -- 标签选择 --
    auto* tagGroup = new QGroupBox(QStringLiteral("标签"));
    auto* tagLayout = new QHBoxLayout(tagGroup);
    tagCombo_ = new QComboBox;
    tagCombo_->addItem(QStringLiteral("无标签"), 0);
    tagCombo_->addItem(QStringLiteral("学习"), 1);
    tagCombo_->addItem(QStringLiteral("写代码"), 2);
    tagCombo_->addItem(QStringLiteral("阅读"), 3);
    tagCombo_->addItem(QStringLiteral("运动"), 4);
    tagLayout->addWidget(new QLabel(QStringLiteral("标签:")));
    tagLayout->addWidget(tagCombo_);
    mainLayout->addWidget(tagGroup);

    // -- 黑名单 --
    auto* blGroup = new QGroupBox(QStringLiteral("进程黑名单"));
    auto* blLayout = new QVBoxLayout(blGroup);
    blacklistWidget_ = new QListWidget;
    for (const QString& name : ruleEngine_.blacklist()) {
        blacklistWidget_->addItem(name);
    }
    blLayout->addWidget(blacklistWidget_);

    auto* inputLayout = new QHBoxLayout;
    blacklistInput_ = new QLineEdit;
    blacklistInput_->setPlaceholderText(QStringLiteral("输入进程名，如 chrome.exe"));
    auto* addBtn = new QPushButton(QStringLiteral("添加"));
    auto* removeBtn = new QPushButton(QStringLiteral("移除"));
    inputLayout->addWidget(blacklistInput_);
    inputLayout->addWidget(addBtn);
    inputLayout->addWidget(removeBtn);
    blLayout->addLayout(inputLayout);
    mainLayout->addWidget(blGroup);

    QObject::connect(addBtn, &QPushButton::clicked, this, &SettingsDialog::onAddBlacklist);
    QObject::connect(removeBtn, &QPushButton::clicked, this, &SettingsDialog::onRemoveBlacklist);
    QObject::connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &SettingsDialog::onModeChanged);

    // -- 确认按钮 --
    auto* okBtn = new QPushButton(QStringLiteral("确 定"));
    QObject::connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addWidget(okBtn);
}

uint32_t SettingsDialog::selectedPlantType() const
{
    return static_cast<uint32_t>(plantCombo_->currentData().toUInt());
}

uint32_t SettingsDialog::selectedMinutes() const
{
    return static_cast<uint32_t>(minutesSpin_->value());
}

uint32_t SettingsDialog::selectedTagId() const
{
    return static_cast<uint32_t>(tagCombo_->currentData().toUInt());
}

bool SettingsDialog::isStopwatchMode() const
{
    return modeCombo_->currentData().toUInt() == 1;
}

bool SettingsDialog::isDeepFocusEnabled() const
{
    return deepFocusCheck_->isChecked();
}

void SettingsDialog::onModeChanged(int index)
{
    minutesSpin_->setEnabled(index == 0);
}

void SettingsDialog::onAddBlacklist()
{
    QString name = blacklistInput_->text().trimmed();
    if (name.isEmpty()) return;
    ruleEngine_.addToBlacklist(name);
    blacklistWidget_->addItem(name);
    blacklistInput_->clear();
}

void SettingsDialog::onRemoveBlacklist()
{
    auto* item = blacklistWidget_->currentItem();
    if (!item) return;
    ruleEngine_.removeFromBlacklist(item->text());
    delete blacklistWidget_->takeItem(blacklistWidget_->row(item));
}
