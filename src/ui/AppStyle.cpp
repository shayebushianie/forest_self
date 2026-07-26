#include "ui/AppStyle.h"

#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QStyle>
#include <QWidget>

namespace {

constexpr auto kKeyboardFocusProperty = "keyboardFocusVisible";
constexpr auto kFocusVisibilityInstalledProperty = "_forestFocusVisibilityInstalled";

bool isKeyboardNavigationKey(int key)
{
    switch (key) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        return true;
    default:
        return false;
    }
}

void setKeyboardFocusProperty(QWidget* widget, bool visible)
{
    if (!widget || widget->property(kKeyboardFocusProperty).toBool() == visible) return;
    widget->setProperty(kKeyboardFocusProperty, visible);
    if (widget->style()) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
    widget->update();
    if (widget->parentWidget())
        widget->parentWidget()->update(widget->geometry().adjusted(-6, -6, 6, 6));
}

class FocusVisibilityFilter final : public QObject {
public:
    explicit FocusVisibilityFilter(QApplication& application)
        : QObject(&application),
          application_(application)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
        case QEvent::TabletPress:
            keyboardMode_ = false;
            setKeyboardFocusProperty(application_.focusWidget(), false);
            break;
        case QEvent::KeyPress:
            if (isKeyboardNavigationKey(static_cast<QKeyEvent*>(event)->key())) {
                keyboardMode_ = true;
                QWidget* target = application_.focusWidget();
                if (!target) target = qobject_cast<QWidget*>(watched);
                setKeyboardFocusProperty(target, true);
            }
            break;
        case QEvent::FocusIn:
            if (auto* widget = qobject_cast<QWidget*>(watched)) {
                const auto reason = static_cast<QFocusEvent*>(event)->reason();
                const bool visible = reason == Qt::TabFocusReason
                    || reason == Qt::BacktabFocusReason
                    || reason == Qt::ShortcutFocusReason
                    || (reason != Qt::MouseFocusReason && keyboardMode_);
                if (reason == Qt::MouseFocusReason) keyboardMode_ = false;
                if (visible) keyboardMode_ = true;
                setKeyboardFocusProperty(widget, visible);
            }
            break;
        case QEvent::FocusOut:
            setKeyboardFocusProperty(qobject_cast<QWidget*>(watched), false);
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QApplication& application_;
    bool keyboardMode_ = false;
};

} // namespace

void AppStyle::installFocusVisibility(QApplication& application)
{
    if (application.property(kFocusVisibilityInstalledProperty).toBool()) return;
    application.setProperty(kFocusVisibilityInstalledProperty, true);
    application.installEventFilter(new FocusVisibilityFilter(application));
}

bool AppStyle::keyboardFocusVisible(const QWidget* widget)
{
    return widget && widget->hasFocus() && widget->property(kKeyboardFocusProperty).toBool();
}

QString AppStyle::styleSheet(int fontScalePercent, bool highContrast)
{
    const int baseFontPx = qMax(11, qRound(14.0 * fontScalePercent / 100.0));
    const int settingDescriptionPx = qMax(10, qRound(12.0 * fontScalePercent / 100.0));
    const int settingsTitlePx = qMax(24, qRound(30.0 * fontScalePercent / 100.0));
    QString rules = QStringLiteral(R"(
        QWidget {
            background-color: #58AD8F;
            color: #F7FFF7;
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            font-size: __BASE_FONT_PX__px;
            selection-background-color: #D9E46B;
            selection-color: #244538;
        }
        QAbstractItemView, QRadioButton, QCheckBox {
            outline: none;
        }
        QDialog, QMessageBox {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
                        stop:0 #EAF7CB, stop:0.56 #D6F0BF, stop:1 #BCE7C7);
            color: #315747;
            border: 2px solid #3F9670;
            border-radius: 22px;
        }
        QDialog QLabel {
            color: #315747;
            background: transparent;
            border: none;
        }
        QMessageBox QLabel { color: #315747; background: transparent; border: none; }
        QGroupBox {
            background-color: rgba(255, 255, 255, 42);
            border: 1px solid rgba(255, 255, 255, 95);
            border-radius: 8px;
            margin-top: 14px;
            padding: 18px 12px 12px 12px;
            font-weight: 700;
            color: #F7FFF7;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 12px;
            padding: 0 6px;
            background-color: #58AD8F;
            color: #F2F4C6;
        }
        QWidget[uiCard="true"] {
            background-color: rgba(255, 255, 255, 42);
            border: 1px solid rgba(255, 255, 255, 92);
            border-radius: 10px;
        }
        QWidget[settingRow="true"] {
            background-color: rgba(21, 86, 66, 34);
            border: 1px solid rgba(255, 255, 255, 52);
            border-radius: 8px;
        }
        QLabel[settingTitle="true"] { font-size: __BASE_FONT_PX__px; font-weight: 800; color: #F7FFF7; }
        QLabel[settingDescription="true"] { font-size: __SETTING_DESC_PX__px; color: rgba(247,255,247,180); }
        QLabel {
            background: transparent;
            border: none;
        }
        QPushButton {
            background-color: rgba(255, 255, 255, 42);
            border: 1px solid rgba(255, 255, 255, 90);
            border-radius: 7px;
            padding: 8px 16px;
            color: #F7FFF7;
            font-weight: 700;
            min-height: 18px;
        }
        QPushButton:hover {
            background-color: rgba(255, 255, 255, 70);
            border-color: #EAF28A;
            color: #FFFFFF;
        }
        QPushButton:pressed {
            background-color: rgba(42, 107, 84, 145);
            border-color: #D5C743;
            padding-top: 9px;
            padding-bottom: 7px;
        }
        QPushButton[keyboardFocusVisible="true"] {
            background-color: rgba(255, 255, 255, 78);
            color: #FFFFFF;
        }
        QPushButton:disabled {
            background-color: rgba(51, 103, 86, 80);
            border-color: rgba(255, 255, 255, 35);
            color: rgba(247, 255, 247, 115);
        }
        QPushButton#btnPrimary {
            background-color: #6ED4B5;
            border-color: #8AE4CC;
            color: #F7FFF7;
        }
        QPushButton#btnPrimary:hover {
            background-color: #7CE1C1;
            border-color: #F2F4C6;
        }
        QPushButton#btnPrimary:pressed {
            background-color: #4E9B82;
        }
        QPushButton#btnSecondary {
            background-color: #3E8D75;
            border-color: #2F735E;
            color: #F7FFF7;
        }
        QPushButton#btnSecondary:hover {
            background-color: #4FA286;
            border-color: #244538;
            color: #FFFFFF;
        }
        QPushButton#btnSecondary:pressed {
            background-color: #315747;
        }
        QPushButton#btnDanger {
            background-color: #C95852;
            border-color: #E9938E;
            color: #FFF9F8;
        }
        QPushButton#btnDanger:hover { background-color: #DD6B64; border-color: #FFD0CB; }
        QPushButton#btnIcon {
            min-width: 30px; min-height: 30px; padding: 4px;
            border-radius: 16px;
        }
        QComboBox, QSpinBox, QLineEdit, QListWidget {
            background-color: rgba(255, 255, 255, 46);
            border: 1px solid rgba(255, 255, 255, 90);
            border-radius: 7px;
            padding: 6px 9px;
            color: #F7FFF7;
        }
        QComboBox:hover, QSpinBox:hover, QLineEdit:hover, QListWidget:hover {
            border-color: #F2F4C6;
            background-color: rgba(255, 255, 255, 60);
        }
        QComboBox:focus, QSpinBox:focus, QLineEdit:focus {
            background-color: rgba(255, 255, 255, 78);
        }
        QComboBox[keyboardFocusVisible="true"],
        QSpinBox[keyboardFocusVisible="true"],
        QLineEdit[keyboardFocusVisible="true"] {
            background-color: rgba(255, 255, 255, 92);
            color: #FFFFFF;
        }
        QDialog QComboBox, QDialog QSpinBox, QDialog QLineEdit, QDialog QListWidget {
            background-color: #FAFBE7;
            border: 1px solid #9CAF88;
            border-radius: 7px;
            color: #244538;
            min-height: 24px;
            padding: 4px 10px;
            placeholder-text-color: #6E7F6B;
            selection-background-color: #6CBF8F;
            selection-color: #FFFFFF;
        }
        QDialog QComboBox:hover, QDialog QSpinBox:hover, QDialog QLineEdit:hover,
        QDialog QListWidget:hover {
            background-color: #FFFFFF;
            border-color: #5E9B72;
        }
        QDialog QComboBox:focus, QDialog QSpinBox:focus, QDialog QLineEdit:focus {
            background-color: #FFFFFF;
        }
        QDialog QComboBox[keyboardFocusVisible="true"],
        QDialog QSpinBox[keyboardFocusVisible="true"],
        QDialog QLineEdit[keyboardFocusVisible="true"] {
            background-color: #F3F8D8;
            color: #173F33;
        }
        QDialog QPushButton {
            background-color: #E7EDB8;
            border: 1px solid #8FAB73;
            color: #244538;
        }
        QDialog QPushButton:hover {
            background-color: #F8F9D7;
            border-color: #4E9B82;
            color: #163729;
        }
        QDialog QPushButton:pressed {
            background-color: #C9D68A;
            border-color: #357A5B;
        }
        QDialog QPushButton#btnPrimary {
            background-color: #2F8F4E;
            border-color: #267540;
            color: #FFFFFF;
        }
        QDialog QPushButton#btnPrimary:hover {
            background-color: #36A85B;
            border-color: #1F6336;
            color: #FFFFFF;
        }
        QDialog QPushButton#btnPrimary:pressed {
            background-color: #267540;
        }
        QDialog QPushButton#btnSecondary {
            background-color: #315747;
            border-color: #244538;
            color: #FFFFFF;
        }
        QDialog QPushButton#btnSecondary:hover {
            background-color: #3E725D;
            border-color: #1A3328;
            color: #FFFFFF;
        }
        QDialog QPushButton#btnSecondary:pressed {
            background-color: #244538;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
            background: transparent;
        }
        QListWidget::item {
            padding: 7px;
            border-radius: 5px;
        }
        QListWidget::item:hover {
            background-color: rgba(255, 255, 255, 36);
        }
        QListWidget::item:selected {
            background-color: #D9E46B;
            color: #244538;
        }
        QWidget#settingsPage {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
                        stop:0 #6BB197, stop:0.62 #4F9B80, stop:1 #3F856E);
        }
        QWidget[settingsTransparent="true"] {
            background: transparent;
        }
        QFrame#settingsGlassShell,
        QWidget#settingsRail,
        QFrame#settingsContentPanel,
        QStackedWidget#settingsCategoryStack,
        QWidget#settingsPage QScrollArea,
        QWidget#settingsPage QScrollArea QWidget#qt_scrollarea_viewport,
        QWidget#focusSettingsContent,
        QWidget#rulesSettingsContent,
        QWidget#appearanceSettingsContent,
        QWidget#dataSettingsContent {
            background-color: #D5EBDD;
        }
        QFrame#settingsGlassShell {
            border: 1px solid rgba(236,250,240,185);
            border-radius: 22px;
        }
        QWidget#settingsRail {
            background: transparent;
            border: none;
        }
        QLabel[settingsRailTitle="true"] {
            color: #204F3E;
            font-size: __SETTINGS_RAIL_TITLE_PX__px;
            font-weight: 800;
            padding: 0 8px 10px 8px;
        }
        QFrame#settingsContentPanel {
            background: transparent;
            border: none;
        }
        QStackedWidget#settingsCategoryStack {
            background: transparent;
            border: none;
        }
        QListWidget#settingsCategoryNav {
            background: transparent;
            border: none;
            outline: none;
            padding: 0;
        }
        QListWidget#settingsCategoryNav::item {
            min-height: 38px;
            padding: 8px 11px;
            margin: 3px 0;
            border: 1px solid transparent;
            border-radius: 12px;
            color: #315E4D;
            font-weight: 700;
        }
        QListWidget#settingsCategoryNav::item:hover {
            background-color: rgba(74,132,103,22);
        }
        QListWidget#settingsCategoryNav::item:selected {
            background-color: rgba(70,137,103,36);
            border-left: 3px solid #4A8768;
            color: #173F33;
        }
        QListWidget#settingsCategoryNav[keyboardFocusVisible="true"]::item:selected {
            border-left: 3px solid #D4B844;
        }
        QWidget#settingsPage QScrollArea,
        QWidget#settingsPage QScrollArea > QWidget > QWidget {
            border: none;
        }
        QLabel[settingsPageTitle="true"] {
            color: #173F33;
            font-size: __SETTINGS_TITLE_PX__px;
            font-weight: 800;
        }
        QLabel[settingsPageDescription="true"] {
            color: rgba(30,73,58,170);
            font-size: __BASE_FONT_PX__px;
        }
        QLabel[settingsSectionLabel="true"] {
            color: #2C6D54;
            font-size: __SETTING_DESC_PX__px;
            font-weight: 800;
            padding: 5px 4px 2px 4px;
        }
        QFrame[settingsListGroup="true"] {
            background: transparent;
            border: none;
        }
        QWidget[settingsRow="true"] {
            background: transparent;
            border: none;
        }
        QFrame[settingsSeparator="true"] {
            background-color: rgba(42,94,74,42);
            border: none;
            margin-left: 4px;
            margin-right: 4px;
        }
        QLabel[settingsRowTitle="true"] {
            color: #173F33;
            font-size: __BASE_FONT_PX__px;
            font-weight: 750;
        }
        QLabel[settingsRowDescription="true"] {
            color: rgba(35,76,62,155);
            font-size: __SETTING_DESC_PX__px;
        }
        QLabel#settingsDataSummary {
            color: #315C49;
            font-size: __BASE_FONT_PX__px;
        }
        QWidget#settingsPage QComboBox,
        QWidget#settingsPage QSpinBox,
        QWidget#settingsPage QLineEdit {
            background-color: rgba(255,255,255,105);
            color: #214E3E;
            border: 1px solid transparent;
            border-radius: 10px;
            padding: 7px 10px;
            min-height: 22px;
        }
        QWidget#settingsPage QComboBox:hover,
        QWidget#settingsPage QSpinBox:hover,
        QWidget#settingsPage QLineEdit:hover {
            border-color: rgba(74,135,103,90);
            background-color: rgba(255,255,255,155);
        }
        QWidget#settingsPage QComboBox:focus,
        QWidget#settingsPage QSpinBox:focus,
        QWidget#settingsPage QLineEdit:focus {
            background-color: rgba(255,255,255,155);
        }
        QWidget#settingsPage QComboBox[keyboardFocusVisible="true"],
        QWidget#settingsPage QSpinBox[keyboardFocusVisible="true"],
        QWidget#settingsPage QLineEdit[keyboardFocusVisible="true"] {
            background-color: rgba(242,247,211,210);
            color: #173F33;
        }
        QWidget#settingsPage QListWidget#settingsBlacklistList {
            background-color: rgba(255,255,255,82);
            color: #244F40;
            border: none;
            border-radius: 14px;
            padding: 8px;
        }
        QWidget#settingsPage QPushButton[settingsActionRow="true"] {
            background: transparent;
            color: #214E3E;
            border: none;
            border-radius: 11px;
            padding: 13px 16px;
            min-height: 42px;
            text-align: left;
            font-weight: 700;
        }
        QWidget#settingsPage QPushButton[settingsActionRow="true"]:hover {
            background-color: rgba(80,145,111,25);
            border: none;
        }
        QWidget#settingsPage QPushButton[settingsActionRow="true"]:pressed {
            background-color: rgba(45,105,80,38);
            padding-top: 14px;
            padding-bottom: 12px;
        }
        QWidget#settingsPage QPushButton[settingsActionRow="true"][keyboardFocusVisible="true"] {
            background-color: rgba(80,145,111,30);
            border-left: 3px solid #D4B844;
        }
        QWidget#settingsPage QPushButton[dangerousAction="true"] {
            color: #87483F;
        }
        QScrollArea {
            border: none;
            background: transparent;
        }
        QScrollBar:vertical {
            background: rgba(43, 108, 84, 80);
            width: 10px;
            margin: 4px 2px 4px 2px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background: rgba(242, 244, 198, 165);
            min-height: 28px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background: #F2F4C6;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
            border: none;
        }
        QMenu {
            background-color: #4E9B82;
            border: 1px solid #72C7AA;
            padding: 5px;
            border-radius: 7px;
        }
        QMenu::item {
            padding: 7px 24px;
            border-radius: 5px;
        }
        QMenu::item:selected {
            background-color: #F2F4C6;
            color: #244538;
        }
    )");
    rules.replace(QStringLiteral("__BASE_FONT_PX__"), QString::number(baseFontPx));
    rules.replace(QStringLiteral("__SETTING_DESC_PX__"), QString::number(settingDescriptionPx));
    rules.replace(QStringLiteral("__SETTINGS_TITLE_PX__"), QString::number(settingsTitlePx));
    rules.replace(QStringLiteral("__SETTINGS_RAIL_TITLE_PX__"),
                  QString::number(qMax(17, qRound(20.0 * fontScalePercent / 100.0))));
    if (highContrast) {
        const int focusWidth = fontScalePercent >= 110 ? 3 : 2;
        rules += QStringLiteral(
            "QPushButton, QComboBox, QSpinBox, QLineEdit, QCheckBox::indicator {"
            " border:%1px solid #173F33; }"
            "QPushButton[keyboardFocusVisible=\"true\"],"
            " QComboBox[keyboardFocusVisible=\"true\"],"
            " QSpinBox[keyboardFocusVisible=\"true\"],"
            " QLineEdit[keyboardFocusVisible=\"true\"] {"
            " background-color:#F2F4C6; color:#173F33; }"
            "QCheckBox[keyboardFocusVisible=\"true\"],"
            " QRadioButton[keyboardFocusVisible=\"true\"] { color:#8B6D08; }"
            "QFrame[settingsListGroup=\"true\"] { background:transparent; border:none; }"
            "QWidget#settingsRail, QFrame#settingsContentPanel,"
            " QStackedWidget#settingsCategoryStack,"
            " QWidget#settingsPage QScrollArea,"
            " QWidget#settingsPage QScrollArea QWidget#qt_scrollarea_viewport,"
            " QWidget#focusSettingsContent, QWidget#rulesSettingsContent,"
            " QWidget#appearanceSettingsContent, QWidget#dataSettingsContent {"
            " background-color:#DDEFE3; border:none; }"
            "QFrame[settingsSeparator=\"true\"] { background-color:#315E4D; }"
            "QFrame#settingsGlassShell { background-color:#DDEFE3; border:%1px solid #173F33; }")
                     .arg(focusWidth);
    }
    return rules;
}
