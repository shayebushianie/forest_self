#include "ui/AppStyle.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLineEdit>
#include <QPropertyAnimation>
#include <QSpinBox>
#include <QWidget>

namespace {

class SmoothInteractionFilter : public QObject {
public:
    explicit SmoothInteractionFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget || !widget->isEnabled() || !isInteractive(widget)) {
            return QObject::eventFilter(watched, event);
        }

        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::FocusIn:
            animateOpacity(widget, 1.0, 120);
            break;
        case QEvent::MouseButtonPress:
            animateOpacity(widget, 0.86, 70);
            break;
        case QEvent::MouseButtonRelease:
            animateOpacity(widget, 1.0, 120);
            break;
        case QEvent::Leave:
        case QEvent::FocusOut:
            animateOpacity(widget, 0.94, 160);
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static bool isInteractive(QWidget* widget)
    {
        return qobject_cast<QAbstractButton*>(widget) ||
               qobject_cast<QComboBox*>(widget) ||
               qobject_cast<QSpinBox*>(widget) ||
               qobject_cast<QLineEdit*>(widget);
    }

    static void animateOpacity(QWidget* widget, qreal target, int duration)
    {
        auto* effect = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect());
        if (!effect) {
            if (widget->graphicsEffect()) return;
            effect = new QGraphicsOpacityEffect(widget);
            effect->setOpacity(0.94);
            widget->setGraphicsEffect(effect);
        }

        auto* previous = effect->findChild<QPropertyAnimation*>("smoothOpacityAnimation");
        if (previous) {
            previous->stop();
            previous->deleteLater();
        }

        auto* anim = new QPropertyAnimation(effect, "opacity", effect);
        anim->setObjectName("smoothOpacityAnimation");
        anim->setDuration(duration);
        anim->setStartValue(effect->opacity());
        anim->setEndValue(target);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
};

} // namespace

QString AppStyle::styleSheet()
{
    return QStringLiteral(R"(
        QWidget {
            background-color: #58AD8F;
            color: #F7FFF7;
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            font-size: 14px;
            selection-background-color: #D9E46B;
            selection-color: #244538;
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
        QLabel[settingTitle="true"] { font-size: 14px; font-weight: 800; color: #F7FFF7; }
        QLabel[settingDescription="true"] { font-size: 12px; color: rgba(247,255,247,180); }
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
        QPushButton:focus {
            border-color: #F2F4C6;
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
            border-color: #D9E46B;
            background-color: rgba(255, 255, 255, 78);
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
            border-color: #357A5B;
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
        QListWidget#settingsCategoryNav {
            background-color: rgba(23, 86, 67, 96);
            border: 1px solid rgba(255,255,255,62);
            border-radius: 10px;
            padding: 8px;
        }
        QListWidget#settingsCategoryNav::item {
            min-height: 32px;
            padding: 10px 12px;
            margin: 2px 0;
            color: rgba(247,255,247,210);
            font-weight: 700;
        }
        QListWidget#settingsCategoryNav::item:hover { background-color: rgba(255,255,255,34); }
        QListWidget#settingsCategoryNav::item:selected {
            background-color: #F2F4C6;
            color: #245543;
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
}

void AppStyle::installSmoothInteractions(QApplication& app)
{
    app.installEventFilter(new SmoothInteractionFilter(&app));
}
