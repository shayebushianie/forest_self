#ifndef DIALOG_PRESENTER_H
#define DIALOG_PRESENTER_H

#include <QMessageBox>
#include <QAbstractButton>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QFileInfo>

#include "ui/AppStyle.h"

class DialogPresenter final {
public:
    static void information(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Information, title, message, QMessageBox::Ok);
    }

    static void warning(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Warning, title, message, QMessageBox::Ok);
    }

    static void error(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Critical, title, message, QMessageBox::Ok);
    }

    static void detail(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Information, title, message, QMessageBox::Ok);
    }

    static void empty(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Information, title, message, QMessageBox::Ok);
    }

    static bool confirm(QWidget* parent, const QString& title, const QString& message)
    {
        return show(parent, QMessageBox::Question, title, message,
                    QMessageBox::No | QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes;
    }

    static bool confirmDanger(QWidget* parent, const QString& title, const QString& message)
    {
        return show(parent, QMessageBox::Warning, title, message,
                    QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel, true) == QMessageBox::Yes;
    }

    static bool detailWithAction(QWidget* parent, const QString& title, const QString& message,
                                 const QString& actionText)
    {
        QMessageBox dialog(parent);
        dialog.setIcon(QMessageBox::Information);
        dialog.setWindowTitle(title);
        dialog.setText(title);
        dialog.setInformativeText(message);
        prepare(dialog);
        QPushButton* action = dialog.addButton(actionText, QMessageBox::AcceptRole);
        QPushButton* close = dialog.addButton(QStringLiteral("关闭"), QMessageBox::RejectRole);
        applyButtonRoles(dialog, action);
        dialog.setDefaultButton(close);
        dialog.setEscapeButton(close);
        close->setFocus(Qt::OtherFocusReason);
        dialog.exec();
        return dialog.clickedButton() == action;
    }

    static void prepare(QMessageBox& dialog)
    {
        dialog.setWindowFlags(dialog.windowFlags() | Qt::FramelessWindowHint);
        dialog.setAttribute(Qt::WA_StyledBackground, true);
        dialog.setObjectName("forestDialog");
        dialog.setProperty("forest.roundedDialog", true);
        dialog.setModal(true);
        dialog.setTextFormat(Qt::PlainText);
        dialog.setMinimumWidth(360);
        dialog.setMaximumWidth(640);
        for (QLabel* label : dialog.findChildren<QLabel*>()) {
            label->setWordWrap(true);
            label->setMaximumWidth(560);
        }
        dialog.setStyleSheet(QStringLiteral(
            "QMessageBox#forestDialog {"
            " background-color:#F8FCF9; color:#263D32; font-size:14px;"
            " border:1px solid #A9C8B6; border-radius:16px;"
            "}"
            "QMessageBox#forestDialog QWidget {"
            " background-color:transparent; color:#263D32; font-size:14px;"
            "}"
            "QMessageBox#forestDialog QLabel {"
            " background:transparent; border:none; color:#263D32;"
            "}"
            "QMessageBox#forestDialog QLabel#qt_msgbox_label { font-size:18px; font-weight:800; }"
            "QMessageBox#forestDialog QDialogButtonBox {"
            " background:transparent; border:none;"
            "}"
            "QMessageBox#forestDialog QPushButton {"
            " min-width:84px; min-height:36px; padding:6px 14px;"
            " border:1px solid #A8C4B2; border-radius:8px;"
            " background-color:#EDF4EF; color:#294B3B;"
            "}"
            "QMessageBox#forestDialog QPushButton:hover {"
            " background-color:#E1EEE5; border-color:#6E9B7F;"
            "}"
            "QMessageBox#forestDialog QPushButton:focus {"
            " border:2px solid #315C43;"
            "}"
            "QMessageBox#forestDialog QPushButton#btnPrimary {"
            " background-color:#2F805B; border:1px solid #276D4D; color:#FFFFFF;"
            "}"
            "QMessageBox#forestDialog QPushButton#btnPrimary:hover {"
            " background-color:#388F68; border-color:#205C41;"
            "}"
            "QMessageBox#forestDialog QPushButton#btnSecondary {"
            " background-color:#EDF4EF; border:1px solid #A8C4B2; color:#294B3B;"
            "}"
            "QMessageBox#forestDialog QPushButton#btnSecondary:hover {"
            " background-color:#E1EEE5; border-color:#6E9B7F;"
            "}"
            "QMessageBox#forestDialog QPushButton#btnDanger {"
            " background-color:#B7433D; border:1px solid #9D3934; color:#FFFFFF;"
            "}"
            "QMessageBox#forestDialog QPushButton#btnDanger:hover {"
            " background-color:#CA514A; border-color:#8E302C;"
            "}"
            "QMessageBox#forestDialog QPushButton#btnPrimary:focus,"
            "QMessageBox#forestDialog QPushButton#btnSecondary:focus,"
            "QMessageBox#forestDialog QPushButton#btnDanger:focus {"
            " border:2px solid #244A37;"
            "}"
        ));
        AppStyle::applyAccessibility(dialog);
    }

    static void prepare(QDialog& dialog)
    {
        dialog.setWindowFlags(dialog.windowFlags() | Qt::FramelessWindowHint);
        dialog.setAttribute(Qt::WA_StyledBackground, true);
        dialog.setObjectName("forestDialog");
        dialog.setProperty("forest.roundedDialog", true);
        dialog.setModal(true);
        dialog.setStyleSheet(QStringLiteral(
            "QDialog#forestDialog { background:#F5FCF8; color:#245543; border:1px solid #93C8B4; border-radius:16px; }"
            "QDialog#forestDialog QWidget { background:transparent; }"
            "QDialog#forestDialog QLabel { color:#245543; background:transparent; border:none; }"
            "QDialog#forestDialog QGroupBox { color:#245543; background:#EDF6F0; border:1px solid #B5D4C2; }"
            "QDialog#forestDialog QGroupBox::title { color:#245543; background:#EDF6F0; }"
            "QDialog#forestDialog QPushButton { background:#EDF4EF; color:#294B3B;"
            " border:1px solid #A8C4B2; border-radius:8px; padding:8px 14px; min-height:24px; }"
            "QDialog#forestDialog QPushButton:hover { background:#E1EEE5; border-color:#6E9B7F; }"
            "QDialog#forestDialog QPushButton#btnPrimary { background:%1; color:#FFFFFF; border-color:%1; }"
            "QDialog#forestDialog QPushButton#btnPrimary:hover { background:%2; border-color:%2; }"
            "QDialog#forestDialog QPushButton#btnPrimary:pressed { background:%3; }"
            "QDialog#forestDialog QPushButton#btnSecondary { background:#EDF4EF; color:#294B3B; }"
            "QDialog#forestDialog QPushButton#btnSecondary:hover { background:#E1EEE5; }"
            "QDialog#forestDialog QPushButton#btnDanger { background:#B7433D; color:#FFFFFF; border-color:#9D3934; }"
            "QDialog#forestDialog QPushButton#btnDanger:hover { background:#CA514A; }"
            "QDialog#forestDialog QPushButton:focus { border:2px solid #244A37; }"
            "QDialog#forestDialog QPushButton:disabled { background:#DFE8E3; color:#516D61; border-color:#CCD8D1; }")
            .arg(AppStyle::color(AppStyle::Color::Primary).name(),
                 AppStyle::color(AppStyle::Color::PrimaryHover).name(),
                 AppStyle::color(AppStyle::Color::PrimaryPressed).name()));
    }

    static void setPrimary(QPushButton* button) { if (button) button->setObjectName("btnPrimary"); }
    static void setSecondary(QPushButton* button) { if (button) button->setObjectName("btnSecondary"); }
    static void setDanger(QPushButton* button) { if (button) button->setObjectName("btnDanger"); }

    static QString chooseDirectory(QWidget* parent, const QString& title)
    {
        QFileDialog dialog(parent, title);
        dialog.setOption(QFileDialog::DontUseNativeDialog);
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::ShowDirsOnly);
        prepareFileChooser(dialog, title, QStringLiteral("选择"));
        return dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()
            ? dialog.selectedFiles().first() : QString();
    }

    static QString chooseSaveFile(QWidget* parent, const QString& title, const QString& initial,
                                  const QString& filter)
    {
        QFileDialog dialog(parent, title, QFileInfo(initial).absolutePath(), filter);
        dialog.setOption(QFileDialog::DontUseNativeDialog);
        dialog.setOption(QFileDialog::DontConfirmOverwrite);
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.selectFile(QFileInfo(initial).fileName());
        prepareFileChooser(dialog, title, QStringLiteral("导出"));
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return {};
        const QString path = dialog.selectedFiles().first();
        if (QFileInfo::exists(path) && !confirmDanger(parent, QStringLiteral("替换已有文件"),
            QStringLiteral("该位置已有文件，是否替换？取消会保留原文件。"))) return {};
        return path;
    }

private:
    static void prepareFileChooser(QFileDialog& dialog, const QString& title, const QString& accept)
    {
        prepare(static_cast<QDialog&>(dialog));
        dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(
            "QDialog#forestDialog QAbstractItemView { color:#245543; background:#F8FCF9;"
            " selection-background-color:#CEE9DA; selection-color:#173F33; }"
            "QDialog#forestDialog QHeaderView::section { color:#245543; background:#E5F2EB;"
            " border:1px solid #B5D4C2; padding:4px; }"
            "QDialog#forestDialog QComboBox, QDialog#forestDialog QLineEdit { color:#245543; background:#F8FCF9; }"));
        dialog.setAccessibleName(title);
        dialog.setLabelText(QFileDialog::Accept, accept);
        dialog.setLabelText(QFileDialog::Reject, QStringLiteral("取消"));
        dialog.setLabelText(QFileDialog::LookIn, QStringLiteral("位置"));
        dialog.setLabelText(QFileDialog::FileName, QStringLiteral("名称"));
        dialog.setLabelText(QFileDialog::FileType, QStringLiteral("类型"));
        if (dialog.fileMode() == QFileDialog::Directory) dialog.setNameFilter(QStringLiteral("目录"));
        if (auto* grid = qobject_cast<QGridLayout*>(dialog.layout())) {
            for (int i = grid->count() - 1; i >= 0; --i) {
                int row, column, rows, columns;
                grid->getItemPosition(i, &row, &column, &rows, &columns);
                QLayoutItem* item = grid->takeAt(i);
                grid->addItem(item, row + 1, column, rows, columns);
            }
            auto* heading = new QLabel(title, &dialog);
            heading->setStyleSheet("font-size:18px; font-weight:800; color:#245543;");
            grid->addWidget(heading, 0, 0, 1, grid->columnCount());
        }
        if (auto* buttons = dialog.findChild<QDialogButtonBox*>()) {
            for (auto* button : buttons->findChildren<QPushButton*>()) {
                button->setAutoDefault(false);
                if (buttons->buttonRole(button) == QDialogButtonBox::RejectRole) {
                    setSecondary(button);
                    button->setDefault(true);
                } else setPrimary(button);
            }
        }
    }
    static QMessageBox::StandardButton show(QWidget* parent, QMessageBox::Icon icon,
                                            const QString& title, const QString& message,
                                            QMessageBox::StandardButtons buttons,
                                            QMessageBox::StandardButton defaultButton = QMessageBox::Ok,
                                            bool destructive = false)
    {
        QMessageBox dialog(icon, title, title, buttons, parent);
        dialog.setInformativeText(message);
        prepare(dialog);
        localizeStandardButtons(dialog);
        dialog.setDefaultButton(defaultButton);
        dialog.setEscapeButton(defaultButton);
        applyButtonRoles(dialog, nullptr, destructive);
        if (QAbstractButton* defaultAction = dialog.button(defaultButton))
            defaultAction->setFocus(Qt::OtherFocusReason);
        return static_cast<QMessageBox::StandardButton>(dialog.exec());
    }

    static void localizeStandardButtons(QMessageBox& dialog)
    {
        if (QAbstractButton* button = dialog.button(QMessageBox::Ok)) button->setText(QStringLiteral("确定"));
        if (QAbstractButton* button = dialog.button(QMessageBox::Cancel)) button->setText(QStringLiteral("取消"));
        if (QAbstractButton* button = dialog.button(QMessageBox::Yes)) button->setText(QStringLiteral("确定"));
        if (QAbstractButton* button = dialog.button(QMessageBox::No)) button->setText(QStringLiteral("取消"));
    }

    static void applyButtonRoles(QMessageBox& dialog, QAbstractButton* primaryAction = nullptr,
                                 bool destructive = false)
    {
        for (QAbstractButton* button : dialog.buttons()) {
            button->setFocusPolicy(Qt::StrongFocus);
            if (button == dialog.button(QMessageBox::Yes) && destructive) button->setObjectName("btnDanger");
            else if (button == dialog.button(QMessageBox::Yes) || button == dialog.button(QMessageBox::Ok) || button == primaryAction) button->setObjectName("btnPrimary");
            else button->setObjectName("btnSecondary");
        }
    }
};

#endif // DIALOG_PRESENTER_H
