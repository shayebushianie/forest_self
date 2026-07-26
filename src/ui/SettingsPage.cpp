#include "ui/SettingsPage.h"

#include "ui/AppStyle.h"
#include "ui/SproutToggle.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

class SettingsSurface final : public QFrame {
public:
    using QFrame::QFrame;

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QFrame::resizeEvent(event);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), 22, 22);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }
};

class ChoiceRow final : public QWidget {
public:
    ChoiceRow(const QString& title, const QString& description, const QString& rowName,
              const QString& comboName, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(rowName);
        setProperty("settingsTransparent", true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setAccessibleName(title);
        setToolTip(description);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(16, 10, 14, 10);
        layout->setSpacing(18);
        auto* copy = new QWidget(this);
        copy->setProperty("settingsTransparent", true);
        copy->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* copyLayout = new QVBoxLayout(copy);
        copyLayout->setContentsMargins(0, 0, 0, 0);
        copyLayout->setSpacing(2);
        auto* titleLabel = new QLabel(title, copy);
        titleLabel->setProperty("settingsRowTitle", true);
        auto* descriptionLabel = new QLabel(description, copy);
        descriptionLabel->setProperty("settingsRowDescription", true);
        descriptionLabel->setWordWrap(true);
        copyLayout->addWidget(titleLabel);
        copyLayout->addWidget(descriptionLabel);
        layout->addWidget(copy, 1);

        combo_ = new QComboBox(this);
        combo_->setObjectName(comboName);
        combo_->setAccessibleName(title);
        combo_->setToolTip(description);
        combo_->setMinimumWidth(168);
        layout->addWidget(combo_);
    }

    QComboBox* combo() const { return combo_; }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && isEnabled()) {
            pointerPressActive_ = true;
            pressed_ = true;
            update();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (pointerPressActive_) {
            const bool pressed = rect().contains(event->position().toPoint());
            if (pressed_ != pressed) {
                pressed_ = pressed;
                update();
            }
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const bool activate = pointerPressActive_ && pressed_ && event->button() == Qt::LeftButton
            && rect().contains(event->position().toPoint());
        pointerPressActive_ = false;
        pressed_ = false;
        update();
        if (activate) {
            combo_->setFocus();
            combo_->showPopup();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter) {
            combo_->setFocus();
            combo_->showPopup();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QWidget::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (pressed_)
            painter.fillRect(rect(), QColor(32, 91, 70, 20));
        else if (underMouse())
            painter.fillRect(rect(), QColor(255, 255, 255, 55));
        if (AppStyle::keyboardFocusVisible(this)) {
            painter.fillRect(rect(), QColor(80, 145, 111, 22));
            painter.setPen(QPen(QColor("#D4B844"), 3, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(4, height() / 2.0 - 10), QPointF(4, height() / 2.0 + 10));
        }
    }

private:
    QComboBox* combo_ = nullptr;
    bool pointerPressActive_ = false;
    bool pressed_ = false;
};

class ControlRow final : public QWidget {
public:
    ControlRow(const QString& title, const QString& description, QWidget* control,
               QWidget* parent = nullptr)
        : QWidget(parent),
          control_(control)
    {
        setProperty("settingsRow", true);
        setProperty("settingsTransparent", true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setAccessibleName(title);
        setToolTip(description);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && isEnabled()) {
            pressActive_ = true;
            pointerInside_ = true;
            update();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (pressActive_) {
            pointerInside_ = rect().contains(event->position().toPoint());
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const bool activate = pressActive_ && pointerInside_ && event->button() == Qt::LeftButton
            && rect().contains(event->position().toPoint());
        pressActive_ = false;
        pointerInside_ = true;
        update();
        if (activate) {
            control_->setFocus();
            if (auto* button = qobject_cast<QAbstractButton*>(control_)) button->click();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QWidget::paintEvent(event);
        QPainter painter(this);
        if (pressActive_ && pointerInside_)
            painter.fillRect(rect(), QColor(32, 91, 70, 20));
        else if (underMouse())
            painter.fillRect(rect(), QColor(255, 255, 255, 38));
    }

private:
    QWidget* control_ = nullptr;
    bool pressActive_ = false;
    bool pointerInside_ = true;
};

QIcon categoryIcon(int index)
{
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#3F765E"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (index == 0) {
        painter.drawLine(QPointF(14, 21), QPointF(14, 8));
        painter.drawEllipse(QRectF(7, 7, 8, 5));
        painter.drawEllipse(QRectF(14, 10, 8, 5));
    } else if (index == 1) {
        painter.drawEllipse(QRectF(6, 8, 16, 12));
        painter.drawEllipse(QRectF(11, 11, 6, 6));
    } else if (index == 2) {
        painter.drawEllipse(QRectF(9, 9, 10, 10));
        for (int angle = 0; angle < 8; ++angle) {
            const qreal radians = angle * 3.141592653589793 / 4.0;
            painter.drawLine(QPointF(14 + qCos(radians) * 8, 14 + qSin(radians) * 8),
                             QPointF(14 + qCos(radians) * 11, 14 + qSin(radians) * 11));
        }
    } else {
        painter.drawRoundedRect(QRectF(6, 7, 16, 15), 3, 3);
        painter.drawLine(QPointF(9, 12), QPointF(19, 12));
        painter.drawLine(QPointF(9, 16), QPointF(17, 16));
    }
    return QIcon(pixmap);
}

QFrame* createGroup(QWidget* parent)
{
    auto* group = new QFrame(parent);
    group->setProperty("settingsListGroup", true);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return group;
}

void addSeparator(QVBoxLayout* layout)
{
    auto* separator = new QFrame;
    separator->setProperty("settingsSeparator", true);
    separator->setFixedHeight(1);
    layout->addWidget(separator);
}

QWidget* createCopy(const QString& title, const QString& description, QWidget* parent)
{
    auto* copy = new QWidget(parent);
    copy->setProperty("settingsTransparent", true);
    copy->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* layout = new QVBoxLayout(copy);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto* titleLabel = new QLabel(title, copy);
    titleLabel->setProperty("settingsRowTitle", true);
    auto* descriptionLabel = new QLabel(description, copy);
    descriptionLabel->setProperty("settingsRowDescription", true);
    descriptionLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    return copy;
}

QWidget* createControlRow(const QString& title, const QString& description, QWidget* control,
                          QWidget* parent)
{
    auto* row = new ControlRow(title, description, control, parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(16, 10, 14, 10);
    layout->setSpacing(18);
    layout->addWidget(createCopy(title, description, row), 1);
    layout->addWidget(control);
    return row;
}

QVBoxLayout* createCategory(QStackedWidget* stack, const QString& objectName, const QString& title,
                            const QString& description)
{
    auto* scroll = new QScrollArea(stack);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    content->setObjectName(objectName);
    content->setProperty("settingsTransparent", true);
    content->setMaximumWidth(900);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(34, 28, 34, 36);
    layout->setSpacing(8);
    auto* titleLabel = new QLabel(title, content);
    titleLabel->setProperty("settingsPageTitle", true);
    auto* descriptionLabel = new QLabel(description, content);
    descriptionLabel->setProperty("settingsPageDescription", true);
    descriptionLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addSpacing(14);
    scroll->setWidget(content);
    stack->addWidget(scroll);
    return layout;
}

void addSectionLabel(QVBoxLayout* layout, const QString& text)
{
    auto* label = new QLabel(text);
    label->setProperty("settingsSectionLabel", true);
    layout->addSpacing(6);
    layout->addWidget(label);
}

QPushButton* createAction(const QString& objectName, const QString& title,
                          const QString& description, bool dangerous = false)
{
    auto* button = new QPushButton(QStringLiteral("%1\n%2").arg(title, description));
    button->setObjectName(objectName);
    button->setProperty("settingsActionRow", true);
    button->setProperty("dangerousAction", dangerous);
    button->setAccessibleName(title);
    button->setAccessibleDescription(description);
    button->setToolTip(description);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

} // namespace

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("settingsPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(20, 20, 20, 20);
    rootLayout->setSpacing(0);

    auto* shell = new SettingsSurface(this);
    shell->setObjectName(QStringLiteral("settingsGlassShell"));
    auto* shellLayout = new QHBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    rootLayout->addWidget(shell);

    auto* rail = new QWidget(shell);
    rail->setObjectName(QStringLiteral("settingsRail"));
    rail->setFixedWidth(196);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(16, 24, 16, 18);
    railLayout->setSpacing(8);
    auto* railTitle = new QLabel(QStringLiteral("系统设置"), rail);
    railTitle->setProperty("settingsRailTitle", true);
    railLayout->addWidget(railTitle);

    auto* categories = new QListWidget(rail);
    categories->setObjectName(QStringLiteral("settingsCategoryNav"));
    categories->setAccessibleName(QStringLiteral("设置分类"));
    categories->setFocusPolicy(Qt::StrongFocus);
    const QStringList categoryNames = {
        QStringLiteral("专注设置"), QStringLiteral("监督规则"),
        QStringLiteral("外观与无障碍"), QStringLiteral("数据与备份")
    };
    for (int index = 0; index < categoryNames.size(); ++index)
        categories->addItem(new QListWidgetItem(categoryIcon(index), categoryNames[index]));
    categories->setCurrentRow(0);
    railLayout->addWidget(categories);
    railLayout->addStretch();
    shellLayout->addWidget(rail);

    auto* contentPanel = new QFrame(shell);
    contentPanel->setObjectName(QStringLiteral("settingsContentPanel"));
    auto* contentPanelLayout = new QVBoxLayout(contentPanel);
    contentPanelLayout->setContentsMargins(0, 0, 0, 0);
    contentPanelLayout->setSpacing(0);
    auto* stack = new QStackedWidget(contentPanel);
    stack->setObjectName(QStringLiteral("settingsCategoryStack"));
    contentPanelLayout->addWidget(stack);
    shellLayout->addWidget(contentPanel, 1);
    connect(categories, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);

    auto* focusLayout = createCategory(
        stack, QStringLiteral("focusSettingsContent"), QStringLiteral("专注设置"),
        QStringLiteral("设置下一次专注的节奏、植物与项目归属。"));
    addSectionLabel(focusLayout, QStringLiteral("下一次专注"));
    auto* nextFocusGroup = createGroup(this);
    auto* nextFocusLayout = qobject_cast<QVBoxLayout*>(nextFocusGroup->layout());

    auto* plantRow = new ChoiceRow(QStringLiteral("专注植物"),
                                   QStringLiteral("专注完成后成长的植物"),
                                   QStringLiteral("focusPlantRow"),
                                   QStringLiteral("focusPlantSelector"), nextFocusGroup);
    plantCombo_ = plantRow->combo();
    nextFocusLayout->addWidget(plantRow);
    addSeparator(nextFocusLayout);
    auto* modeRow = new ChoiceRow(QStringLiteral("计时模式"),
                                  QStringLiteral("倒计时或自由专注"),
                                  QStringLiteral("focusModeRow"),
                                  QStringLiteral("focusModeSelector"), nextFocusGroup);
    modeCombo_ = modeRow->combo();
    modeCombo_->addItem(QStringLiteral("倒计时"), 0);
    modeCombo_->addItem(QStringLiteral("正计时"), 1);
    nextFocusLayout->addWidget(modeRow);
    addSeparator(nextFocusLayout);
    minutesSpin_ = new QSpinBox(nextFocusGroup);
    minutesSpin_->setObjectName(QStringLiteral("focusMinutesSelector"));
    minutesSpin_->setAccessibleName(QStringLiteral("专注时长"));
    minutesSpin_->setToolTip(QStringLiteral("仅倒计时模式生效"));
    minutesSpin_->setRange(10, 120);
    minutesSpin_->setSuffix(QStringLiteral(" 分钟"));
    minutesSpin_->setMinimumWidth(132);
    nextFocusLayout->addWidget(createControlRow(
        QStringLiteral("专注时长"), QStringLiteral("仅倒计时模式生效"), minutesSpin_, nextFocusGroup));
    addSeparator(nextFocusLayout);
    auto* tagRow = new ChoiceRow(QStringLiteral("项目标签"),
                                 QStringLiteral("用于森林中的分类与统计"),
                                 QStringLiteral("focusTagRow"),
                                 QStringLiteral("focusTagSelector"), nextFocusGroup);
    tagCombo_ = tagRow->combo();
    nextFocusLayout->addWidget(tagRow);
    addSeparator(nextFocusLayout);
    oathInput_ = new QLineEdit(nextFocusGroup);
    oathInput_->setObjectName(QStringLiteral("focusOathInput"));
    oathInput_->setAccessibleName(QStringLiteral("本次专注心愿"));
    oathInput_->setPlaceholderText(QStringLiteral("写下一句专注心愿…"));
    oathInput_->setMinimumWidth(240);
    nextFocusLayout->addWidget(createControlRow(
        QStringLiteral("本次专注心愿"), QStringLiteral("专注开始后显示在计时页面"),
        oathInput_, nextFocusGroup));
    focusLayout->addWidget(nextFocusGroup);

    addSectionLabel(focusLayout, QStringLiteral("专注行为"));
    auto* behaviorGroup = createGroup(this);
    auto* behaviorLayout = qobject_cast<QVBoxLayout*>(behaviorGroup->layout());
    deepFocusToggle_ = new SproutToggle(behaviorGroup);
    deepFocusToggle_->setObjectName(QStringLiteral("focusDeepToggle"));
    deepFocusToggle_->setAccessibleName(QStringLiteral("深度专注"));
    deepFocusToggle_->setToolTip(QStringLiteral("离开允许的窗口时发出提醒"));
    behaviorLayout->addWidget(createControlRow(
        QStringLiteral("深度专注"), QStringLiteral("离开允许的窗口时发出提醒"),
        deepFocusToggle_, behaviorGroup));
    addSeparator(behaviorLayout);
    allowPauseToggle_ = new SproutToggle(behaviorGroup);
    allowPauseToggle_->setObjectName(QStringLiteral("focusPauseToggle"));
    allowPauseToggle_->setAccessibleName(QStringLiteral("允许暂停"));
    allowPauseToggle_->setToolTip(QStringLiteral("专注过程中保留暂停入口"));
    behaviorLayout->addWidget(createControlRow(
        QStringLiteral("允许暂停"), QStringLiteral("专注过程中保留暂停入口"),
        allowPauseToggle_, behaviorGroup));
    addSeparator(behaviorLayout);
    guardianGoalSpin_ = new QSpinBox(behaviorGroup);
    guardianGoalSpin_->setObjectName(QStringLiteral("guardianGoalSelector"));
    guardianGoalSpin_->setAccessibleName(QStringLiteral("每日守护目标"));
    guardianGoalSpin_->setToolTip(QStringLiteral("用于时间守护页面的完成进度"));
    guardianGoalSpin_->setRange(10, 600);
    guardianGoalSpin_->setSuffix(QStringLiteral(" 分钟/天"));
    guardianGoalSpin_->setMinimumWidth(144);
    behaviorLayout->addWidget(createControlRow(
        QStringLiteral("每日守护目标"), QStringLiteral("用于时间守护页面的完成进度"),
        guardianGoalSpin_, behaviorGroup));
    focusLayout->addWidget(behaviorGroup);
    focusLayout->addStretch();

    auto* rulesLayout = createCategory(
        stack, QStringLiteral("rulesSettingsContent"), QStringLiteral("监督规则"),
        QStringLiteral("管理深度专注期间需要避开的应用程序。"));
    addSectionLabel(rulesLayout, QStringLiteral("进程黑名单"));
    auto* blacklistGroup = createGroup(this);
    auto* blacklistLayout = qobject_cast<QVBoxLayout*>(blacklistGroup->layout());
    blacklistWidget_ = new QListWidget(blacklistGroup);
    blacklistWidget_->setObjectName(QStringLiteral("settingsBlacklistList"));
    blacklistWidget_->setAccessibleName(QStringLiteral("进程黑名单"));
    blacklistWidget_->setMinimumHeight(230);
    blacklistLayout->addWidget(blacklistWidget_);
    addSeparator(blacklistLayout);
    auto* blacklistInputRow = new QWidget(blacklistGroup);
    blacklistInputRow->setProperty("settingsTransparent", true);
    auto* blacklistInputLayout = new QHBoxLayout(blacklistInputRow);
    blacklistInputLayout->setContentsMargins(16, 12, 14, 12);
    blacklistInput_ = new QLineEdit(blacklistInputRow);
    blacklistInput_->setObjectName(QStringLiteral("settingsBlacklistInput"));
    blacklistInput_->setAccessibleName(QStringLiteral("输入进程名"));
    blacklistInput_->setPlaceholderText(QStringLiteral("输入进程名，如 chrome.exe"));
    auto* addButton = new QPushButton(QStringLiteral("添加"), blacklistInputRow);
    addButton->setObjectName(QStringLiteral("settingsBlacklistAdd"));
    addButton->setAccessibleName(QStringLiteral("添加进程到黑名单"));
    addButton->setCursor(Qt::PointingHandCursor);
    auto* removeButton = new QPushButton(QStringLiteral("移除所选"), blacklistInputRow);
    removeButton->setObjectName(QStringLiteral("settingsBlacklistRemove"));
    removeButton->setAccessibleName(QStringLiteral("从黑名单移除所选进程"));
    removeButton->setCursor(Qt::PointingHandCursor);
    blacklistInputLayout->addWidget(blacklistInput_, 1);
    blacklistInputLayout->addWidget(addButton);
    blacklistInputLayout->addWidget(removeButton);
    blacklistLayout->addWidget(blacklistInputRow);
    rulesLayout->addWidget(blacklistGroup);
    rulesLayout->addStretch();

    auto* appearanceLayout = createCategory(
        stack, QStringLiteral("appearanceSettingsContent"), QStringLiteral("外观与无障碍"),
        QStringLiteral("调整文字、动态反馈和键盘焦点的可辨识度。"));
    addSectionLabel(appearanceLayout, QStringLiteral("阅读与反馈"));
    auto* accessibilityGroup = createGroup(this);
    auto* accessibilityLayout = qobject_cast<QVBoxLayout*>(accessibilityGroup->layout());
    fontScaleCombo_ = new QComboBox(accessibilityGroup);
    fontScaleCombo_->setObjectName(QStringLiteral("accessibilityFontScale"));
    fontScaleCombo_->setAccessibleName(QStringLiteral("文字大小"));
    fontScaleCombo_->setMinimumWidth(150);
    fontScaleCombo_->addItem(QStringLiteral("较小 · 90%"), 90);
    fontScaleCombo_->addItem(QStringLiteral("默认 · 100%"), 100);
    fontScaleCombo_->addItem(QStringLiteral("较大 · 110%"), 110);
    fontScaleCombo_->addItem(QStringLiteral("最大 · 125%"), 125);
    accessibilityLayout->addWidget(createControlRow(
        QStringLiteral("文字大小"), QStringLiteral("同步调整应用文字与布局"),
        fontScaleCombo_, accessibilityGroup));
    addSeparator(accessibilityLayout);
    reducedMotionToggle_ = new SproutToggle(accessibilityGroup);
    reducedMotionToggle_->setObjectName(QStringLiteral("accessibilityReducedMotion"));
    reducedMotionToggle_->setAccessibleName(QStringLiteral("减少动效"));
    reducedMotionToggle_->setToolTip(QStringLiteral("停止非必要的布局和装饰运动"));
    accessibilityLayout->addWidget(createControlRow(
        QStringLiteral("减少动效"), QStringLiteral("停止非必要的布局和装饰运动"),
        reducedMotionToggle_, accessibilityGroup));
    addSeparator(accessibilityLayout);
    highContrastToggle_ = new SproutToggle(accessibilityGroup);
    highContrastToggle_->setObjectName(QStringLiteral("accessibilityHighContrast"));
    highContrastToggle_->setAccessibleName(QStringLiteral("增强对比度"));
    highContrastToggle_->setToolTip(QStringLiteral("增强控件边界与键盘焦点"));
    accessibilityLayout->addWidget(createControlRow(
        QStringLiteral("增强对比度"), QStringLiteral("增强控件边界与键盘焦点"),
        highContrastToggle_, accessibilityGroup));
    appearanceLayout->addWidget(accessibilityGroup);
    appearanceLayout->addStretch();

    auto* dataLayout = createCategory(
        stack, QStringLiteral("dataSettingsContent"), QStringLiteral("数据与备份"),
        QStringLiteral("查看本地数据状态，并创建备份、恢复或导出专注记录。"));
    addSectionLabel(dataLayout, QStringLiteral("本地数据"));
    auto* dataSummaryGroup = createGroup(this);
    auto* dataSummaryLayout = qobject_cast<QVBoxLayout*>(dataSummaryGroup->layout());
    dataSummaryLabel_ = new QLabel(dataSummaryGroup);
    dataSummaryLabel_->setObjectName(QStringLiteral("settingsDataSummary"));
    dataSummaryLabel_->setWordWrap(true);
    dataSummaryLabel_->setContentsMargins(16, 14, 16, 14);
    dataSummaryLayout->addWidget(dataSummaryLabel_);
    dataLayout->addWidget(dataSummaryGroup);
    addSectionLabel(dataLayout, QStringLiteral("数据操作"));
    auto* dataActionGroup = createGroup(this);
    auto* dataActionLayout = qobject_cast<QVBoxLayout*>(dataActionGroup->layout());
    auto* backupButton = createAction(
        QStringLiteral("settingsBackupAction"), QStringLiteral("创建备份"),
        QStringLiteral("选择安全位置保存当前数据副本"));
    auto* restoreButton = createAction(
        QStringLiteral("settingsRestoreAction"), QStringLiteral("从备份恢复"),
        QStringLiteral("重启后恢复，并在恢复前创建安全快照"), true);
    auto* exportButton = createAction(
        QStringLiteral("settingsExportAction"), QStringLiteral("导出专注记录"),
        QStringLiteral("将专注历史保存为 CSV 文件"));
    auto* openDataButton = createAction(
        QStringLiteral("settingsOpenDataAction"), QStringLiteral("打开数据目录"),
        QStringLiteral("在文件管理器中查看本地 Forest 数据"));
    dataActionLayout->addWidget(backupButton);
    addSeparator(dataActionLayout);
    dataActionLayout->addWidget(restoreButton);
    addSeparator(dataActionLayout);
    dataActionLayout->addWidget(exportButton);
    addSeparator(dataActionLayout);
    dataActionLayout->addWidget(openDataButton);
    dataLayout->addWidget(dataActionGroup);
    dataLayout->addStretch();

    const auto emitFocus = [this]() {
        if (!updating_) emit focusSettingsChanged();
    };
    connect(plantCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [emitFocus](int) { emitFocus(); });
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, emitFocus](int) {
                updateMinutesEnabled();
                emitFocus();
            });
    connect(minutesSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [emitFocus](int) { emitFocus(); });
    connect(tagCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [emitFocus](int) { emitFocus(); });
    connect(oathInput_, &QLineEdit::textChanged, this, [emitFocus](const QString&) { emitFocus(); });
    connect(deepFocusToggle_, &QCheckBox::toggled, this, [emitFocus](bool) { emitFocus(); });
    connect(allowPauseToggle_, &QCheckBox::toggled, this, [emitFocus](bool) { emitFocus(); });
    connect(guardianGoalSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsPage::guardianGoalChanged);

    connect(fontScaleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { emitAccessibilityChange(); });
    connect(reducedMotionToggle_, &QCheckBox::toggled, this, [this](bool reduced) {
        setReducedMotion(reduced);
        emitAccessibilityChange();
    });
    connect(highContrastToggle_, &QCheckBox::toggled, this,
            [this](bool) { emitAccessibilityChange(); });

    connect(addButton, &QPushButton::clicked, this, [this]() {
        const QString processName = blacklistInput_->text().trimmed();
        if (processName.isEmpty()) return;
        emit blacklistAddRequested(processName);
        blacklistInput_->clear();
    });
    connect(blacklistInput_, &QLineEdit::returnPressed, addButton, &QPushButton::click);
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        if (auto* item = blacklistWidget_->currentItem())
            emit blacklistRemoveRequested(item->text());
    });
    connect(backupButton, &QPushButton::clicked, this, &SettingsPage::backupRequested);
    connect(restoreButton, &QPushButton::clicked, this, &SettingsPage::restoreRequested);
    connect(exportButton, &QPushButton::clicked, this, &SettingsPage::exportRequested);
    connect(openDataButton, &QPushButton::clicked, this, &SettingsPage::openDataDirectoryRequested);
}

SettingsPage::FocusSettings SettingsPage::focusSettings() const
{
    FocusSettings settings;
    settings.minutes = minutesSpin_->value();
    settings.plantType = plantCombo_->currentData().toUInt();
    settings.tagId = tagCombo_->currentData().toUInt();
    settings.stopwatch = modeCombo_->currentData().toInt() == 1;
    settings.deepFocus = deepFocusToggle_->isChecked();
    settings.allowPause = allowPauseToggle_->isChecked();
    settings.oath = oathInput_->text();
    return settings;
}

SettingsPage::AccessibilitySettings SettingsPage::accessibilitySettings() const
{
    AccessibilitySettings settings;
    settings.fontScalePercent = fontScaleCombo_->currentData().toInt();
    settings.reducedMotion = reducedMotionToggle_->isChecked();
    settings.highContrast = highContrastToggle_->isChecked();
    return settings;
}

void SettingsPage::setFocusSettings(const FocusSettings& settings)
{
    updating_ = true;
    const int plantIndex = plantCombo_->findData(settings.plantType);
    if (plantIndex >= 0) plantCombo_->setCurrentIndex(plantIndex);
    const int tagIndex = tagCombo_->findData(settings.tagId);
    if (tagIndex >= 0) tagCombo_->setCurrentIndex(tagIndex);
    modeCombo_->setCurrentIndex(settings.stopwatch ? 1 : 0);
    minutesSpin_->setValue(settings.minutes);
    deepFocusToggle_->setChecked(settings.deepFocus);
    allowPauseToggle_->setChecked(settings.allowPause);
    oathInput_->setText(settings.oath);
    updating_ = false;
    updateMinutesEnabled();
}

void SettingsPage::setAccessibilitySettings(const AccessibilitySettings& settings)
{
    updating_ = true;
    const int scaleIndex = fontScaleCombo_->findData(settings.fontScalePercent);
    fontScaleCombo_->setCurrentIndex(scaleIndex >= 0 ? scaleIndex : 1);
    reducedMotionToggle_->setChecked(settings.reducedMotion);
    highContrastToggle_->setChecked(settings.highContrast);
    updating_ = false;
    setReducedMotion(settings.reducedMotion);
    setProperty("highContrast", settings.highContrast);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void SettingsPage::setPlantOptions(const QVector<Option>& options, uint32_t selectedValue)
{
    updating_ = true;
    plantCombo_->clear();
    int selectedIndex = 0;
    for (const Option& option : options) {
        plantCombo_->addItem(option.label, option.value);
        if (option.value == selectedValue) selectedIndex = plantCombo_->count() - 1;
    }
    plantCombo_->setCurrentIndex(selectedIndex);
    updating_ = false;
}

void SettingsPage::setTagOptions(const QVector<Option>& options, uint32_t selectedValue)
{
    updating_ = true;
    tagCombo_->clear();
    int selectedIndex = 0;
    for (const Option& option : options) {
        tagCombo_->addItem(option.label, option.value);
        if (option.value == selectedValue) selectedIndex = tagCombo_->count() - 1;
    }
    tagCombo_->setCurrentIndex(selectedIndex);
    updating_ = false;
}

void SettingsPage::setBlacklist(const QStringList& entries)
{
    blacklistWidget_->clear();
    blacklistWidget_->addItems(entries);
}

void SettingsPage::setDataSummary(const QString& mode, int schemaVersion)
{
    dataSummaryLabel_->setText(
        QStringLiteral("数据模式：%1\n专注数据：SQLite 架构 v%2").arg(mode).arg(schemaVersion));
}

void SettingsPage::setGuardianGoalMinutes(int minutes)
{
    const QSignalBlocker blocker(guardianGoalSpin_);
    guardianGoalSpin_->setValue(minutes);
}

void SettingsPage::setAllowPauseControlEnabled(bool enabled)
{
    allowPauseToggle_->setEnabled(enabled);
}

void SettingsPage::setReducedMotion(bool reduced)
{
    for (SproutToggle* toggle : findChildren<SproutToggle*>())
        toggle->setReducedMotion(reduced);
}

void SettingsPage::updateMinutesEnabled()
{
    minutesSpin_->setEnabled(modeCombo_->currentData().toInt() == 0);
}

void SettingsPage::emitAccessibilityChange()
{
    if (!updating_) emit accessibilitySettingsChanged();
}
