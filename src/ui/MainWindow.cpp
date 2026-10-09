#include "ui/MainWindow.h"
#include "ui/CommercePages.h"
#include "ui/DashboardPageHost.h"
#include "ui/SocialPages.h"
#include "ui/PlantTimerWidget.h"
#include "ui/GardenCanvas.h"
#include "ui/ForestDashboardWidget.h"
#include "ui/ChallengeDashboardWidget.h"
#include "ui/GuardianDashboardWidget.h"
#include "ui/PlantImageUtils.h"
#include "ui/StoreDialog.h"
#include "ui/AchievementDialog.h"
#include "ui/MyForestDialog.h"
#include "core/FocusController.h"
#include "core/FocusSessionCoordinator.h"
#include "core/CoinManager.h"
#include "core/QuoteProvider.h"
#include "core/AchievementEngine.h"
#include "core/FocusResultService.h"
#include "core/FocusRecordSemantics.h"
#include "core/DashboardSnapshotService.h"
#include "config/PlantCatalog.h"
#include "core/GachaManager.h"
#include "core/ForestLayoutManager.h"
#include "core/FriendManager.h"
#include "core/ChallengeManager.h"
#include "core/TagManager.h"
#include "core/GuardianManager.h"
#include "core/UserFeatureServices.h"
#include "ui/FriendDialog.h"
#include "ui/ChallengeDialog.h"
#include "ui/GuardianDialog.h"
#include "ui/SidebarNavigation.h"
#include "ui/FocusPageState.h"
#include "ui/AppStyle.h"
#include "ui/DialogPresenter.h"
#include "system/RuleEngine.h"
#include "system/SystemMonitor.h"
#include "storage/DatabaseManager.h"
#include "common/PathConfig.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QCloseEvent>
#include <QMenu>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QMap>
#include <QStyle>
#include <QScrollArea>
#include <QRadioButton>
#include <QFontMetrics>
#include <QCheckBox>
#include <utility>
#include <QGridLayout>
#include <QIcon>
#include <QFrame>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QImage>
#include <QColor>
#include <QScreen>
#include <QEvent>
#include <QFormLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QProgressBar>
#include <QTabWidget>
#include <QDateTime>
#include <QDateEdit>
#include <QShortcut>
#include <QKeySequence>
#include <QAction>
#include <QTimer>
#include <functional>
#include <limits>
#include "config/UserPreferences.h"
#include "storage/BackupService.h"
#include "common/PathConfig.h"
#include "utils/Logger.h"

namespace {

class FocusStartButton final : public QPushButton {
public:
    explicit FocusStartButton(QWidget* parent = nullptr) : QPushButton(parent) {}

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QRectF bounds = rect().adjusted(1, 1, -1, -1);
        QColor textColor = AppStyle::color(AppStyle::Color::Surface);
        QColor borderColor = AppStyle::color(AppStyle::Color::Primary);
        QColor fill = borderColor;

        if (!isEnabled()) {
            fill = QColor("#DFE8E3");
            textColor = AppStyle::color(AppStyle::Color::Muted);
            borderColor = AppStyle::color(AppStyle::Color::Border);
        } else if (isDown()) {
            fill = AppStyle::color(AppStyle::Color::PrimaryPressed);
        } else if (underMouse()) {
            fill = AppStyle::color(AppStyle::Color::PrimaryHover);
        }

        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(bounds, 8, 8);

        if (hasFocus()) {
            painter.setPen(QPen(AppStyle::color(AppStyle::Color::Selection), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds.adjusted(4, 4, -4, -4), 5, 5);
        }

        painter.setFont(font());
        painter.setPen(textColor);
        painter.drawText(bounds, Qt::AlignCenter, text());
    }
};

} // namespace

MainWindow::MainWindow(FocusController& controller,
                       SystemMonitor& monitor,
                       RuleEngine& ruleEngine,
                       CoinManager& coinManager,
                       QuoteProvider& quotes,
                       DatabaseManager& db,
                       AchievementEngine& achievements,
                       FocusResultService& focusResults,
                       DashboardSnapshotService& dashboardSnapshots,
                       UserFeatureServices& featureServices,
                       QWidget* parent)
    : QMainWindow(parent),
      controller_(controller), monitor_(monitor),
      ruleEngine_(ruleEngine), coinManager_(coinManager),
      quotes_(quotes), db_(db), achievements_(achievements), focusResults_(focusResults),
      dashboardSnapshots_(dashboardSnapshots),
      featureServices_(featureServices),
      m_gacha(featureServices.gacha()),
      m_forestLayout(featureServices.forestLayout()),
      m_friendMgr(featureServices.friends()),
      m_challengeMgr(featureServices.challenges()),
      m_tagMgr(featureServices.tags()),
      m_guardian(featureServices.guardian())
{
    baseApplicationFont_ = QApplication::font();
    setWindowTitle(QStringLiteral("forest 专注森林"));
    setMinimumSize(880, 600);
    focusSession_ = std::make_unique<FocusSessionCoordinator>(controller_, focusResults_, this);
    shopPage_ = std::make_unique<ShopPageController>(coinManager_, focusResults_, this);
    achievementsPage_ = std::make_unique<AchievementsPageController>(achievements_, this);
    friendsPage_ = std::make_unique<FriendsPageController>(*m_friendMgr, this);
    QObject::connect(shopPage_.get(), &ShopPageController::plantCatalogChanged,
                     this, &MainWindow::refreshPlantCombo);
    QObject::connect(shopPage_.get(), &ShopPageController::achievementsChanged,
                     this, &MainWindow::refreshAchievementsPage);
    QObject::connect(friendsPage_.get(), &FriendsPageController::challengeRequested,
                     this, [this] { switchPage(7); });

    initNavigationLayout();

    timerRing_ = new PlantTimerWidget;
    timerRing_->setObjectName("focusTimer");
    QObject::connect(timerRing_, &PlantTimerWidget::sig_plantClicked,
                     this, &MainWindow::showHomePlantSelector);
    gardenCanvas_ = new GardenCanvas;
    forestDashboard_ = new ForestDashboardWidget;
    QObject::connect(timerRing_, &PlantTimerWidget::sig_tagClicked,
                     forestDashboard_, &ForestDashboardWidget::overviewRequested);
    QObject::connect(forestDashboard_, &ForestDashboardWidget::settingsRequested,
                     this, [this]() { switchPage(3); });
    QObject::connect(forestDashboard_, &ForestDashboardWidget::focusRecordRequested,
                     this, [this](uint32_t recordId) {
        const auto record = db_.readById(recordId);
        if (!record) {
            DialogPresenter::error(this, QStringLiteral("专注记录读取失败"), db_.lastError().isEmpty()
                ? QStringLiteral("这条记录暂不可用，请重新进入森林后重试。") : db_.lastError());
            return;
        }
        const FocusRecord& focus = record.value();
        const QString plantName = PlantCatalog::isKnown(focus.plantType)
            ? PlantCatalog::byType(focus.plantType).displayName : QStringLiteral("未知植物");
        const int tagIndex = settingsTagCombo_ ? settingsTagCombo_->findData(focus.tagId) : -1;
        const QString tagName = tagIndex >= 0 ? settingsTagCombo_->itemText(tagIndex)
                                               : QStringLiteral("无标签");
        const QString state = FocusRecordSemantics::statusText(focus);
        const QString started = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(focus.startTimestamp), Qt::LocalTime).toString(QStringLiteral("yyyy年M月d日 HH:mm"));
        QDialog detail(this);
        detail.setWindowTitle(QStringLiteral("专注记录"));
        DialogPresenter::prepare(detail);
        detail.setObjectName("forestRecordDetailDialog");
        detail.setStyleSheet(detail.styleSheet().replace(QStringLiteral("#forestDialog"),
            QStringLiteral("#forestRecordDetailDialog")) + QStringLiteral(
            "QDialog#forestRecordDetailDialog { background:#FFFFFF; border:1px solid #D5DFDA; border-radius:20px; }"));
        detail.setFixedWidth(520);
        auto* layout = new QVBoxLayout(&detail);
        layout->setContentsMargins(28, 24, 28, 22);
        layout->setSpacing(14);
        auto* title = new QLabel(QStringLiteral("专注记录"), &detail);
        title->setStyleSheet("font-size:20px; font-weight:800; color:#245543;");
        auto* details = new QHBoxLayout;
        details->setSpacing(18);
        auto* plant = new QLabel(&detail);
        plant->setObjectName("forestRecordDetailPlant");
        plant->setFixedSize(84, 88);
        plant->setAlignment(Qt::AlignCenter);
        plant->setAccessibleName(plantName);
        if (PlantCatalog::isKnown(focus.plantType)) {
            QPixmap plantIcon = PlantImageUtils::loadPlantIcon(
                PlantCatalog::byType(focus.plantType).iconPath);
            if (focus.status != FocusRecordStatus::Success && !plantIcon.isNull()) {
                QImage image = plantIcon.toImage().convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < image.height(); ++y) {
                    auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
                    for (int x = 0; x < image.width(); ++x) {
                        const int gray = qGray(pixels[x]);
                        pixels[x] = qRgba(gray, gray, gray, qAlpha(pixels[x]));
                    }
                }
                plantIcon = QPixmap::fromImage(image);
            }
            plant->setPixmap(plantIcon.scaled(plant->size(), Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
        } else {
            plant->hide();
        }
        auto* content = new QLabel(QStringLiteral("%1\n%2\n有效时长：%3\n项目：%4\n植物：%5\n状态：%6\n奖励：🪙 %7")
            .arg(started)
            .arg(focus.focusMode == PersistedFocusMode::Deep ? QStringLiteral("深度专注") : QStringLiteral("专注"))
            .arg(QStringLiteral("%1 分 %2 秒").arg(focus.actualSeconds / 60).arg(focus.actualSeconds % 60))
            .arg(tagName).arg(plantName).arg(state).arg(focus.coinsEarned), &detail);
        content->setObjectName("forestRecordDetailContent");
        content->setWordWrap(true);
        content->setTextFormat(Qt::PlainText);
        content->setStyleSheet("font-size:14px; color:#48675B;");
        auto* close = new QPushButton(QStringLiteral("关闭"), &detail);
        DialogPresenter::setSecondary(close);
        close->setDefault(true);
        layout->addWidget(title);
        details->addWidget(plant, 0, Qt::AlignTop);
        details->addWidget(content, 1);
        layout->addLayout(details);
        layout->addWidget(close, 0, Qt::AlignRight);
        QObject::connect(close, &QPushButton::clicked, &detail, &QDialog::accept);
        detail.adjustSize();
        QPainterPath detailShape;
        detailShape.addRoundedRect(QRectF(detail.rect()), 20, 20);
        detail.setMask(QRegion(detailShape.toFillPolygon().toPolygon()));
        detail.exec();
    });
    QObject::connect(forestDashboard_, &ForestDashboardWidget::filtersRequested,
                     this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("筛选森林记录"));
        DialogPresenter::prepare(dialog);
        dialog.setObjectName("forestRecordFilterDialog");
        dialog.setFixedWidth(520);
        dialog.setStyleSheet(dialog.styleSheet().replace(QStringLiteral("#forestDialog"),
            QStringLiteral("#forestRecordFilterDialog")) + QStringLiteral(
            "QDialog#forestRecordFilterDialog { background:#FFFFFF; border:1px solid #D5DFDA; border-radius:20px; }"
            "QCheckBox { color:#48675B; background:transparent; border:none; }"
            "QDateEdit, QComboBox { color:#203C35; background:#FFFFFF; border:1px solid #D5DFDA; border-radius:6px; min-height:30px; padding:0 8px; }"
            "QDateEdit:disabled { color:#71857A; background:#EAF0EA; }"
            "QDateEdit:focus, QComboBox:focus { border:2px solid #246B57; }"));
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(28, 24, 28, 22);
        layout->setSpacing(18);
        auto* title = new QLabel(QStringLiteral("筛选森林记录"), &dialog);
        title->setObjectName("forestRecordFilterTitle");
        title->setWordWrap(true);
        title->setStyleSheet("font-size:20px; font-weight:800; color:#245543;");
        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(14);
        const auto current = forestDashboard_->recordFilter();

        auto* dateEnabled = new QCheckBox(QStringLiteral("指定日期"), &dialog);
        dateEnabled->setObjectName("forestRecordFilterDateEnabled");
        auto* dateEdit = new QDateEdit(current.date.isValid() ? current.date : QDate::currentDate(), &dialog);
        dateEdit->setObjectName("forestRecordFilterDate");
        dateEdit->setCalendarPopup(true);
        dateEnabled->setChecked(current.date.isValid());
        dateEdit->setEnabled(dateEnabled->isChecked());
        QObject::connect(dateEnabled, &QCheckBox::toggled, dateEdit, &QWidget::setEnabled);

        auto* tagFilter = new QComboBox(&dialog);
        tagFilter->addItem(QStringLiteral("全部项目"), QVariant::fromValue<qulonglong>(std::numeric_limits<uint32_t>::max()));
        if (settingsTagCombo_) {
            for (int index = 0; index < settingsTagCombo_->count(); ++index) {
                tagFilter->addItem(settingsTagCombo_->itemText(index), settingsTagCombo_->itemData(index));
            }
        }
        const int tagIndex = current.tagId == std::numeric_limits<uint32_t>::max()
            ? 0 : tagFilter->findData(current.tagId);
        tagFilter->setCurrentIndex(tagIndex >= 0 ? tagIndex : 0);

        auto* plantFilter = new QComboBox(&dialog);
        plantFilter->setObjectName("forestRecordFilterPlant");
        plantFilter->addItem(QStringLiteral("全部植物"), QVariant::fromValue<qulonglong>(std::numeric_limits<uint32_t>::max()));
        for (const PlantDefinition& plant : PlantCatalog::all()) {
            plantFilter->addItem(plant.displayName, plant.type);
        }
        const int plantIndex = current.plantType == std::numeric_limits<uint32_t>::max()
            ? 0 : plantFilter->findData(current.plantType);
        plantFilter->setCurrentIndex(plantIndex >= 0 ? plantIndex : 0);

        auto* statusFilter = new QComboBox(&dialog);
        statusFilter->setObjectName("forestRecordFilterStatus");
        statusFilter->addItem(QStringLiteral("全部状态"), -1);
        statusFilter->addItem(QStringLiteral("成功完成"), 0);
        statusFilter->addItem(QStringLiteral("放弃"), 1);
        statusFilter->setToolTip(QStringLiteral("旧记录未保存中断原因时保留为放弃，不猜测其原因；新中断恢复记录单独分类。"));
        statusFilter->addItem(QStringLiteral("规则失败"), 2);
        statusFilter->addItem(QStringLiteral("中断恢复"), 3);
        const int statusIndex = statusFilter->findData(current.terminalState);
        statusFilter->setCurrentIndex(statusIndex >= 0 ? statusIndex : 0);

        form->addRow(dateEnabled, dateEdit);
        form->addRow(QStringLiteral("项目标签:"), tagFilter);
        form->addRow(QStringLiteral("植物:"), plantFilter);
        form->addRow(QStringLiteral("状态:"), statusFilter);
        layout->addWidget(title);
        layout->addLayout(form);
        auto* actions = new QHBoxLayout;
        actions->setSpacing(8);
        auto* reset = new QPushButton(QStringLiteral("清除筛选"), &dialog);
        reset->setProperty("testId", "forestRecordFilterReset");
        auto* cancel = new QPushButton(QStringLiteral("取消"), &dialog);
        cancel->setProperty("testId", "forestRecordFilterCancel");
        auto* apply = new QPushButton(QStringLiteral("应用"), &dialog);
        apply->setProperty("testId", "forestRecordFilterApply");
        DialogPresenter::setSecondary(reset);
        DialogPresenter::setSecondary(cancel);
        DialogPresenter::setPrimary(apply);
        cancel->setDefault(true);
        cancel->setAutoDefault(false);
        reset->setAutoDefault(false);
        apply->setAutoDefault(false);
        actions->addWidget(reset);
        actions->addStretch();
        actions->addWidget(cancel);
        actions->addWidget(apply);
        layout->addLayout(actions);
        QObject::connect(reset, &QPushButton::clicked, &dialog, [this, &dialog]() {
            forestDashboard_->setRecordFilter({});
            dialog.accept();
        });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(apply, &QPushButton::clicked, &dialog,
                         [this, &dialog, dateEnabled, dateEdit, tagFilter, plantFilter, statusFilter]() {
            ForestDashboardWidget::RecordFilter filter;
            filter.date = dateEnabled->isChecked() ? dateEdit->date() : QDate();
            filter.tagId = tagFilter->currentData().toUInt();
            filter.plantType = plantFilter->currentData().toUInt();
            filter.terminalState = statusFilter->currentData().toInt();
            forestDashboard_->setRecordFilter(filter);
            dialog.accept();
        });
        dialog.adjustSize();
        QPainterPath filterShape;
        filterShape.addRoundedRect(QRectF(dialog.rect()), 20, 20);
        dialog.setMask(QRegion(filterShape.toFillPolygon().toPolygon()));
        dialog.exec();
    });
    QObject::connect(forestDashboard_, &ForestDashboardWidget::overviewRequested,
                     this, [this]() {
        QDialog dialog(this);
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setWindowTitle(QStringLiteral("总览筛选"));
        DialogPresenter::prepare(dialog);
        dialog.setObjectName("forestOverviewDialog");
        dialog.setFixedWidth(720);
        dialog.setStyleSheet(dialog.styleSheet().replace(QStringLiteral("#forestDialog"),
                                                        QStringLiteral("#forestOverviewDialog")) + QStringLiteral(
            "QDialog#forestOverviewDialog {"
            " background:#FFFFFF; border:1px solid #D5DFDA; border-radius:20px; }"
            "QLabel { background:transparent; border:none; }"
            "QDialog#forestOverviewDialog QLabel#forestOverviewLeaf { background:#EFF5F2; border-radius:8px; color:#246B57; font-size:21px; }"
            "QDialog#forestOverviewDialog QLabel#forestOverviewTitle { color:#203C35; font-size:22px; font-weight:800; }"
            "QDialog#forestOverviewDialog QLabel#forestOverviewDescription { color:#53665F; font-size:14px; }"
            "QWidget#forestOverviewTagCard { background:transparent; border:none; }"
            "QWidget#forestOverviewAddRow { background:transparent; border:none; }"
            "QLabel#forestOverviewTagLabel { color:#315747; font-size:14px; font-weight:800; }"
            "QScrollArea#forestOverviewTagScroll { background:transparent; border:none; }"
            "QScrollArea#forestOverviewTagScroll > QWidget > QWidget { background:transparent; }"
            "QLineEdit#forestOverviewAddInput { background:#F5F7F8; border:1px solid #D5DFDA;"
            " border-radius:8px; color:#203C35; padding:7px 10px; selection-background-color:#C9F0DE; selection-color:#203C35; }"
            "QLineEdit#forestOverviewAddInput:focus { border:2px solid #246B57; padding:6px 9px; }"
            "QDialog#forestOverviewDialog QPushButton#forestOverviewAddTag { background:#E9ECEA; border:1px dashed #9DB9A5;"
            " border-radius:16px; color:#526A5D; font-size:26px; font-weight:500; padding:0; min-height:0; }"
            "QDialog#forestOverviewDialog QPushButton#forestOverviewAddTag:hover { background:#FFFFFF; border-color:#3E9A6F; color:#245543; }"
            "QDialog#forestOverviewDialog QPushButton#forestOverviewAddTag:focus { border:2px solid #3E9A6F; }"
            "QDialog#forestOverviewDialog QPushButton#forestOverviewAddTag:pressed { background:#D5E9DD; }"));

        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(28, 26, 28, 24);
        layout->setSpacing(20);
        auto* titleRow = new QHBoxLayout;
        titleRow->setSpacing(12);
        auto* leaf = new QLabel(QString::fromUtf8(u8"🌿"), &dialog);
        leaf->setObjectName("forestOverviewLeaf");
        leaf->setAlignment(Qt::AlignCenter);
        leaf->setFixedSize(44, 44);
        auto* title = new QLabel(QStringLiteral("总览筛选"), &dialog);
        title->setObjectName("forestOverviewTitle");
        auto* description = new QLabel(
            QStringLiteral("按照项目标签，看看你的专注足迹。"), &dialog);
        description->setObjectName("forestOverviewDescription");
        description->setWordWrap(true);
        titleRow->addWidget(leaf);
        titleRow->addWidget(title);
        titleRow->addStretch();

        auto* tagCard = new QWidget(&dialog);
        tagCard->setObjectName("forestOverviewTagCard");
        auto* tagLayout = new QVBoxLayout(tagCard);
        tagLayout->setContentsMargins(0, 0, 0, 0);
        tagLayout->setSpacing(12);
        auto* tagLabel = new QLabel(QStringLiteral("项目标签"), tagCard);
        tagLabel->setObjectName("forestOverviewTagLabel");
        const auto current = forestDashboard_->recordFilter();
        uint32_t selectedTagId = current.tagId;
        auto* tagScroll = new QScrollArea(tagCard);
        tagScroll->setObjectName("forestOverviewTagScroll");
        tagScroll->setWidgetResizable(true);
        tagScroll->setFrameShape(QFrame::NoFrame);
        tagScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tagScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* tagOptions = new QWidget(tagScroll);
        tagOptions->setObjectName("forestOverviewTagOptions");
        tagOptions->setStyleSheet("QWidget#forestOverviewTagOptions { background:transparent; border:none; }");
        auto* tagOptionsLayout = new QGridLayout(tagOptions);
        tagOptionsLayout->setContentsMargins(0, 0, 0, 0);
        tagOptionsLayout->setHorizontalSpacing(10);
        tagOptionsLayout->setVerticalSpacing(10);
        for (int column = 0; column < 4; ++column) tagOptionsLayout->setColumnStretch(column, 1);
        QFont chipFont = tagOptions->font();
        chipFont.setPixelSize(14);
        chipFont.setWeight(QFont::ExtraBold);
        chipFont = AppStyle::scaledFont(chipFont);
        const int chipHeight = qMax(44, QFontMetrics(chipFont).height() + 20);
        int tagOptionCount = 0;
        const auto addTagOption = [&](uint32_t tagId, const QString& name, const QColor& color) {
            auto* option = new QRadioButton(
                QFontMetrics(chipFont).elidedText(name, Qt::ElideRight, 98), tagOptions);
            option->setFont(chipFont);
            option->setObjectName("forestOverviewTagOption");
            option->setProperty("tagId", QVariant::fromValue<qulonglong>(tagId));
            option->setChecked(tagId == selectedTagId);
            option->setCursor(Qt::PointingHandCursor);
            option->setFocusPolicy(Qt::StrongFocus);
            option->setFixedHeight(chipHeight);
            option->setAccessibleName(name);
            option->setToolTip(name);
            option->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            option->setStyleSheet(QStringLiteral(
                "QRadioButton { background:#E9ECEA; border:1px solid transparent; border-radius:18px;"
                " color:#3B5348; font-size:14px; font-weight:800; padding:8px 12px; }"
                "QRadioButton:hover { background:#F7FAF5; border-color:%1; }"
                "QRadioButton:checked { background:#FFFFFF; border-color:#FFFFFF; color:#245543; }"
                "QRadioButton:focus { border-color:#3F9670; }"
                "QRadioButton::indicator { width:14px; height:14px; border-radius:7px;"
                " background:%1; border:1px solid rgba(36,85,67,30); margin-right:10px; }"
                "QRadioButton::indicator:checked { border:2px solid %1; }").arg(color.name()));
            QObject::connect(option, &QRadioButton::toggled, &dialog,
                              [&selectedTagId, tagId](bool checked) {
                                 if (checked) selectedTagId = tagId;
                             });
            tagOptionsLayout->addWidget(option, tagOptionCount / 4, tagOptionCount % 4);
            ++tagOptionCount;
            return option;
        };
        addTagOption(0, QStringLiteral("无标签"), QColor("#B8B6EA"));
        if (m_tagMgr) {
            for (const TagDef& tag : m_tagMgr->all()) {
                QColor tagColor(tag.color);
                addTagOption(tag.id, tag.name, tagColor.isValid() ? tagColor : QColor("#7EC8A0"));
            }
        }
        auto* addTagButton = new QPushButton(QStringLiteral("＋"), tagOptions);
        addTagButton->setObjectName("forestOverviewAddTag");
        addTagButton->setProperty("testId", "forestOverviewAddTag");
        addTagButton->setToolTip(QStringLiteral("新增项目标签"));
        addTagButton->setFocusPolicy(Qt::StrongFocus);
        addTagButton->setAutoDefault(false);
        addTagButton->setCursor(Qt::PointingHandCursor);
        addTagButton->setFixedHeight(chipHeight);
        addTagButton->setAccessibleName(QStringLiteral("新增项目标签"));
        addTagButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const auto refreshTagGrid = [&]() {
            tagOptionsLayout->removeWidget(addTagButton);
            tagOptionsLayout->addWidget(addTagButton, tagOptionCount / 4, tagOptionCount % 4);
            const int totalChips = tagOptionCount + 1;
            const int visibleRows = qMin(4, (totalChips + 3) / 4);
            const int allRows = (totalChips + 3) / 4;
            tagScroll->setVerticalScrollBarPolicy(
                totalChips > 16 ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
            tagScroll->setFixedHeight(visibleRows * chipHeight + qMax(0, visibleRows - 1) * 10 + 2);
            tagOptions->setMinimumHeight(allRows * chipHeight + qMax(0, allRows - 1) * 10 + 2);
        };
        refreshTagGrid();
        tagScroll->setWidget(tagOptions);

        auto* addRow = new QWidget(tagCard);
        addRow->setObjectName("forestOverviewAddRow");
        auto* addRowLayout = new QHBoxLayout(addRow);
        addRowLayout->setContentsMargins(0, 0, 0, 0);
        addRowLayout->setSpacing(8);
        auto* addInput = new QLineEdit(addRow);
        addInput->setObjectName("forestOverviewAddInput");
        addInput->setPlaceholderText(QStringLiteral("输入新标签名称"));
        addInput->setMinimumHeight(40);
        auto* addConfirm = new QPushButton(QStringLiteral("添加"), addRow);
        addConfirm->setProperty("testId", "forestOverviewAddConfirm");
        DialogPresenter::setPrimary(addConfirm);
        addConfirm->setMinimumHeight(40);
        addConfirm->setAutoDefault(false);
        QWidget::setTabOrder(addInput, addConfirm);
        addRowLayout->addWidget(addInput, 1);
        addRowLayout->addWidget(addConfirm);
        addRow->hide();

        const auto updateOverviewShape = [&dialog]() {
            dialog.adjustSize();
            QPainterPath shape;
            shape.addRoundedRect(QRectF(dialog.rect()), 20, 20);
            dialog.setMask(QRegion(shape.toFillPolygon().toPolygon()));
        };
        QObject::connect(addTagButton, &QPushButton::clicked, &dialog, [addRow, addInput, updateOverviewShape]() {
            addRow->show();
            updateOverviewShape();
            addInput->setFocus();
        });
        QObject::connect(addInput, &QLineEdit::returnPressed, addConfirm, &QPushButton::click);
        QObject::connect(addConfirm, &QPushButton::clicked, &dialog,
                         [this, &selectedTagId, addInput, addRow, tagScroll, addTagOption, refreshTagGrid, updateOverviewShape]() {
            if (!m_tagMgr) return;
            const auto result = m_tagMgr->addAndSave(addInput->text());
            if (result.status == TagManager::AddStatus::EmptyName) {
                DialogPresenter::warning(this, QStringLiteral("标签名称为空"),
                                         QStringLiteral("请输入一个项目标签名称。"));
                return;
            }
            if (result.status == TagManager::AddStatus::DuplicateName) {
                DialogPresenter::warning(this, QStringLiteral("标签已存在"),
                                         QStringLiteral("请使用一个不同的项目标签名称。"));
                return;
            }
            if (result.status == TagManager::AddStatus::LimitReached) {
                DialogPresenter::warning(this, QStringLiteral("无法添加标签"),
                                         QStringLiteral("当前标签数量或编号已达到可保存范围。"));
                return;
            }
            if (result.status == TagManager::AddStatus::SaveFailed) {
                DialogPresenter::error(this, QStringLiteral("保存标签失败"),
                                       QStringLiteral("新标签未能安全保存，请稍后重试。"));
                return;
            }
            selectedTagId = result.tag.id;
            auto* addedOption = addTagOption(result.tag.id, result.tag.name, QColor(result.tag.color));
            addedOption->setChecked(true);
            refreshTagGrid();
            refreshTagOptions();
            addInput->clear();
            addRow->hide();
            updateOverviewShape();
            tagScroll->ensureWidgetVisible(addedOption);
            addedOption->setFocus(Qt::OtherFocusReason);
        });
        tagLayout->addWidget(tagLabel);
        tagLayout->addWidget(tagScroll);
        tagLayout->addWidget(addRow);

        auto* actions = new QHBoxLayout;
        auto* clear = new QPushButton(QStringLiteral("清除项目标签"), &dialog);
        auto* cancel = new QPushButton(QStringLiteral("取消"), &dialog);
        auto* apply = new QPushButton(QStringLiteral("应用"), &dialog);
        apply->setProperty("testId", "forestOverviewApply");
        DialogPresenter::setSecondary(clear);
        DialogPresenter::setSecondary(cancel);
        DialogPresenter::setPrimary(apply);
        cancel->setDefault(false);
        cancel->setAutoDefault(false);
        clear->setAutoDefault(false);
        apply->setAutoDefault(false);
        clear->setToolTip(QStringLiteral("显示全部项目标签的记录。"));
        for (auto* button : {clear, cancel, apply}) button->setMinimumHeight(40);
        QWidget::setTabOrder(addConfirm, clear);
        QWidget::setTabOrder(clear, cancel);
        QWidget::setTabOrder(cancel, apply);
        actions->addWidget(clear);
        actions->addStretch();
        actions->addWidget(cancel);
        actions->addWidget(apply);

        layout->addLayout(titleRow);
        layout->addWidget(description);
        layout->addWidget(tagCard);
        layout->addLayout(actions);
        QObject::connect(clear, &QPushButton::clicked, &dialog, [this, &dialog, current]() {
            auto filter = current;
            filter.tagId = std::numeric_limits<uint32_t>::max();
            forestDashboard_->setRecordFilter(filter);
            dialog.accept();
        });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(apply, &QPushButton::clicked, &dialog,
                         [this, &dialog, &selectedTagId, current]() {
            auto filter = current;
            filter.tagId = selectedTagId;
            forestDashboard_->setRecordFilter(filter);
            dialog.accept();
        });
        updateOverviewShape();
        cancel->setFocus(Qt::OtherFocusReason);
        dialog.exec();
    });
    challengeRewards_ = std::make_unique<ChallengeRewardService>(coinManager_);
    challengeDashboard_ = new ChallengeDashboardWidget;
    challengeDashboard_->setRewardService(challengeRewards_.get());
    QObject::connect(challengeDashboard_, &ChallengeDashboardWidget::openFocusRequested,
                     this, [this]() { switchPage(0); });
    QObject::connect(challengeDashboard_, &ChallengeDashboardWidget::openShopRequested,
                     this, [this]() { switchPage(2); });
    QObject::connect(challengeDashboard_, &ChallengeDashboardWidget::openPlantSettingsRequested,
                     this, [this]() { switchPage(3); });
    QObject::connect(challengeDashboard_, &ChallengeDashboardWidget::growthRulesRequested,
                     this, [this]() {
        DialogPresenter::detail(this, QStringLiteral("成长值规则"),
            QStringLiteral("成长值根据专注时长、完成次数和挑战进度动态计算，用于表现森林成长。"));
    });
    QObject::connect(challengeDashboard_, &ChallengeDashboardWidget::rewardsChanged,
                     this, [this](int) {
        refreshChallengePage();
    });

    // ========== Page 0: 专注主页 ==========
    auto* page0 = new QWidget;
    page0->setObjectName("focusPage");
    page0->setStyleSheet(QStringLiteral("QWidget#focusPage { background:%1; }")
        .arg(AppStyle::color(AppStyle::Color::Workspace).name()));
    auto* p0Layout = new QVBoxLayout(page0);
    p0Layout->setContentsMargins(28, 24, 28, 24);
    p0Layout->setSpacing(0);

    auto* focusContent = new QWidget(page0);
    focusContent->setObjectName("focusContent");
    focusContent->setMinimumWidth(620);
    focusContent->setMaximumWidth(980);
    focusContent->setMaximumHeight(980);
    focusContent->setStyleSheet("QWidget#focusContent { background:transparent; border:none; }");
    auto* focusPageLayout = new QVBoxLayout(focusContent);
    focusPageLayout->setContentsMargins(0, 0, 0, 0);
    focusPageLayout->setSpacing(12);
    focusPageLayout->setAlignment(Qt::AlignTop);
    p0Layout->addWidget(focusContent, 1, Qt::AlignHCenter);

    auto* statusLayout = new QHBoxLayout;
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);

    auto* modeSwitch = new QWidget(page0);
    modeSwitch->setObjectName("focusModeSwitch");
    modeSwitch->setFixedHeight(48);
    modeSwitch->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    modeSwitch->setStyleSheet(QStringLiteral(
        "QWidget#focusModeSwitch { background:%1; border:1px solid %2; border-radius:8px; }"
        "QPushButton[mode=\"true\"] { background:transparent; border:1px solid transparent;"
        " border-radius:6px; color:%3; font-size:15px; font-weight:600; padding:0; min-height:0; }"
        "QPushButton[mode=\"true\"]:hover { background:%4; }"
        "QPushButton[mode=\"true\"][active=\"true\"] { background:%4; color:%5; }"
        "QPushButton[mode=\"true\"]:focus { border:2px solid %6; }"
        "QPushButton[mode=\"true\"]:disabled { color:%3; background:%1; }")
        .arg(AppStyle::color(AppStyle::Color::Surface).name(), AppStyle::color(AppStyle::Color::Border).name(),
             AppStyle::color(AppStyle::Color::Muted).name(), AppStyle::color(AppStyle::Color::Selection).name(),
             AppStyle::color(AppStyle::Color::Text).name(), AppStyle::color(AppStyle::Color::Primary).name()));
    auto* modeSwitchLayout = new QHBoxLayout(modeSwitch);
    modeSwitchLayout->setContentsMargins(4, 4, 4, 4);
    modeSwitchLayout->setSpacing(4);
    homeCountdownBtn_ = new QPushButton(QIcon(":/icons/hourglass.png"), QStringLiteral("倒计时"), modeSwitch);
    homeStopwatchBtn_ = new QPushButton(QIcon(":/icons/timer.png"), QStringLiteral("正计时"), modeSwitch);
    homeCountdownBtn_->setObjectName("focusCountdownMode");
    homeStopwatchBtn_->setObjectName("focusStopwatchMode");
    homeCountdownBtn_->setIconSize(QSize(20, 20));
    homeStopwatchBtn_->setIconSize(QSize(20, 20));
    homeCountdownBtn_->setProperty("mode", "true");
    homeStopwatchBtn_->setProperty("mode", "true");
    homeCountdownBtn_->setToolTip(QStringLiteral("倒计时"));
    homeStopwatchBtn_->setToolTip(QStringLiteral("正计时"));
    homeCountdownBtn_->setAccessibleName(QStringLiteral("倒计时"));
    homeStopwatchBtn_->setAccessibleName(QStringLiteral("正计时"));
    homeCountdownBtn_->setCursor(Qt::PointingHandCursor);
    homeStopwatchBtn_->setCursor(Qt::PointingHandCursor);
    homeCountdownBtn_->setFixedSize(136, 38);
    homeStopwatchBtn_->setFixedSize(136, 38);
    modeSwitchLayout->addWidget(homeCountdownBtn_);
    modeSwitchLayout->addWidget(homeStopwatchBtn_);

    coinLabel_ = new QLabel(page0);
    coinLabel_->setObjectName("focusCoinBalance");
    coinLabel_->setFixedHeight(44);
    coinLabel_->setAlignment(Qt::AlignCenter);
    coinLabel_->setStyleSheet(QStringLiteral("background:transparent; border:none;"
        " padding:0 8px; color:%1; font-size:14px; font-weight:600;")
        .arg(AppStyle::color(AppStyle::Color::Muted).name()));
    coinLabel_->setText(QStringLiteral("🪙 %1").arg(coinManager_.balance()));
    coinLabel_->setMinimumWidth(86);

    auto* modeBalanceSpacer = new QWidget(page0);
    modeBalanceSpacer->setFixedWidth(86);
    modeBalanceSpacer->setStyleSheet("background:transparent; border:none;");
    statusLayout->addWidget(modeBalanceSpacer);
    statusLayout->addStretch();
    statusLayout->addWidget(modeSwitch);
    statusLayout->addStretch();
    statusLayout->addWidget(coinLabel_);
    focusPageLayout->addLayout(statusLayout);

    auto* focusHint = new QLabel(QString::fromUtf8(u8"开始种树吧!"), page0);
    focusHint_ = focusHint;
    focusHint->setObjectName("focusHint");
    focusHint->setFixedHeight(36);
    focusHint->setAlignment(Qt::AlignCenter);
    focusHint->setStyleSheet(QStringLiteral("font-size:15px; font-weight:600; color:%1;"
        " padding:4px 20px; background:transparent; border:none;")
        .arg(AppStyle::color(AppStyle::Color::Muted).name()));
    auto* focusHintLayout = new QHBoxLayout;
    focusHintLayout->setContentsMargins(0, 0, 0, 0);
    focusHintLayout->addStretch();
    focusHintLayout->addWidget(focusHint);
    focusHintLayout->addStretch();
    focusPageLayout->addLayout(focusHintLayout);
    timerRing_->setMaximumHeight(700);
    focusPageLayout->addWidget(timerRing_, 1);

    homeModeOverlay_ = new QWidget(centralWidget());
    homeModeOverlay_->setObjectName("homeModeOverlay");
    homeModeOverlay_->setStyleSheet(
        "QWidget#homeModeOverlay { background-color:rgba(0,0,0,96); border:none; }");
    homeModeOverlay_->hide();
    homeModeOverlay_->installEventFilter(this);
    auto* overlayLayout = new QVBoxLayout(homeModeOverlay_);
    overlayLayout->setContentsMargins(16, 86, 16, 16);
    overlayLayout->setSpacing(0);
    overlayLayout->addStretch();

    homeModePanel_ = new QWidget(homeModeOverlay_);
    auto* modePanel = homeModePanel_;
    modePanel->setObjectName("homeModePanel");
    modePanel->setStyleSheet(
        "QWidget#homeModePanel { background-color:#FFFFFF;"
        " border:1px solid #D5DFDA; border-radius:8px; }"
        "QWidget[optionRow=\"true\"] { background:transparent; border:none; }"
        "QLabel[optionIcon=\"true\"] { color:rgba(247,255,247,220); font-size:24px;"
        " background:transparent; border:none; }"
        "QLabel[optionTitle=\"true\"] { color:#203C35; font-size:16px; font-weight:600;"
        " background:transparent; border:none; }"
        "QLabel[optionDesc=\"true\"] { color:#53665F; font-size:13px;"
        " background:transparent; border:none; }"
        "QCheckBox { background:transparent; color:#203C35; font-weight:600; spacing:8px; }"
        "QCheckBox::indicator { width:36px; height:20px; border-radius:10px;"
        " background-color:#E3EAE6; border:1px solid #A9CDBE; }"
        "QCheckBox::indicator:checked { background-color:#246B57; border-color:#246B57; }"
        "QCheckBox::indicator:disabled { background:#E3EAE6; border-color:#CCD8D1; }"
        "QCheckBox:focus { border:1px solid #246B57; }");
    modePanel->setMinimumWidth(560);
    modePanel->setMaximumWidth(900);
    overlayLayout->addWidget(modePanel, 0, Qt::AlignHCenter);
    overlayLayout->addStretch(2);

    auto* modePanelLayout = new QVBoxLayout(modePanel);
    modePanelLayout->setContentsMargins(28, 20, 28, 20);
    modePanelLayout->setSpacing(14);

    countdownOptionsWidget_ = new QWidget(modePanel);
    countdownOptionsWidget_->setStyleSheet("background:transparent; border:none;");
    auto* optionList = new QVBoxLayout(countdownOptionsWidget_);
    optionList->setContentsMargins(4, 2, 4, 2);
    optionList->setSpacing(10);

    auto makeOptionRow = [this](QWidget* parent,
                                const QString& icon,
                                const QString& title,
                                const QString& desc,
                                QCheckBox** outCheck) {
        auto* row = new QWidget(parent);
        row->setProperty("optionRow", "true");
        row->setMinimumHeight(74);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 6, 0, 6);
        rowLayout->setSpacing(16);

        auto* iconLabel = new QLabel(icon, row);
        iconLabel->setProperty("optionIcon", "true");
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setFixedWidth(42);

        auto* textWrap = new QWidget(row);
        textWrap->setStyleSheet("background:transparent; border:none;");
        auto* textLayout = new QVBoxLayout(textWrap);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(4);
        auto* titleLabel = new QLabel(title, textWrap);
        titleLabel->setProperty("optionTitle", "true");
        titleLabel->setMinimumHeight(24);
        auto* descLabel = new QLabel(desc, textWrap);
        descLabel->setProperty("optionDesc", "true");
        descLabel->setWordWrap(true);
        descLabel->setMinimumHeight(34);
        descLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        textLayout->addWidget(titleLabel);
        textLayout->addWidget(descLabel);

        auto* check = new QCheckBox(row);
        check->setCursor(Qt::PointingHandCursor);
        check->setFixedWidth(90);
        check->setAccessibleName(title);
        check->setToolTip(title);
        check->setText(QStringLiteral("关"));
        QObject::connect(check, &QCheckBox::toggled, check, [check](bool checked) {
            check->setText(checked ? QStringLiteral("开") : QStringLiteral("关"));
        });

        rowLayout->addWidget(iconLabel);
        rowLayout->addWidget(textWrap, 1);
        rowLayout->addWidget(check);
        *outCheck = check;
        return row;
    };

    homeAllowPauseCheck_ = new QCheckBox(countdownOptionsWidget_);
    homeAllowPauseCheck_->hide();
    homeDeepFocusCheck_ = nullptr;
    homeGroupPlantCheck_ = nullptr;
    homeAutoExtendCheck_ = nullptr;
    auto* deepFocusRow = makeOptionRow(
        countdownOptionsWidget_,
        QString::fromUtf8(u8"🔥"),
        QStringLiteral("深度专注"),
        QStringLiteral("使用黑名单应用时有十秒返回宽限"),
        &homeDeepFocusCheck_);
    auto* groupPlantRow = makeOptionRow(
        countdownOptionsWidget_,
        QString::fromUtf8(u8"👥"),
        QStringLiteral("多人一起种"),
        QStringLiteral("一人放弃将导致所有人的小树枯萎"),
        &homeGroupPlantCheck_);
    groupPlantRow->setObjectName("groupPlantOptionRow");
    auto* autoExtendRow = makeOptionRow(
        countdownOptionsWidget_,
        QString::fromUtf8(u8"⏱+"),
        QStringLiteral("延长计时"),
        QStringLiteral("种植结束后继续计时"),
        &homeAutoExtendCheck_);
    autoExtendRow->setObjectName("autoExtendOptionRow");
    homeAllowPauseCheck_->setChecked(true);
    homeDeepFocusCheck_->setChecked(true);
    homeAutoExtendCheck_->setChecked(false);
    homeGroupPlantCheck_->setChecked(false);
    homeGroupPlantCheck_->setEnabled(false);
    homeGroupPlantCheck_->setToolTip(QStringLiteral("多人一起种功能暂未接入当前专注流程"));
    optionList->addWidget(deepFocusRow);
    optionList->addWidget(groupPlantRow);
    optionList->addWidget(autoExtendRow);
    modePanelLayout->addWidget(countdownOptionsWidget_);

    pauseBreakOverlay_ = new QWidget(centralWidget());
    pauseBreakOverlay_->setObjectName("pauseBreakOverlay");
    pauseBreakOverlay_->setStyleSheet(
        "QWidget#pauseBreakOverlay { background-color:rgba(0,0,0,118); border:none; }"
        "QFrame#pauseBreakPanel { background-color:#FFFFFF;"
        " border:1px solid #D5DFDA; border-radius:14px; }"
        "QLabel#pauseBreakTitle { color:#203C35; font-size:20px; font-weight:600;"
        " background:transparent; border:none; }"
        "QLabel#pauseBreakTime { color:#203C35; font-size:52px; font-weight:300;"
        " background:transparent; border:none; }"
        "QPushButton#pauseBreakContinue { background-color:#246B57; color:#FFFFFF;"
        " border:1px solid #246B57; border-radius:8px;"
        " padding:10px 34px; font-weight:600; font-size:15px; }"
        "QPushButton#pauseBreakContinue:hover { background-color:#1D5948; }"
        "QPushButton#pauseBreakContinue:pressed { background-color:#174838; }");
    pauseBreakOverlay_->hide();
    pauseBreakOverlay_->installEventFilter(this);
    auto* pauseOverlayLayout = new QVBoxLayout(pauseBreakOverlay_);
    pauseOverlayLayout->setContentsMargins(24, 24, 24, 24);
    pauseOverlayLayout->addStretch();

    auto* pausePanel = new QFrame(pauseBreakOverlay_);
    pausePanel->setObjectName("pauseBreakPanel");
    auto* pausePanelLayout = new QVBoxLayout(pausePanel);
    pausePanelLayout->setContentsMargins(36, 28, 36, 30);
    pausePanelLayout->setSpacing(16);

    auto* pauseTitle = new QLabel(QStringLiteral("暂停中"), pausePanel);
    pauseTitle->setObjectName("pauseBreakTitle");
    pauseTitle->setAlignment(Qt::AlignCenter);
    pauseBreakTimeLabel_ = new QLabel(QStringLiteral("05:00"), pausePanel);
    pauseBreakTimeLabel_->setObjectName("pauseBreakTime");
    pauseBreakTimeLabel_->setAlignment(Qt::AlignCenter);
    pauseBreakContinueBtn_ = new QPushButton(QStringLiteral("继续专注"), pausePanel);
    pauseBreakContinueBtn_->setObjectName("pauseBreakContinue");
    pauseBreakContinueBtn_->setCursor(Qt::PointingHandCursor);
    pausePanelLayout->addWidget(pauseTitle);
    pausePanelLayout->addWidget(pauseBreakTimeLabel_);
    pausePanelLayout->addWidget(pauseBreakContinueBtn_, 0, Qt::AlignHCenter);
    pauseOverlayLayout->addWidget(pausePanel, 0, Qt::AlignHCenter);
    pauseOverlayLayout->addStretch();
    QObject::connect(pauseBreakContinueBtn_, &QPushButton::clicked,
                     this, &MainWindow::resumeFromPauseBreak);
    QObject::connect(&pauseBreakTimer_, &QTimer::timeout, this, [this]() {
        controller_.tick();
        if (controller_.currentState() != FocusController::State::PAUSED) {
            hidePauseBreakOverlay();
            return;
        }
        pauseBreakRemainingSeconds_ = static_cast<int>(controller_.pauseRemainingSeconds());
        updatePauseBreakText();
    });

    auto* btnLayout = new QHBoxLayout;
    startBtn_   = new FocusStartButton;
    startBtn_->setText(QStringLiteral("开始专注"));
    startBtn_->setObjectName("btnPrimary");
    startBtn_->setProperty("testId", "focusStartButton");
    pauseBtn_   = new QPushButton(QStringLiteral("暂停"));
    pauseBtn_->setObjectName("btnPauseAction");
    pauseBtn_->setProperty("testId", "focusPauseButton");
    abandonBtn_ = new QPushButton(QStringLiteral("放弃"));
    abandonBtn_->setObjectName("btnDangerAction");
    abandonBtn_->setProperty("testId", "focusAbandonButton");
    startBtn_->setToolTip(QStringLiteral("应用内快捷键：Ctrl+Alt+F（不是全局热键）"));
    pauseBtn_->setToolTip(QStringLiteral("应用内快捷键：Ctrl+Alt+P（不是全局热键）"));
    abandonBtn_->setToolTip(QStringLiteral("应用内快捷键：Ctrl+Alt+A（不是全局热键）"));
    pauseBtn_->setEnabled(false);
    abandonBtn_->setEnabled(false);
    pauseBtn_->setVisible(false);
    abandonBtn_->setVisible(false);
    startBtn_->setCursor(Qt::PointingHandCursor);
    pauseBtn_->setCursor(Qt::PointingHandCursor);
    abandonBtn_->setCursor(Qt::PointingHandCursor);
    startBtn_->setFixedSize(256, 52);
    startBtn_->setFont(QFont("Microsoft YaHei", 14, QFont::DemiBold));
    startBtn_->setStyleSheet(
        "QPushButton#btnPrimary { padding:0; border-width:2px; min-height:48px; font-size:18px; }");
    pauseBtn_->setMinimumSize(128, 42);
    abandonBtn_->setMinimumSize(128, 42);
    pauseBtn_->setStyleSheet(
        "QPushButton#btnPauseAction { background-color:#9B6A3D; border-color:#C08A52;"
        " color:#FFF8EE; font-weight:900; }"
        "QPushButton#btnPauseAction:hover { background-color:#B47A45; border-color:#F2D6A2; }"
        "QPushButton#btnPauseAction:pressed { background-color:#7D4E27; }"
        "QPushButton#btnPauseAction:disabled { background-color:rgba(130,84,44,95);"
        " border-color:rgba(255,248,238,45); color:rgba(255,248,238,120); }");
    abandonBtn_->setStyleSheet(
        "QPushButton#btnDangerAction { background-color:#D9534F; border-color:#F28A84;"
        " color:#FFF7F7; font-weight:900; }"
        "QPushButton#btnDangerAction:hover { background-color:#E76561; border-color:#FFD0CC; }"
        "QPushButton#btnDangerAction:pressed { background-color:#B83E3A; }");
    btnLayout->setContentsMargins(0, 18, 0, 0);
    btnLayout->setSpacing(12);
    btnLayout->addStretch();
    btnLayout->addWidget(startBtn_);
    btnLayout->addWidget(pauseBtn_);
    btnLayout->addWidget(abandonBtn_);
    btnLayout->addStretch();
    focusPageLayout->addLayout(btnLayout);

    stackedWidget_->addWidget(page0);

    // ========== Page 1: 我的森林 ==========
    auto* page1 = new QWidget;
    page1->setObjectName("forestPage");
    page1->setStyleSheet(
        "QWidget#forestPage { background:#F5F7F8; }");
    auto* p1Layout = new QVBoxLayout(page1);
    p1Layout->setContentsMargins(0, 0, 0, 0);

    auto* forestScroll = new QScrollArea(page1);
    forestScroll->setObjectName("forestScroll");
    forestScroll->setWidgetResizable(true);
    forestScroll->setFrameShape(QFrame::NoFrame);
    forestScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    forestScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    forestScroll->setStyleSheet(
        "QScrollArea { background:transparent; border:none; }"
        "QScrollArea > QWidget > QWidget { background:transparent; }");
    forestDashboard_->setParent(forestScroll);
    forestDashboard_->setMinimumHeight(860);
    forestScroll->setWidget(forestDashboard_);
    p1Layout->addWidget(forestScroll);

    stackedWidget_->addWidget(page1);

    // ========== Page 2: 植物商城 ==========
    stackedWidget_->addWidget(shopPage_->page());

    // ========== Page 3: 系统设置 ==========
    auto* page5 = new QWidget;
    page5->setObjectName("settingsPage");
    page5->setStyleSheet(
        "QWidget#settingsPage { background:#F5F7F8; }"
        "QWidget#settingsBody, QStackedWidget#settingsCategoryStack,"
        " QScrollArea#settingsSectionScroll, QWidget#settingsSectionViewport,"
        " QWidget#focusSettingsContent, QWidget#rulesSettingsContent,"
        " QWidget#appearanceSettingsContent, QWidget#dataSettingsContent, QWidget#settingsSectionIntro {"
        " background:transparent; border:none; }"
        "QListWidget#settingsCategoryNav { background:transparent; border:none; padding:4px; }"
        "QListWidget#settingsCategoryNav::item { color:#53665F; padding:12px; margin:3px 0; }"
        "QListWidget#settingsCategoryNav::item:hover { background:#EAF0ED; color:#203C35; }"
        "QListWidget#settingsCategoryNav::item:selected { background:#C9F0DE; color:#203C35; }"
        "QWidget#settingsPage QWidget[settingRow=\"true\"] { background:transparent;"
        " border:none; border-bottom:1px solid #D5DFDA; border-radius:0; }"
        "QWidget#settingsPage QLabel { color:#285E4E; background:transparent; border:none; }"
        "QWidget#settingsPage QLabel[settingTitle=\"true\"] { font-size:16px; font-weight:700; }"
        "QWidget#settingsPage QLabel[settingDescription=\"true\"] { color:#52746A; font-size:14px; }"
        "QWidget#settingsPage QWidget#settingsDataActions, QWidget#settingsPage QWidget#settingsRuleEditor,"
        " QWidget#settingsPage QWidget#settingsRestoreSection { background:transparent; border:none; }"
        "QWidget#settingsPage QComboBox, QWidget#settingsPage QSpinBox,"
        " QWidget#settingsPage QLineEdit { color:#203C35; background:#FFFFFF;"
        " border:1px solid #D5DFDA; border-radius:6px; min-height:34px; padding:4px 10px; }"
        "QWidget#settingsPage QComboBox:focus, QWidget#settingsPage QSpinBox:focus,"
        " QWidget#settingsPage QLineEdit:focus { border:2px solid #287C61; }"
        "QWidget#settingsPage QComboBox:disabled, QWidget#settingsPage QSpinBox:disabled {"
        " color:#637B72; background:#E3EAE6; border-color:#CCD8D1; }"
        "QWidget#settingsPage QCheckBox { color:#285E4E; background:transparent;"
        " border:none; spacing:10px; padding:6px; }"
        "QWidget#settingsPage QCheckBox:focus { outline:none; border:1px solid #287C61; }"
        "QWidget#settingsPage QCheckBox::indicator { width:18px; height:18px;"
        " border:1px solid #487968; background:#F8FCF9; }"
        "QWidget#settingsPage QCheckBox::indicator:checked { background:#2F805B; border-color:#245543; }"
        "QWidget#settingsPage QListWidget#processBlacklist { background:#FFFFFF;"
        " color:#203C35; border:1px solid #D5DFDA; border-radius:6px; }"
        "QWidget#settingsPage QListWidget#processBlacklist::item { padding:10px; }"
        "QWidget#settingsPage QListWidget#processBlacklist::item:selected {"
        " color:#174D3C; background:#C6E5D6; }"
        "QWidget#settingsPage QPushButton#processBlacklistAdd { background:#246B57;"
        " color:#FFFFFF; border:1px solid #246B57; }"
        "QWidget#settingsPage QPushButton#processBlacklistRemove { background:#B34E49;"
        " color:#FFFFFF; border:1px solid #B34E49; }"
        "QWidget#settingsPage QPushButton#processBlacklistAdd:hover { background:#1D5948; }"
        "QWidget#settingsPage QPushButton#processBlacklistRemove:hover { background:#983E3A; }"
        "QWidget#settingsPage QPushButton#processBlacklistAdd:pressed { background:#174838; }"
        "QWidget#settingsPage QPushButton#processBlacklistRemove:pressed { background:#803531; }"
        "QWidget#settingsPage QPushButton#processBlacklistAdd:focus,"
        " QWidget#settingsPage QPushButton#processBlacklistRemove:focus { border:2px solid #203C35; }"
        "QWidget#settingsPage QPushButton#processBlacklistRemove:disabled {"
        " background:#E3EAE6; color:#637B72; border:1px solid #CCD8D1; }"
        "QWidget#settingsPage QPushButton:disabled { background:#E3EAE6; color:#637B72;"
        " border:1px solid #CCD8D1; }"
        "QWidget#settingsPage QPushButton#btnPrimary { background:#246B57; color:#FFFFFF; border:1px solid #246B57; }"
        "QWidget#settingsPage QPushButton#btnPrimary:hover { background:#1D5948; }"
        "QWidget#settingsPage QPushButton#btnPrimary:pressed { background:#174838; }"
        "QWidget#settingsPage QPushButton#btnSecondary { background:#FFFFFF; color:#203C35; border:1px solid #D5DFDA; }"
        "QWidget#settingsPage QPushButton#btnSecondary:hover { background:#EAF0ED; }"
        "QWidget#settingsPage QPushButton#btnSecondary:pressed { background:#C9F0DE; }"
        "QWidget#settingsPage QPushButton#btnDanger { background:#B34E49; color:#FFFFFF; border:1px solid #B34E49; }"
        "QWidget#settingsPage QPushButton#btnDanger:hover { background:#983E3A; }"
        "QWidget#settingsPage QPushButton#btnDanger:pressed { background:#803531; }"
        "QWidget#settingsPage QPushButton:focus { border:2px solid #203C35; }"
        "QWidget#settingsPage QWidget#settingsSectionIntro QLabel[settingTitle=\"true\"] {"
        " font-size:22px; color:#203C35; }");
    auto* p5Layout = new QVBoxLayout(page5);
    p5Layout->setContentsMargins(28, 24, 28, 24);
    p5Layout->setSpacing(20);

    auto* setTitle = new QLabel(QStringLiteral("系统设置"), page5);
    setTitle->setStyleSheet("font-size:26px; font-weight:bold; color:#285E4E; background:transparent; border:none;");
    setTitle->setFixedHeight(44);
    setTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    p5Layout->addWidget(setTitle);

    auto* settingsBody = new QWidget(page5);
    settingsBody->setObjectName("settingsBody");
    auto* settingsBodyLayout = new QHBoxLayout(settingsBody);
    settingsBodyLayout->setContentsMargins(0, 0, 0, 0);
    settingsBodyLayout->setSpacing(24);

    auto* settingsNav = new QListWidget(settingsBody);
    settingsNav->setObjectName("settingsCategoryNav");
    settingsNav->setFixedWidth(196);
    settingsNav->setFixedHeight(284);
    settingsNav->setAccessibleName(QStringLiteral("设置分类"));
    settingsNav->addItem(QStringLiteral("专注设置"));
    settingsNav->addItem(QStringLiteral("监督规则"));
    settingsNav->addItem(QStringLiteral("外观与无障碍"));
    settingsNav->addItem(QStringLiteral("数据与备份"));
    settingsNav->setCurrentRow(0);

    auto* settingsStack = new QStackedWidget(settingsBody);
    settingsStack->setObjectName("settingsCategoryStack");
    const auto createSettingsSection = [settingsStack](const QString& objectName) {
        auto* scroll = new QScrollArea(settingsStack);
        scroll->setObjectName("settingsSectionScroll");
        scroll->viewport()->setObjectName("settingsSectionViewport");
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        auto* content = new QWidget(scroll);
        content->setObjectName(objectName);
        content->setMaximumWidth(920);
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(0, 0, 12, 16);
        layout->setSpacing(12);
        scroll->setWidget(content);
        settingsStack->addWidget(scroll);
        return layout;
    };
    auto* focusSettingsLayout = createSettingsSection(QStringLiteral("focusSettingsContent"));
    auto* rulesSettingsLayout = createSettingsSection(QStringLiteral("rulesSettingsContent"));
    auto* appearanceSettingsLayout = createSettingsSection(QStringLiteral("appearanceSettingsContent"));
    auto* dataSettingsLayout = createSettingsSection(QStringLiteral("dataSettingsContent"));
    const auto addSectionIntro = [](QVBoxLayout* layout, const QString& title, const QString& description) {
        auto* intro = new QWidget;
        intro->setObjectName("settingsSectionIntro");
        auto* cardLayout = new QVBoxLayout(intro);
        cardLayout->setContentsMargins(0, 0, 0, 10);
        cardLayout->setSpacing(8);
        auto* titleLabel = new QLabel(title, intro);
        titleLabel->setProperty("settingTitle", true);
        auto* descriptionLabel = new QLabel(description, intro);
        descriptionLabel->setProperty("settingDescription", true);
        descriptionLabel->setWordWrap(true);
        cardLayout->addWidget(titleLabel);
        cardLayout->addWidget(descriptionLabel);
        layout->addWidget(intro);
    };
    addSectionIntro(focusSettingsLayout, QStringLiteral("专注设置"),
                    QStringLiteral("设置下一次专注的植物、标签、时长与计时行为。"));
    addSectionIntro(rulesSettingsLayout, QStringLiteral("监督规则"),
                    QStringLiteral("管理深度专注期间需要避免的应用程序。"));
    addSectionIntro(appearanceSettingsLayout, QStringLiteral("外观与无障碍"),
                    QStringLiteral("调整文字大小、动态反馈和键盘操作的可辨识度。"));
    addSectionIntro(dataSettingsLayout, QStringLiteral("数据与备份"),
                    QStringLiteral("查看本地数据状态，并创建备份、恢复或导出专注记录。"));
    auto* s3Layout = focusSettingsLayout;
    QObject::connect(settingsNav, &QListWidget::currentRowChanged, settingsStack,
                     &QStackedWidget::setCurrentIndex);
    settingsBodyLayout->addWidget(settingsNav, 0, Qt::AlignTop);
    settingsBodyLayout->addWidget(settingsStack, 1);
    p5Layout->addWidget(settingsBody, 1);

    const auto addSettingRow = [](QVBoxLayout* layout, const QString& title,
                                 const QString& description, QWidget* control) {
        auto* row = new QWidget;
        row->setProperty("settingRow", true);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(18, 14, 18, 14);
        rowLayout->setSpacing(20);
        auto* copy = new QVBoxLayout;
        copy->setSpacing(6);
        auto* heading = new QLabel(title, row);
        heading->setProperty("settingTitle", true);
        heading->setWordWrap(true);
        auto* detail = new QLabel(description, row);
        detail->setProperty("settingDescription", true);
        detail->setWordWrap(true);
        copy->addWidget(heading);
        copy->addWidget(detail);
        rowLayout->addLayout(copy, 1);
        control->setFixedWidth(240);
        control->setAccessibleName(title);
        if (auto* check = qobject_cast<QCheckBox*>(control)) {
            const QString label = check->text();
            const auto updateState = [check, label](bool checked) {
                check->setText(label + (checked ? QStringLiteral("（开）") : QStringLiteral("（关）")));
            };
            updateState(check->isChecked());
            QObject::connect(check, &QCheckBox::toggled, check, updateState);
        }
        rowLayout->addWidget(control);
        layout->addWidget(row);
    };
    settingsPlantCombo_ = new QComboBox;
    settingsPlantCombo_->setObjectName("focusPlantSelector");
    refreshPlantCombo();
    addSettingRow(s3Layout, QStringLiteral("植物选择"), QStringLiteral("下一次专注使用的已解锁植物。"), settingsPlantCombo_);

    settingsModeCombo_ = new QComboBox;
    settingsModeCombo_->setObjectName("focusModeSelector");
    settingsModeCombo_->addItem(QStringLiteral("倒计时 (番茄钟)"), 0);
    settingsModeCombo_->addItem(QStringLiteral("正计时 (自由专注)"), 1);
    addSettingRow(s3Layout, QStringLiteral("计时模式"), QStringLiteral("倒计时设定目标，正计时记录经过时间。"), settingsModeCombo_);

    settingsDeepFocusCheck_ = new QCheckBox(QStringLiteral("开启深度专注"));
    settingsDeepFocusCheck_->setChecked(true);
    settingsDeepFocusCheck_->setCursor(Qt::PointingHandCursor);
    addSettingRow(s3Layout, QStringLiteral("深度专注"), QStringLiteral("进入黑名单应用时警告；十秒内返回不计违规。"), settingsDeepFocusCheck_);

    settingsMinutesSpin_ = new QSpinBox;
    settingsMinutesSpin_->setRange(10, 120);
    settingsMinutesSpin_->setValue(25);
    settingsMinutesSpin_->setSuffix(QStringLiteral(" 分钟"));
    addSettingRow(s3Layout, QStringLiteral("专注时长"), QStringLiteral("仅用于倒计时模式。"), settingsMinutesSpin_);

    settingsAllowPauseCheck_ = new QCheckBox(QStringLiteral("允许计时中暂停"));
    settingsAllowPauseCheck_->setChecked(UserPreferences::instance().allowPause());
    settingsAllowPauseCheck_->setCursor(Qt::PointingHandCursor);
    addSettingRow(s3Layout, QStringLiteral("计时中暂停"), QStringLiteral("暂停后五分钟自动继续，也可提前继续。"), settingsAllowPauseCheck_);

    QObject::connect(settingsModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int idx) {
            settingsMinutesSpin_->setEnabled(idx == 0);
            refreshHomeModeControls();
        });
    QObject::connect(settingsAllowPauseCheck_, &QCheckBox::toggled,
        this, [](bool checked) {
            UserPreferences::instance().setAllowPause(checked);
        });

    const auto accessibility = UserPreferences::instance().accessibilityOptions();
    auto* fontScale = new QComboBox;
    fontScale->setObjectName("accessibilityFontScale");
    fontScale->addItem(QStringLiteral("较小"), 90);
    fontScale->addItem(QStringLiteral("默认"), 100);
    fontScale->addItem(QStringLiteral("较大"), 110);
    fontScale->addItem(QStringLiteral("最大"), 125);
    const int scaleIndex = fontScale->findData(accessibility.fontScalePercent);
    fontScale->setCurrentIndex(scaleIndex >= 0 ? scaleIndex : 1);
    auto* reduceMotion = new QCheckBox(QStringLiteral("减少动态更新"));
    reduceMotion->setObjectName("accessibilityReducedMotion");
    reduceMotion->setChecked(accessibility.reducedMotion);
    auto* highContrast = new QCheckBox(QStringLiteral("增强边界与焦点"));
    highContrast->setObjectName("accessibilityHighContrast");
    highContrast->setChecked(accessibility.highContrast);
    addSettingRow(appearanceSettingsLayout, QStringLiteral("文字大小"), QStringLiteral("调整界面文字的显示比例。"), fontScale);
    addSettingRow(appearanceSettingsLayout, QStringLiteral("安静体验"), QStringLiteral("减少动态内容刷新，保持计时信息更新。"), reduceMotion);
    addSettingRow(appearanceSettingsLayout, QStringLiteral("高对比度"), QStringLiteral("突出可操作控件和键盘焦点。"), highContrast);
    const auto saveAccessibility = [this, fontScale, reduceMotion, highContrast]() {
        UserPreferences::AccessibilityOptions options;
        options.fontScalePercent = fontScale->currentData().toInt();
        options.reducedMotion = reduceMotion->isChecked();
        options.highContrast = highContrast->isChecked();
        if (UserPreferences::instance().setAccessibilityOptions(options)) {
            applyAccessibilityPreferences();
        }
    };
    QObject::connect(fontScale, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [saveAccessibility](int) { saveAccessibility(); });
    QObject::connect(reduceMotion, &QCheckBox::toggled, this,
                     [saveAccessibility](bool) { saveAccessibility(); });
    QObject::connect(highContrast, &QCheckBox::toggled, this,
                     [saveAccessibility](bool) { saveAccessibility(); });

    QObject::connect(homeCountdownBtn_, &QPushButton::clicked, this, [this]() {
        showHomeModeOptions(false);
    });
    QObject::connect(homeStopwatchBtn_, &QPushButton::clicked, this, [this]() {
        showHomeModeOptions(true);
    });
    QObject::connect(homeDeepFocusCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (settingsDeepFocusCheck_) settingsDeepFocusCheck_->setChecked(checked);
    });
    QObject::connect(settingsDeepFocusCheck_, &QCheckBox::toggled,
        this, [this](bool checked) {
            if (homeDeepFocusCheck_) homeDeepFocusCheck_->setChecked(checked);
        });

    QObject::connect(settingsPlantCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int) {
            timerRing_->setPlantType(settingsPlantCombo_->currentData().toUInt());
        });

    settingsTagCombo_ = new QComboBox;
    settingsTagCombo_->setObjectName("focusTagSelector");
    settingsTagCombo_->addItem(QStringLiteral("无标签"), 0);
    if (m_tagMgr) {
        for (const auto& t : m_tagMgr->all())
            settingsTagCombo_->addItem(t.name, t.id);
    }
    QObject::connect(settingsTagCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int) {
            refreshHomeTagBadge();
        });
    addSettingRow(s3Layout, QStringLiteral("项目标签"), QStringLiteral("下一次专注记录归属的项目。"), settingsTagCombo_);
    refreshHomeTagBadge();

    auto* guardianGoalSpin = new QSpinBox;
    guardianGoalSpin->setRange(10, 600);
    guardianGoalSpin->setValue(m_guardian ? m_guardian->dailyGoalMinutes() : 30);
    guardianGoalSpin->setSuffix(QStringLiteral(" 分钟/天"));
    QObject::connect(guardianGoalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
        this, [this, guardianGoalSpin](int v) {
            if (!m_guardian) return;
            if (!m_guardian->setDailyGoalMinutes(static_cast<uint32_t>(v))) {
                guardianGoalSpin->blockSignals(true);
                guardianGoalSpin->setValue(static_cast<int>(m_guardian->dailyGoalMinutes()));
                guardianGoalSpin->blockSignals(false);
                DialogPresenter::error(this, QStringLiteral("目标未保存"), m_guardian->lastError());
            }
            refreshGuardianPage();
        });
    addSettingRow(s3Layout, QStringLiteral("时间守护"), QStringLiteral("每天希望累计完成的专注时长。"), guardianGoalSpin);

    auto* dataGroup = new QWidget;
    dataGroup->setObjectName("settingsDataActions");
    auto* dataLayout = new QVBoxLayout(dataGroup);
    dataLayout->setContentsMargins(0, 4, 0, 16);
    dataLayout->setSpacing(18);
    auto* dataInfo = new QLabel(QStringLiteral("数据模式：%1  |  专注数据：SQLite 架构 v%2")
        .arg(PathConfig::isPortableMode() ? QStringLiteral("便携模式") : QStringLiteral("安装模式"))
        .arg(DatabaseManager::schemaVersion()));
    dataInfo->setWordWrap(true);
    dataLayout->addWidget(dataInfo);
    auto* dataButtons = new QGridLayout;
    dataButtons->setSpacing(12);
    auto* backupBtn = new QPushButton(QStringLiteral("创建备份"));
    auto* restoreBtn = new QPushButton(QStringLiteral("从备份恢复"));
    auto* exportBtn = new QPushButton(QStringLiteral("导出专注记录"));
    auto* openDataBtn = new QPushButton(QStringLiteral("打开数据目录"));
    backupBtn->setProperty("testId", "settingsCreateBackup");
    restoreBtn->setProperty("testId", "settingsRestoreBackup");
    exportBtn->setProperty("testId", "settingsExportRecords");
    openDataBtn->setProperty("testId", "settingsOpenData");
    DialogPresenter::setPrimary(backupBtn);
    DialogPresenter::setDanger(restoreBtn);
    DialogPresenter::setSecondary(exportBtn);
    DialogPresenter::setSecondary(openDataBtn);
    for (auto* button : {backupBtn, restoreBtn, exportBtn, openDataBtn}) {
        button->setMinimumHeight(44);
        button->setCursor(Qt::PointingHandCursor);
    }
    backupBtn->setToolTip(QStringLiteral("选择目录并创建完整本地数据备份"));
    restoreBtn->setToolTip(QStringLiteral("选择经过校验的备份，重启后执行恢复"));
    exportBtn->setToolTip(QStringLiteral("导出专注记录，不包含账户密码信息"));
    openDataBtn->setToolTip(QStringLiteral("在文件管理器中查看当前数据目录"));
    dataButtons->addWidget(backupBtn, 0, 0);
    dataButtons->addWidget(exportBtn, 0, 1);
    dataButtons->addWidget(openDataBtn, 1, 0);
    dataButtons->setColumnStretch(0, 1);
    dataButtons->setColumnStretch(1, 1);
    dataLayout->addLayout(dataButtons);
    auto* diagnosticLogStatus = new QLabel(dataGroup);
    diagnosticLogStatus->setObjectName("diagnosticLogStatus");
    diagnosticLogStatus->setWordWrap(true);
    diagnosticLogStatus->setTextFormat(Qt::PlainText);
    dataLayout->addWidget(diagnosticLogStatus);
    auto* restoreSection = new QWidget(dataGroup);
    restoreSection->setObjectName("settingsRestoreSection");
    auto* restoreLayout = new QVBoxLayout(restoreSection);
    restoreLayout->setContentsMargins(0, 20, 0, 0);
    restoreLayout->setSpacing(10);
    auto* restoreHeading = new QLabel(QStringLiteral("恢复数据"), restoreSection);
    restoreHeading->setProperty("settingTitle", true);
    auto* restoreDescription = new QLabel(
        QStringLiteral("恢复将替换当前数据，重启后生效。执行前会自动创建安全快照。"), restoreSection);
    restoreDescription->setProperty("settingDescription", true);
    restoreDescription->setWordWrap(true);
    restoreLayout->addWidget(restoreHeading);
    restoreLayout->addWidget(restoreDescription);
    restoreLayout->addWidget(restoreBtn, 0, Qt::AlignLeft);
    dataLayout->addWidget(restoreSection);
    const auto refreshDiagnosticLogStatus = [diagnosticLogStatus]() {
        const Logger::RuntimeStatus status = Logger::instance().runtimeStatus();
        using State = Logger::RuntimeStatus::State;
        if (status.state == State::Writing) {
            diagnosticLogStatus->setText(QStringLiteral("诊断日志状态：正常。日志正在写入。"));
            return;
        }
        if (status.state == State::FailedStopped) {
            QString reason = QStringLiteral("日志操作失败");
            if (status.failureReason == "empty log path") reason = QStringLiteral("日志路径未配置");
            else if (status.failureReason == "open failed") reason = QStringLiteral("无法打开日志文件");
            else if (status.failureReason == "rotation failed") reason = QStringLiteral("日志轮转失败");
            else if (status.failureReason == "write failed") reason = QStringLiteral("日志写入失败");
            else if (status.failureReason == "flush failed") reason = QStringLiteral("日志刷新失败");
            else if (status.failureReason == "close failed") reason = QStringLiteral("日志关闭失败");
            diagnosticLogStatus->setText(
                QStringLiteral("诊断日志状态：故障停写（%1）。请退出应用并检查日志目录权限、文件占用及可用空间，再重新启动。")
                    .arg(reason));
            return;
        }
        diagnosticLogStatus->setText(
            QStringLiteral("诊断日志状态：未开启。请重新启动应用以初始化诊断日志。"));
    };
    auto* diagnosticLogStatusTimer = new QTimer(page5);
    diagnosticLogStatusTimer->setInterval(1000);
    const auto syncDiagnosticLogStatusTimer = [this, page5, settingsStack, diagnosticLogStatusTimer,
                                               refreshDiagnosticLogStatus]() {
        const bool dataSettingsSelected = stackedWidget_->currentWidget() == page5 &&
                                          settingsStack->currentIndex() == 3;
        if (!dataSettingsSelected) {
            diagnosticLogStatusTimer->stop();
            return;
        }
        if (page5->isVisible()) refreshDiagnosticLogStatus();
        if (!diagnosticLogStatusTimer->isActive()) diagnosticLogStatusTimer->start();
    };
    QObject::connect(settingsStack, &QStackedWidget::currentChanged, this,
                     [syncDiagnosticLogStatusTimer](int) { syncDiagnosticLogStatusTimer(); });
    QObject::connect(stackedWidget_, &QStackedWidget::currentChanged, this,
                     [syncDiagnosticLogStatusTimer](int) { syncDiagnosticLogStatusTimer(); });
    QObject::connect(diagnosticLogStatusTimer, &QTimer::timeout, this,
                     [this, page5, settingsStack, diagnosticLogStatusTimer,
                      refreshDiagnosticLogStatus]() {
        if (stackedWidget_->currentWidget() == page5 && settingsStack->currentIndex() == 3 &&
            page5->isVisible()) {
            refreshDiagnosticLogStatus();
        } else if (stackedWidget_->currentWidget() != page5 || settingsStack->currentIndex() != 3) {
            diagnosticLogStatusTimer->stop();
        }
    });
    syncDiagnosticLogStatusTimer();
    dataSettingsLayout->addWidget(dataGroup, 0, Qt::AlignTop);
    QObject::connect(backupBtn, &QPushButton::clicked, this, [this]() {
        const QString root = DialogPresenter::chooseDirectory(this, QStringLiteral("选择备份保存位置"));
        if (root.isEmpty()) return;
        QString backupDir, error;
        if (!featureServices_.saveForBackup(controller_, coinManager_, &error)) {
            DialogPresenter::warning(this, QStringLiteral("暂时无法备份"), error);
            return;
        }
        if (BackupService::createBackup(PathConfig::getAppDataDir(), root, &backupDir, &error)) {
            DialogPresenter::information(this, QStringLiteral("备份完成"), QStringLiteral("备份已创建：\n%1").arg(backupDir));
        } else {
            DialogPresenter::warning(this, QStringLiteral("备份失败"), error);
        }
    });
    QObject::connect(restoreBtn, &QPushButton::clicked, this, [this]() {
        const QString backupDir = DialogPresenter::chooseDirectory(this, QStringLiteral("选择 Forest 备份目录"));
        if (backupDir.isEmpty()) return;
        QString error;
        if (BackupService::requestRestore(PathConfig::getAppDataDir(), backupDir, &error)) {
            DialogPresenter::information(this, QStringLiteral("恢复已安排"), QStringLiteral("重启 Forest 后将执行恢复。恢复前会自动创建安全快照。"));
        } else {
            DialogPresenter::warning(this, QStringLiteral("恢复请求失败"), error);
        }
    });
    QObject::connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QString path = DialogPresenter::chooseSaveFile(this, QStringLiteral("导出专注记录"),
            QDir::home().filePath(QStringLiteral("Forest-focus-records.csv")), QStringLiteral("CSV 文件 (*.csv)"));
        if (path.isEmpty()) return;
        QString error;
        if (!BackupService::exportFocusCsv(db_, path, &error)) {
            DialogPresenter::warning(this, QStringLiteral("导出失败"), error);
        }
    });
    QObject::connect(openDataBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(PathConfig::getAppDataDir()));
    });

    settingsOathInput_ = new QLineEdit;
    settingsOathInput_->setPlaceholderText(QStringLiteral("写下一句专注心愿..."));
    addSettingRow(s3Layout, QStringLiteral("树梢誓言"), QStringLiteral("显示在主页的一句专注心愿。"), settingsOathInput_);

    auto* blGroup = new QWidget;
    blGroup->setObjectName("settingsRuleEditor");
    auto* blLayout = new QVBoxLayout(blGroup);
    blLayout->setContentsMargins(0, 6, 0, 16);
    blLayout->setSpacing(16);
    auto* blacklistTitle = new QLabel(QStringLiteral("进程黑名单"), blGroup);
    blacklistTitle->setProperty("settingTitle", true);
    blLayout->addWidget(blacklistTitle);
    settingsBlacklistWidget_ = new QListWidget;
    settingsBlacklistWidget_->setObjectName("processBlacklist");
    settingsBlacklistWidget_->setFixedHeight(224);
    settingsBlacklistWidget_->setAccessibleName(QStringLiteral("深度专注进程黑名单"));
    for (const QString& name : ruleEngine_.blacklist()) {
        settingsBlacklistWidget_->addItem(name);
    }
    blLayout->addWidget(settingsBlacklistWidget_);
    auto* inputLayout = new QHBoxLayout;
    settingsBlacklistInput_ = new QLineEdit;
    settingsBlacklistInput_->setObjectName("processBlacklistInput");
    settingsBlacklistInput_->setPlaceholderText(QStringLiteral("输入进程名，如 chrome.exe"));
    auto* addBtn = new QPushButton(QStringLiteral("添加"));
    auto* removeBtn = new QPushButton(QStringLiteral("移除"));
    addBtn->setObjectName("processBlacklistAdd");
    removeBtn->setObjectName("processBlacklistRemove");
    removeBtn->setEnabled(false);
    removeBtn->setToolTip(QStringLiteral("先选择需要移除的应用"));
    addBtn->setToolTip(QStringLiteral("将输入的进程名加入监督规则"));
    for (auto* button : {addBtn, removeBtn}) {
        button->setMinimumSize(92, 44);
    }
    QObject::connect(settingsBlacklistWidget_, &QListWidget::currentRowChanged, removeBtn,
                     [removeBtn](int row) { removeBtn->setEnabled(row >= 0); });
    addBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setCursor(Qt::PointingHandCursor);
    inputLayout->addWidget(settingsBlacklistInput_);
    inputLayout->addWidget(addBtn);
    inputLayout->addWidget(removeBtn);
    blLayout->addLayout(inputLayout);
    rulesSettingsLayout->addWidget(blGroup, 0, Qt::AlignTop);

    QObject::connect(addBtn, &QPushButton::clicked, this, [this]() {
        QString name = settingsBlacklistInput_->text().trimmed();
        if (name.isEmpty()) return;
        auto& preferences = UserPreferences::instance();
        QStringList next = preferences.processBlacklist();
        next.append(name);
        if (!preferences.setProcessBlacklist(next)) {
            DialogPresenter::error(this, QStringLiteral("规则未保存"), preferences.lastError());
            return;
        }
        ruleEngine_.setBlacklist(preferences.processBlacklist());
        settingsBlacklistWidget_->clear();
        settingsBlacklistWidget_->addItems(ruleEngine_.blacklist());
        settingsBlacklistInput_->clear();
    });
    QObject::connect(removeBtn, &QPushButton::clicked, this, [this]() {
        auto* item = settingsBlacklistWidget_->currentItem();
        if (!item) return;
        auto& preferences = UserPreferences::instance();
        QStringList next = preferences.processBlacklist();
        next.removeAll(item->text());
        if (!preferences.setProcessBlacklist(next)) {
            DialogPresenter::error(this, QStringLiteral("规则未保存"), preferences.lastError());
            return;
        }
        ruleEngine_.setBlacklist(preferences.processBlacklist());
        settingsBlacklistWidget_->clear();
        settingsBlacklistWidget_->addItems(ruleEngine_.blacklist());
    });

    focusSettingsLayout->addStretch();
    rulesSettingsLayout->addStretch();
    appearanceSettingsLayout->addStretch();
    dataSettingsLayout->addStretch();

    stackedWidget_->addWidget(page5);

    // ========== Page 4: 成就列表 ==========
    stackedWidget_->addWidget(achievementsPage_->page());

    // ========== Page 5: 异色发掘 ==========
    gachaPage_ = std::make_unique<GachaPageController>(coinManager_, *m_gacha, focusResults_, this);
    QObject::connect(gachaPage_.get(), &GachaPageController::achievementsChanged,
                     this, &MainWindow::refreshAchievementsPage);
    stackedWidget_->addWidget(gachaPage_->page());

    // ========== Page 6: 用户 ==========
    stackedWidget_->addWidget(friendsPage_->page());

    // ========== Page 7: 专注挑战 ==========
    stackedWidget_->addWidget(DashboardPageHost::create(
        challengeDashboard_, QStringLiteral("challengePage"),
        QStringLiteral("QWidget#challengePage { background:%1; }").arg(AppStyle::color(AppStyle::Color::Workspace).name()),
        true, stackedWidget_));

    // ========== Page 8: 时间守护 ==========
    guardianDashboard_ = new GuardianDashboardWidget;
    stackedWidget_->addWidget(DashboardPageHost::create(
        guardianDashboard_, QStringLiteral("guardianPage"), QString(), false, stackedWidget_));

    QObject::connect(guardianDashboard_, &GuardianDashboardWidget::dailyGoalChanged, this, [this](int v) {
        if (!m_guardian) return;
        if (!m_guardian->setDailyGoalMinutes(static_cast<uint32_t>(v))) {
            DialogPresenter::error(this, QStringLiteral("目标未保存"), m_guardian->lastError());
        }
        refreshGuardianPage();
    });

    // ========== 信号连接 ==========
    QObject::connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    QObject::connect(pauseBtn_, &QPushButton::clicked, this, &MainWindow::onPauseResumeClicked);
    QObject::connect(abandonBtn_, &QPushButton::clicked, this, &MainWindow::onAbandonClicked);

    QObject::connect(&controller_, &FocusController::sig_tick,
        this, [this](uint32_t ds, bool sw) {
            timerRing_->setDisplaySeconds(ds, sw);
            updateTrayStatus();
        });

    QObject::connect(&monitor_, &SystemMonitor::sig_violationDetected,
        &controller_, &FocusController::handleViolationDetected);
    QObject::connect(&monitor_, &SystemMonitor::sig_safeWindowDetected,
        &controller_, &FocusController::handleSafeWindowDetected);

    QObject::connect(&controller_, &FocusController::sig_softViolation,
        this, [this](const QString&) {
            trayIcon_->showMessage(QStringLiteral("专注监督系统"),
                QStringLiteral("检测到分心行为，本次专注收益已减少。"),
                QSystemTrayIcon::Information, 5000);
        });

    QObject::connect(&controller_, &FocusController::sig_strictWarningTick,
        this, [this](uint32_t r) {
            if (!warningOverlay_) return;
            if (r == 0) { warningOverlay_->hide(); return; }
            warningLabel_->setText(
                QStringLiteral("违规！%1 秒内切回，否则小树枯萎").arg(r));
            if (centralWidget()) warningOverlay_->setGeometry(centralWidget()->rect());
            warningOverlay_->show(); warningOverlay_->raise();
        });

    QObject::connect(&controller_, &FocusController::sig_stateChanged,
        this, [this](FocusController::State s) {
            if (s == FocusController::State::RUNNING) {
                monitor_.startMonitoring();
                quoteTimer_.start(10000);
            }
            if (s != FocusController::State::WARNING && warningOverlay_) warningOverlay_->hide();
            if (s != FocusController::State::PAUSED) hidePauseBreakOverlay();
            updateUI();
        });

    QObject::connect(focusSession_.get(), &FocusSessionCoordinator::terminalResultReady,
        this, [this](uint32_t recordId, uint32_t status, const FocusResultService::Outcome& outcome) {
            handleFocusFinalized(recordId, status, outcome);
        });
    QObject::connect(&controller_, &FocusController::sig_growthStageChanged,
        this, [this](uint32_t) { updateUI(); });
    QObject::connect(&controller_, &FocusController::sig_finalizationSaveFailed,
        this, [this](const QString& error) {
            monitor_.stopMonitoring();
            quoteTimer_.stop();
            DialogPresenter::error(this, QStringLiteral("专注结果未能保存"),
                QStringLiteral("计时已停止，本次结束决定不会改判。请检查数据目录，然后点击“重试保存”。保存成功前不能开始新专注或正常退出。\n\n%1").arg(error));
        });

    QObject::connect(&quoteTimer_, &QTimer::timeout, this, [this]() {
        if (!UserPreferences::instance().accessibilityOptions().reducedMotion) {
            timerRing_->setQuote(quotes_.getRandomQuote());
        }
    });

    QObject::connect(&achievements_, &AchievementEngine::sig_achievementUnlocked,
        this, [this](uint32_t, const QString&, const QString&) {
            refreshAchievementsPage();
        });

    QObject::connect(&coinManager_, &CoinManager::sig_balanceChanged,
        this, [this](uint32_t balance) {
            if (coinLabel_) coinLabel_->setText(QStringLiteral("🪙 %1").arg(balance));
            if (sidebarNavigation_) sidebarNavigation_->setCoinBalance(balance);
        });

    warningOverlay_ = new QWidget(this);
    warningOverlay_->setObjectName("warningOverlay");
    warningOverlay_->setStyleSheet("background-color: rgba(180,40,40,220);");
    warningOverlay_->setGeometry(0, 0, width(), height());
    warningLabel_ = new QLabel(warningOverlay_);
    warningLabel_->setAlignment(Qt::AlignCenter);
    warningLabel_->setStyleSheet("font-size:22px; font-weight:bold; color:#D8B257; background:transparent;");
    warningLabel_->setGeometry(0, height()/3, width(), 100);
    warningOverlay_->hide();

    switchPage(0);
    setupTrayIcon();
    applyAccessibilityPreferences();
    auto* startShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+F")), this);
    QObject::connect(startShortcut, &QShortcut::activated, this, [this]() {
        if (FocusPageState::from(controller_).idle || controller_.hasPendingFinalization()) onStartClicked();
        else if (controller_.currentState() == FocusController::State::PAUSED) resumeFromPauseBreak();
    });
    auto* pauseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+P")), this);
    QObject::connect(pauseShortcut, &QShortcut::activated, this, [this]() {
        if (controller_.currentState() == FocusController::State::RUNNING && controller_.allowPause()) {
            onPauseResumeClicked();
        }
    });
    auto* abandonShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+A")), this);
    QObject::connect(abandonShortcut, &QShortcut::activated, this, [this]() {
        const auto state = FocusPageState::from(controller_);
        if (state.active && !state.saveFailed) onAbandonClicked();
    });
    restoreFocusSetup();
    refreshGarden();
    updateUI();
}

void MainWindow::initNavigationLayout()
{
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    sidebarWidget_ = new QWidget(centralWidget);
    sidebarWidget_->setFixedWidth(180);
    sidebarWidget_->setObjectName("sidebarWidget");
    sidebarWidget_->setStyleSheet(
        "QWidget#sidebarWidget { background-color: #143F36; border-right:none; }"
        "QPushButton[nav=\"true\"] { background:transparent; border:none; border-radius:0;"
        "  border-left:3px solid transparent; text-align:left; padding:12px 16px;"
        "  font-size:14px; font-weight:600; color:#C6DAD2; }"
        "QPushButton[nav=\"true\"]:hover { background-color:#215648; color:#FFFFFF; }"
        "QPushButton[nav=\"true\"][active=\"true\"] { background-color:#295F4F; color:#FFFFFF;"
        "  border-left:3px solid #A2D8C1; }"
        "QPushButton#btnToggle { background:transparent; border:none; text-align:left;"
        "  padding:16px; font-weight:bold; color:#F7FFF7; font-size:15px; }"
        "QPushButton#btnToggle:hover { color:#F2F4C6; }");

    auto* sidebarLayout = new QVBoxLayout(sidebarWidget_);
    sidebarLayout->setContentsMargins(0, 10, 0, 10);
    sidebarLayout->setSpacing(6);

    btnToggle_ = new QPushButton(QStringLiteral("  ☰  收起菜单"), sidebarWidget_);
    btnToggle_->setObjectName("btnToggle");
    sidebarLayout->addWidget(btnToggle_);

    sidebarCoinLabel_ = new QPushButton(QStringLiteral("  🪙 %1").arg(coinManager_.balance()), sidebarWidget_);
    sidebarCoinLabel_->setStyleSheet(
        "QPushButton { background-color:rgba(242,244,198,24); border:none;"
        "border-radius:8px; margin:0 10px 8px 10px; padding:7px; color:#F7FFF7;"
        "font-weight:bold; text-align:left; }"
        "QPushButton:hover { background-color:rgba(242,244,198,48); color:#FFFFFF; }");
    sidebarCoinLabel_->setCursor(Qt::PointingHandCursor);
    sidebarLayout->addWidget(sidebarCoinLabel_);

    btnHome_ = new QPushButton(QStringLiteral("  💻 专注主页"), sidebarWidget_);
    btnForest_ = new QPushButton(QStringLiteral("  🌳 我的森林"), sidebarWidget_);
    btnShop_ = new QPushButton(QStringLiteral("  🪙 植物商城"), sidebarWidget_);
    btnAchievements_ = new QPushButton(QStringLiteral("  🏆 成就列表"), sidebarWidget_);
    btnGacha_ = new QPushButton(QStringLiteral("  ✨ 异色发掘"), sidebarWidget_);
    btnFriends_ = new QPushButton(QStringLiteral("  👥 用户"), sidebarWidget_);
    btnFriends_->setObjectName("navFriends");
    btnFriends_->setProperty("nav", "true");
    btnFriends_->setMinimumHeight(42);
    btnFriends_->setCursor(Qt::PointingHandCursor);
    btnChallenges_ = new QPushButton(QStringLiteral("  🏆 专注挑战"), sidebarWidget_);
    btnChallenges_->setObjectName("navChallenges");
    btnChallenges_->setProperty("nav", "true");
    btnChallenges_->setMinimumHeight(42);
    btnChallenges_->setCursor(Qt::PointingHandCursor);
    btnGuardian_ = new QPushButton(QStringLiteral("  ⏰ 时间守护"), sidebarWidget_);
    btnGuardian_->setObjectName("navGuardian");
    btnGuardian_->setProperty("nav", "true");
    btnGuardian_->setMinimumHeight(42);
    btnGuardian_->setCursor(Qt::PointingHandCursor);
    btnSettings_ = new QPushButton(QStringLiteral("  ⚙️ 系统设置"), sidebarWidget_);

    btnHome_->setObjectName("navHome");
    btnHome_->setProperty("nav", "true");
    btnForest_->setObjectName("navForest");
    btnForest_->setProperty("nav", "true");
    btnShop_->setObjectName("navShop");
    btnShop_->setProperty("nav", "true");
    btnAchievements_->setObjectName("navAchievements");
    btnAchievements_->setProperty("nav", "true");
    btnGacha_->setObjectName("navGacha");
    btnGacha_->setProperty("nav", "true");
    btnSettings_->setObjectName("navSettings");
    btnSettings_->setProperty("nav", "true");
    btnHome_->setMinimumHeight(42);
    btnForest_->setMinimumHeight(42);
    btnShop_->setMinimumHeight(42);
    btnAchievements_->setMinimumHeight(42);
    btnGacha_->setMinimumHeight(42);
    btnSettings_->setMinimumHeight(42);
    btnToggle_->setCursor(Qt::PointingHandCursor);
    btnHome_->setCursor(Qt::PointingHandCursor);
    btnForest_->setCursor(Qt::PointingHandCursor);
    btnShop_->setCursor(Qt::PointingHandCursor);
    btnAchievements_->setCursor(Qt::PointingHandCursor);
    btnGacha_->setCursor(Qt::PointingHandCursor);
    btnSettings_->setCursor(Qt::PointingHandCursor);

    sidebarLayout->addWidget(btnHome_);
    sidebarLayout->addWidget(btnForest_);
    sidebarLayout->addWidget(btnShop_);
    sidebarLayout->addWidget(btnAchievements_);
    sidebarLayout->addWidget(btnGacha_);
    sidebarLayout->addWidget(btnFriends_);
    sidebarLayout->addWidget(btnChallenges_);
    sidebarLayout->addWidget(btnGuardian_);
    sidebarLayout->addWidget(btnSettings_);
    sidebarLayout->addStretch();

    mainLayout->addWidget(sidebarWidget_);

    stackedWidget_ = new QStackedWidget(centralWidget);
    stackedWidget_->setObjectName("mainPageStack");
    stackedWidget_->setStyleSheet("QStackedWidget { background-color: #58AD8F; }");
    mainLayout->addWidget(stackedWidget_);

    sidebarNavigation_ = std::make_unique<SidebarNavigation>(
        sidebarWidget_, stackedWidget_, btnToggle_, sidebarCoinLabel_, QVector<SidebarNavigation::Item>{
            {btnHome_, 0, QStringLiteral("  💻 专注主页"), QStringLiteral("  💻")},
            {btnForest_, 1, QStringLiteral("  🌳 我的森林"), QStringLiteral("  🌳")},
            {btnShop_, 2, QStringLiteral("  🪙 植物商城"), QStringLiteral("  🪙")},
            {btnAchievements_, 4, QStringLiteral("  🏆 成就列表"), QStringLiteral("  🏆")},
            {btnGacha_, 5, QStringLiteral("  ✨ 异色发掘"), QStringLiteral("  ✨")},
            {btnFriends_, 6, QStringLiteral("  👥 用户"), QStringLiteral("  👥")},
            {btnChallenges_, 7, QStringLiteral("  🏆 专注挑战"), QStringLiteral("  🏆")},
            {btnGuardian_, 8, QStringLiteral("  ⏰ 时间守护"), QStringLiteral("  ⏰")},
            {btnSettings_, 3, QStringLiteral("  ⚙️ 系统设置"), QStringLiteral("  ⚙️")},
        });

    QObject::connect(btnToggle_, &QPushButton::clicked, this, [this]() {
        sidebarNavigation_->toggle(coinManager_.balance());
    });
    QObject::connect(btnHome_, &QPushButton::clicked, this, [this](){ switchPage(0); });
    QObject::connect(btnForest_, &QPushButton::clicked, this, [this](){ switchPage(1); });
    QObject::connect(btnShop_, &QPushButton::clicked, this, [this](){ switchPage(2); });
    QObject::connect(sidebarCoinLabel_, &QPushButton::clicked, this, [this](){
        DialogPresenter::detail(this, QStringLiteral("金币详情"),
            QStringLiteral("当前金币：%1\n金币可通过完成专注、成就、签到和挑战奖励获得。下面将打开植物商城。")
                .arg(coinManager_.balance()));
        switchPage(2);
    });
    QObject::connect(btnGacha_, &QPushButton::clicked, this, &MainWindow::onOpenGacha);
    QObject::connect(btnFriends_, &QPushButton::clicked, this, &MainWindow::onOpenFriends);
    QObject::connect(btnChallenges_, &QPushButton::clicked, this, &MainWindow::onOpenChallenges);
    QObject::connect(btnGuardian_, &QPushButton::clicked, this, &MainWindow::onOpenGuardian);
    QObject::connect(btnAchievements_, &QPushButton::clicked, this, &MainWindow::onOpenAchievements);
    QObject::connect(btnSettings_, &QPushButton::clicked, this, [this](){ switchPage(3); });

}

void MainWindow::switchPage(int index)
{
    if (!sidebarNavigation_ || !sidebarNavigation_->switchTo(index, [this](int activePage) {
        if (activePage == 1) refreshGarden();
        if (activePage == 3) refreshPlantCombo();
        if (activePage == 2) refreshShopPage();
        if (activePage == 4) refreshAchievementsPage();
        if (activePage == 5) refreshGachaPage();
        if (activePage == 6) {
            if (friendsPage_) friendsPage_->refresh();
        }
        if (activePage == 7) refreshChallengePage();
        if (activePage == 8) refreshGuardianPage();
    })) {
        DialogPresenter::warning(this, QStringLiteral("页面不可用"), QStringLiteral("该功能页面暂不可用。"));
    }
}

void MainWindow::refreshPlantCombo()
{
    if (!settingsPlantCombo_) return;

    uint32_t currentType = settingsPlantCombo_->currentData().toUInt();
    settingsPlantCombo_->blockSignals(true);
    settingsPlantCombo_->clear();

    int newIndex = 0;
    for (const auto& plant : PlantCatalog::all()) {
        if (coinManager_.isPlantUnlocked(plant.type)) {
            settingsPlantCombo_->addItem(QStringLiteral("%1 (%2)").arg(plant.displayName, plant.internalName), plant.type);
            if (plant.type == currentType) {
                newIndex = settingsPlantCombo_->count() - 1;
            }
        }
    }
    settingsPlantCombo_->setCurrentIndex(newIndex);
    settingsPlantCombo_->blockSignals(false);
}

void MainWindow::refreshShopPage()
{
    shopPage_->refresh();
}

void MainWindow::refreshAchievementsPage()
{
    achievementsPage_->refresh();
}

void MainWindow::refreshGachaPage()
{
    if (gachaPage_) gachaPage_->refresh();
}

void MainWindow::refreshFriendBrowsePage(const QString& filter)
{
    if (friendsPage_) friendsPage_->refresh(filter);
}

void MainWindow::refreshFriendRequestsPage()
{
    if (friendsPage_) friendsPage_->refresh();
}

void MainWindow::refreshFriendsPage()
{
    if (friendsPage_) friendsPage_->refresh();
}

void MainWindow::refreshChallengePage()
{
    if (!challengeDashboard_) return;
    const auto snapshot = dashboardSnapshots_.challenges();
    challengeDashboard_->setEnabled(snapshot.error.isEmpty());
    challengeDashboard_->setToolTip(snapshot.error.isEmpty() ? QString() : QStringLiteral("读取失败，重新进入此页面可重试。"));
    if (!snapshot.error.isEmpty()) {
        if (stackedWidget_ && stackedWidget_->currentIndex() == 7 && challengeReadError_ != snapshot.error) {
            challengeReadError_ = snapshot.error;
            DialogPresenter::error(this, QStringLiteral("挑战数据读取失败"),
                QStringLiteral("原进度已保留，未将错误当作零进度。请检查数据目录后重新进入页面。\n%1").arg(snapshot.error));
        }
        return;
    }
    challengeReadError_.clear();
    challengeDashboard_->setSnapshot(
        snapshot.coinBalance,
        snapshot.challenges,
        snapshot.records);
}

void MainWindow::refreshTagOptions()
{
    if (!m_tagMgr) return;
    const uint32_t currentId = settingsTagCombo_ ? settingsTagCombo_->currentData().toUInt() : 0;

    if (settingsTagCombo_) {
        settingsTagCombo_->blockSignals(true);
        settingsTagCombo_->clear();
        settingsTagCombo_->addItem(QStringLiteral("无标签"), 0);
        int sel = 0;
        for (const auto& t : m_tagMgr->all()) {
            settingsTagCombo_->addItem(t.name, t.id);
            if (t.id == currentId) sel = settingsTagCombo_->count() - 1;
        }
        settingsTagCombo_->setCurrentIndex(sel);
        settingsTagCombo_->blockSignals(false);
    }
    refreshHomeTagBadge();
}

void MainWindow::setSelectedTagId(uint32_t tagId)
{
    if (!settingsTagCombo_) return;
    int index = settingsTagCombo_->findData(tagId);
    if (index < 0) index = 0;
    if (settingsTagCombo_->currentIndex() != index) {
        settingsTagCombo_->setCurrentIndex(index);
    } else {
        refreshHomeTagBadge();
    }
}

void MainWindow::refreshHomeTagBadge()
{
    if (!timerRing_) return;

    uint32_t tagId = settingsTagCombo_ ? settingsTagCombo_->currentData().toUInt() : 0;
    QString tagName = QStringLiteral("无标签");
    QColor tagColor("#B8B6EA");
    if (tagId != 0 && m_tagMgr) {
        const auto tag = m_tagMgr->tag(tagId);
        tagName = tag.name.trimmed().isEmpty() ? QStringLiteral("无标签") : tag.name.trimmed();
        QColor parsed(tag.color);
        if (parsed.isValid()) tagColor = parsed;
    }
    timerRing_->setTagInfo(tagName, tagColor);
}

void MainWindow::showHomePlantSelector()
{
    if (controller_.hasPendingFinalization() || controller_.currentState() == FocusController::State::RUNNING ||
        controller_.currentState() == FocusController::State::PAUSED ||
        controller_.currentState() == FocusController::State::WARNING) {
        return;
    }
    refreshPlantCombo();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("选择植物"));
    DialogPresenter::prepare(dialog);
    dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(
        "QDialog#forestDialog { background:#FFFFFF; border:1px solid #D5DFDA; border-radius:20px; }"
        "QDialog#forestDialog QLabel#selectorTitle { color:#203C35; font-size:20px; font-weight:800; }"
        "QDialog#forestDialog QLabel#selectorDescription { color:#53665F; font-size:14px; background:transparent; }"
        "QDialog#forestDialog QPushButton[plantCard=\"true\"] { background:#F5F7F8;"
        " border:1px solid #D5DFDA; border-radius:8px; padding:12px;"
        " color:#203C35; font-weight:800; text-align:center; }"
        "QDialog#forestDialog QPushButton[plantCard=\"true\"]:hover { background:#EFF5F2; border-color:#246B57; }"
        "QDialog#forestDialog QPushButton[plantCard=\"true\"]:pressed { background:#C9F0DE; }"
        "QDialog#forestDialog QPushButton[plantCard=\"true\"]:focus { border:2px solid #173F33; }"));

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 26, 28, 24);
    layout->setSpacing(20);
    auto* title = new QLabel(QStringLiteral("选择本次专注植物"), &dialog);
    title->setObjectName("selectorTitle");
    layout->addWidget(title);
    auto* description = new QLabel(QStringLiteral("选择已解锁植物，陪伴本次专注。"), &dialog);
    description->setObjectName("selectorDescription");
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* grid = new QGridLayout;
    grid->setSpacing(12);
    int unlockedCount = 0;
    for (const auto& plant : PlantCatalog::all()) {
        if (!coinManager_.isPlantUnlocked(plant.type)) continue;
        auto* btn = new QPushButton(plant.displayName, &dialog);
        btn->setProperty("plantCard", "true");
        btn->setAutoDefault(false);
        btn->setAccessibleName(plant.displayName);
        btn->setToolTip(plant.displayName);
        btn->setFocusPolicy(Qt::StrongFocus);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(QIcon(PlantImageUtils::loadPlantIcon(plant.iconPath)));
        btn->setIconSize(QSize(76, 76));
        btn->setMinimumSize(168, 116);
        grid->addWidget(btn, unlockedCount / 3, unlockedCount % 3);
        QObject::connect(btn, &QPushButton::clicked, &dialog, [this, &dialog, type = plant.type]() {
            if (settingsPlantCombo_) {
                for (int i = 0; i < settingsPlantCombo_->count(); ++i) {
                    if (settingsPlantCombo_->itemData(i).toUInt() == type) {
                        settingsPlantCombo_->setCurrentIndex(i);
                        break;
                    }
                }
            }
            if (timerRing_) timerRing_->setPlantType(type);
            dialog.accept();
        });
        ++unlockedCount;
    }

    if (unlockedCount == 0) {
        auto* empty = new QLabel(QStringLiteral("暂无已解锁植物"), &dialog);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet("color:#245543; font-weight:800;");
        layout->addWidget(empty);
    } else {
        layout->addLayout(grid);
    }

    auto* close = new QPushButton(QStringLiteral("取消"), &dialog);
    DialogPresenter::setSecondary(close);
    close->setDefault(true);
    close->setFocus(Qt::OtherFocusReason);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    layout->addWidget(close, 0, Qt::AlignRight);
    dialog.exec();
}

void MainWindow::refreshGuardianPage()
{
    if (!guardianDashboard_) return;
    const auto snapshot = dashboardSnapshots_.guardian();
    guardianDashboard_->setSnapshot(snapshot.todayMinutes,
                                    snapshot.dailyGoalMinutes,
                                    snapshot.currentStreak,
                                    snapshot.longestStreak,
                                    snapshot.totalMinutes);
}

void MainWindow::setHomeTimerMode(bool stopwatch)
{
    if (!settingsModeCombo_) return;
    int targetIndex = stopwatch ? 1 : 0;
    if (settingsModeCombo_->currentIndex() != targetIndex) {
        settingsModeCombo_->setCurrentIndex(targetIndex);
    }
    refreshHomeModeControls();
}

void MainWindow::showHomeModeOptions(bool stopwatch)
{
    setHomeTimerMode(stopwatch);
    if (!homeModeOverlay_) return;

    QWidget* host = centralWidget();
    if (host) {
        homeModeOverlay_->setGeometry(host->rect());
    }
    homeModeOverlay_->show();
    homeModeOverlay_->raise();
}

void MainWindow::hideHomeModeOptions()
{
    if (homeModeOverlay_) {
        homeModeOverlay_->hide();
    }
}

void MainWindow::showPauseBreakOverlay()
{
    if (!pauseBreakOverlay_) return;
    pauseBreakRemainingSeconds_ = static_cast<int>(controller_.pauseRemainingSeconds());
    updatePauseBreakText();

    QWidget* host = centralWidget();
    if (host) {
        pauseBreakOverlay_->setGeometry(host->rect());
    }
    pauseBreakOverlay_->show();
    pauseBreakOverlay_->raise();
    pauseBreakTimer_.start(1000);
}

void MainWindow::hidePauseBreakOverlay()
{
    pauseBreakTimer_.stop();
    if (pauseBreakOverlay_) {
        pauseBreakOverlay_->hide();
    }
}

void MainWindow::resumeFromPauseBreak()
{
    hidePauseBreakOverlay();
    if (controller_.currentState() != FocusController::State::PAUSED) {
        updateUI();
        return;
    }
    focusSession_->resume();
    updateUI();
}

void MainWindow::updatePauseBreakText()
{
    if (!pauseBreakTimeLabel_) return;
    const int seconds = qMax(0, pauseBreakRemainingSeconds_);
    pauseBreakTimeLabel_->setText(
        QStringLiteral("%1:%2")
            .arg(seconds / 60, 2, 10, QChar('0'))
            .arg(seconds % 60, 2, 10, QChar('0')));
}

void MainWindow::refreshHomeModeControls()
{
    if (!settingsModeCombo_ || !homeCountdownBtn_ || !homeStopwatchBtn_) return;

    bool stopwatch = settingsModeCombo_->currentData().toUInt() == 1;
    homeCountdownBtn_->setProperty("active", stopwatch ? "false" : "true");
    homeStopwatchBtn_->setProperty("active", stopwatch ? "true" : "false");
    homeCountdownBtn_->style()->unpolish(homeCountdownBtn_);
    homeCountdownBtn_->style()->polish(homeCountdownBtn_);
    homeStopwatchBtn_->style()->unpolish(homeStopwatchBtn_);
    homeStopwatchBtn_->style()->polish(homeStopwatchBtn_);

    if (countdownOptionsWidget_) {
        auto* groupRow = countdownOptionsWidget_->findChild<QWidget*>("groupPlantOptionRow");
        auto* autoExtendRow = countdownOptionsWidget_->findChild<QWidget*>("autoExtendOptionRow");
        if (groupRow) groupRow->setVisible(!stopwatch);
        if (autoExtendRow) autoExtendRow->setVisible(!stopwatch);
        countdownOptionsWidget_->setVisible(true);
        int targetHeight = countdownOptionsWidget_->sizeHint().height();

        auto* oldAnim = countdownOptionsWidget_->findChild<QPropertyAnimation*>("countdownOptionsHeightAnim");
        if (oldAnim) {
            oldAnim->stop();
            oldAnim->deleteLater();
        }

        auto* anim = new QPropertyAnimation(countdownOptionsWidget_, "maximumHeight", countdownOptionsWidget_);
        anim->setObjectName("countdownOptionsHeightAnim");
        anim->setDuration(180);
        anim->setStartValue(countdownOptionsWidget_->maximumHeight() == QWIDGETSIZE_MAX
            ? countdownOptionsWidget_->height()
            : countdownOptionsWidget_->maximumHeight());
        anim->setEndValue(targetHeight);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (homeDeepFocusCheck_ && settingsDeepFocusCheck_) {
        homeDeepFocusCheck_->setChecked(settingsDeepFocusCheck_->isChecked());
    }

    auto state = controller_.currentState();
    bool idle = (state == FocusController::State::IDLE ||
                 state == FocusController::State::SUCCESS ||
                 state == FocusController::State::FAILED);
    if (idle && timerRing_) timerRing_->setDisplaySeconds(0, stopwatch);
    if (!idle) hideHomeModeOptions();
}

void MainWindow::restoreFocusSetup()
{
    const auto setup = UserPreferences::instance().focusSetup();
    if (settingsModeCombo_) settingsModeCombo_->setCurrentIndex(setup.stopwatch ? 1 : 0);
    if (settingsPlantCombo_) {
        const int index = settingsPlantCombo_->findData(setup.plantType);
        if (index >= 0) settingsPlantCombo_->setCurrentIndex(index);
    }
    if (settingsTagCombo_) {
        const int index = settingsTagCombo_->findData(setup.tagId);
        if (index >= 0) settingsTagCombo_->setCurrentIndex(index);
    }
    if (settingsMinutesSpin_) settingsMinutesSpin_->setValue(static_cast<int>(setup.minutes));
    if (timerRing_) timerRing_->setSelectedMinutes(setup.minutes);
    if (settingsDeepFocusCheck_) settingsDeepFocusCheck_->setChecked(setup.deepFocus);
    if (homeDeepFocusCheck_) homeDeepFocusCheck_->setChecked(setup.deepFocus);
    if (homeAutoExtendCheck_) homeAutoExtendCheck_->setChecked(setup.autoExtend);
    refreshHomeModeControls();
    refreshHomeTagBadge();
}

void MainWindow::persistFocusSetup()
{
    UserPreferences::FocusSetup setup;
    setup.minutes = timerRing_ ? timerRing_->selectedMinutes() : 25;
    setup.plantType = settingsPlantCombo_ ? settingsPlantCombo_->currentData().toUInt() : 0;
    setup.tagId = settingsTagCombo_ ? settingsTagCombo_->currentData().toUInt() : 0;
    setup.stopwatch = settingsModeCombo_ && settingsModeCombo_->currentData().toUInt() == 1;
    setup.deepFocus = settingsDeepFocusCheck_ && settingsDeepFocusCheck_->isChecked();
    setup.autoExtend = homeAutoExtendCheck_ && homeAutoExtendCheck_->isChecked();
    UserPreferences::instance().setFocusSetup(setup);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        if (watched == homeModeOverlay_) {
            hideHomeModeOptions();
            return true;
        }
        if (watched == pauseBreakOverlay_) {
            resumeFromPauseBreak();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (controller_.hasPendingFinalization()) {
        closingFocus_ = true;
        const bool saved = controller_.retryPendingFinalization();
        closingFocus_ = false;
        if (saved) event->accept(); else event->ignore();
        return;
    }
    if (controller_.currentState() == FocusController::State::RUNNING ||
        controller_.currentState() == FocusController::State::PAUSED ||
        controller_.currentState() == FocusController::State::WARNING) {
        const bool confirmed = DialogPresenter::confirmDanger(this,
            QStringLiteral("确认退出"),
            QStringLiteral("正在专注中！退出将会导致小树枯萎。\n确定要退出吗？"));
        if (confirmed) {
            closingFocus_ = true;
            const bool saved = focusSession_->abandon();
            closingFocus_ = false;
            if (saved) event->accept(); else event->ignore();
        } else { event->ignore(); }
    } else { event->accept(); }
}

void MainWindow::onStartClicked()
{
    if (controller_.hasPendingFinalization()) {
        controller_.retryPendingFinalization();
        return;
    }
    if (!FocusPageState::from(controller_).idle) return;
    hideHomeModeOptions();

    bool isCountdown = settingsModeCombo_->currentData().toUInt() == 0;
    auto mode = !isCountdown
        ? FocusController::TimerMode::STOPWATCH : FocusController::TimerMode::COUNTDOWN;
    auto fm = settingsDeepFocusCheck_ && settingsDeepFocusCheck_->isChecked()
        ? FocusController::FocusMode::STRICT_MODE : FocusController::FocusMode::GENTLE_MODE;

    uint32_t minutes = timerRing_->selectedMinutes();
    FocusSessionCoordinator::StartRequest request;
    request.plantType = static_cast<uint32_t>(settingsPlantCombo_->currentData().toUInt());
    request.plannedMinutes = minutes;
    request.timerMode = mode;
    request.focusMode = fm;
    request.tagId = static_cast<uint32_t>(settingsTagCombo_->currentData().toUInt());
    request.allowPause = isCountdown && settingsAllowPauseCheck_ && settingsAllowPauseCheck_->isChecked();
    request.autoExtend = isCountdown && homeAutoExtendCheck_ && homeAutoExtendCheck_->isChecked();
    if (!focusSession_->start(request)) {
        if (controller_.hasPendingFinalization()) return;
        DialogPresenter::warning(this, QStringLiteral("无法开始专注"),
            QStringLiteral("专注记录未能安全创建，请稍后重试。"));
        return;
    }
    persistFocusSetup();
    timerRing_->setPlantType(
        static_cast<uint32_t>(settingsPlantCombo_->currentData().toUInt()));
    timerRing_->setDisplaySeconds(isCountdown ? minutes * 60 : 0, !isCountdown);
    monitor_.startMonitoring();
    quoteTimer_.start(10000);
    timerRing_->setOath(settingsOathInput_->text());
    switchPage(0);
    updateUI();
}

void MainWindow::onPauseResumeClicked()
{
    if (controller_.currentState() == FocusController::State::RUNNING) {
        focusSession_->pause();
        if (controller_.currentState() == FocusController::State::PAUSED) {
            monitor_.stopMonitoring();
            quoteTimer_.stop();
            showPauseBreakOverlay();
        }
    } else if (controller_.currentState() == FocusController::State::PAUSED) {
        resumeFromPauseBreak();
        return;
    }
    updateUI();
}

void MainWindow::onAbandonClicked()
{
    const uint32_t elapsedMinutes = (controller_.actualSeconds() + 59) / 60;
    const bool confirmed = DialogPresenter::confirmDanger(this, QStringLiteral("确认放弃"),
        QStringLiteral("本次已专注约 %1 分钟。放弃后会保留这条记录，并在森林中显示枯萎植物；本次不获得专注奖励。\n\n仍要放弃吗？")
            .arg(elapsedMinutes));
    if (!confirmed) return;
    focusSession_->abandon();
    monitor_.stopMonitoring(); quoteTimer_.stop();
    hidePauseBreakOverlay();
    updateUI(); refreshGarden();
}

void MainWindow::updateUI()
{
    const FocusPageState state = FocusPageState::from(controller_);
    if (focusHint_) {
        focusHint_->setText(state.saveFailed ? QStringLiteral("结果待保存")
            : state.paused ? QStringLiteral("暂停中")
            : state.running ? QStringLiteral("正在专注")
            : state.active ? QStringLiteral("返回安全应用以继续专注")
            : QStringLiteral("准备开始专注"));
    }
    if (coinLabel_) coinLabel_->setVisible(!state.active);
    startBtn_->setVisible(state.idle || state.saveFailed);
    startBtn_->setEnabled(state.idle || state.saveFailed);
    startBtn_->setText(state.saveFailed ? QStringLiteral("重试保存") : QStringLiteral("开始专注"));
    pauseBtn_->setVisible(state.canPause && !state.paused);
    abandonBtn_->setVisible(state.active && !state.paused && !state.saveFailed);
    pauseBtn_->setEnabled(state.canPause);
    abandonBtn_->setEnabled(state.active && !state.paused && !state.saveFailed);
    if (state.paused && pauseBreakOverlay_ && !pauseBreakOverlay_->isVisible()) {
        showPauseBreakOverlay();
    }
    if (homeCountdownBtn_) homeCountdownBtn_->setEnabled(state.idle);
    if (homeStopwatchBtn_) homeStopwatchBtn_->setEnabled(state.idle);
    if (homeAllowPauseCheck_) homeAllowPauseCheck_->setEnabled(state.idle);
    if (homeDeepFocusCheck_) homeDeepFocusCheck_->setEnabled(state.idle);
    if (homeGroupPlantCheck_) homeGroupPlantCheck_->setEnabled(false);
    if (homeAutoExtendCheck_) homeAutoExtendCheck_->setEnabled(state.idle);
    if (settingsAllowPauseCheck_) settingsAllowPauseCheck_->setEnabled(state.idle);
    if (sidebarWidget_) sidebarWidget_->setVisible(!state.active);

    if (state.idle) {
        const bool stopwatch = settingsModeCombo_ && settingsModeCombo_->currentData().toUInt() == 1;
        timerRing_->setDisplaySeconds(0, stopwatch);
        timerRing_->setOath(QString());
        timerRing_->setPlantType(settingsPlantCombo_->currentData().toUInt());
        refreshHomeModeControls();
    }
    timerRing_->setSessionState(state.active, controller_.growthStage());

    abandonBtn_->setText(QStringLiteral("放弃"));
    if (state.running) pauseBtn_->setText(QStringLiteral("暂停"));
    else if (state.paused) pauseBtn_->setText(QStringLiteral("继续"));

    updateTrayStatus();
}

void MainWindow::refreshGarden()
{
    auto snapshot = dashboardSnapshots_.forest();
    if (forestDashboard_) {
        forestDashboard_->setEnabled(snapshot.error.isEmpty());
        forestDashboard_->setToolTip(snapshot.error.isEmpty() ? QString() : QStringLiteral("读取失败，重新进入此页面可重试。"));
    }
    if (!snapshot.error.isEmpty()) {
        if (stackedWidget_ && stackedWidget_->currentIndex() == 1 && forestReadError_ != snapshot.error) {
            forestReadError_ = snapshot.error;
            DialogPresenter::error(this, QStringLiteral("森林数据读取失败"),
                QStringLiteral("原森林及统计已保留，未将错误当作空记录。请检查数据目录后重新进入页面。\n%1").arg(snapshot.error));
        }
        return;
    }
    forestReadError_.clear();
    gardenCanvas_->loadRecords(snapshot.records);
    if (forestDashboard_) {
        forestDashboard_->setTagNames(snapshot.tagNames);
        forestDashboard_->setRecords(std::move(snapshot.records));
    }
}

void MainWindow::setupTrayIcon()
{
    trayIcon_ = new QSystemTrayIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon), this);
    trayIcon_->setToolTip(QStringLiteral("Forest 专注森林"));
    auto* menu = new QMenu;
    auto* showAction = menu->addAction(QStringLiteral("显示主窗口"));
    QObject::connect(showAction, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
    });
    trayPauseAction_ = menu->addAction(QStringLiteral("暂停专注"));
    QObject::connect(trayPauseAction_, &QAction::triggered, this, &MainWindow::onPauseResumeClicked);
    trayAbandonAction_ = menu->addAction(QStringLiteral("放弃专注"));
    QObject::connect(trayAbandonAction_, &QAction::triggered, this, &MainWindow::onAbandonClicked);
    menu->addSeparator();
    auto* quitAction = menu->addAction(QStringLiteral("退出"));
    quitAction->setObjectName("trayQuitAction");
    QObject::connect(quitAction, &QAction::triggered, this, &MainWindow::close);
    trayIcon_->setContextMenu(menu);
    QObject::connect(trayIcon_, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    trayIcon_->show();
    updateTrayStatus();
}

void MainWindow::updateTrayStatus()
{
    if (!trayIcon_) return;

    const FocusPageState state = FocusPageState::from(controller_);
    if (trayPauseAction_) {
        trayPauseAction_->setVisible(state.active && !state.saveFailed);
        trayPauseAction_->setEnabled(state.paused || state.canPause);
        trayPauseAction_->setText(state.paused ? QStringLiteral("继续专注")
                                                : QStringLiteral("暂停专注"));
    }
    if (trayAbandonAction_) {
        trayAbandonAction_->setVisible(state.active && !state.paused && !state.saveFailed);
        trayAbandonAction_->setEnabled(state.active && !state.paused && !state.saveFailed);
    }

    if (state.saveFailed) {
        trayIcon_->setToolTip(QStringLiteral("Forest 专注森林 - 结果待保存，请返回主窗口重试"));
        return;
    }
    if (state.idle) {
        trayIcon_->setToolTip(QStringLiteral("Forest 专注森林 - 准备开始"));
        return;
    }

    const uint32_t seconds = controller_.timerMode() == FocusController::TimerMode::COUNTDOWN
        ? controller_.remainingSeconds() : controller_.actualSeconds();
    const QString timeText = QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    trayIcon_->setToolTip(QStringLiteral("Forest 专注森林 - %1 %2")
        .arg(state.paused ? QStringLiteral("已暂停") : QStringLiteral("专注中"), timeText));
}

void MainWindow::applyAccessibilityPreferences()
{
    const auto options = UserPreferences::instance().accessibilityOptions();
    QFont font = baseApplicationFont_;
    const qreal baseSize = font.pointSizeF() > 0 ? font.pointSizeF() : 10.0;
    font.setPointSizeF(baseSize * options.fontScalePercent / 100.0);
    QApplication::setFont(font);

    QString contrastRules;
    if (options.highContrast) {
        contrastRules = QStringLiteral(
            "QPushButton, QComboBox, QSpinBox, QLineEdit, QCheckBox::indicator {"
            " border:2px solid #173F33; }"
            "QPushButton:focus, QComboBox:focus, QSpinBox:focus, QLineEdit:focus, QCheckBox:focus {"
            " outline:2px solid #F2C94C; outline-offset:2px; }");
    }
    qApp->setStyleSheet(AppStyle::scaledStyleSheet(AppStyle::styleSheet(), options.fontScalePercent) + contrastRules);
    AppStyle::applyAccessibility(*this);
    for (QWidget* child : findChildren<QWidget*>()) AppStyle::applyAccessibility(*child);
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) { show(); raise(); activateWindow(); }
}

void MainWindow::onOpenGacha()
{
    switchPage(5);
}

void MainWindow::onOpenAchievements()
{
    switchPage(4);
}

void MainWindow::onOpenMyForest()
{
    if (!m_forestLayout) return;
    MyForestDialog dlg(m_forestLayout, &db_, this);
    dlg.exec();
}

void MainWindow::onOpenFriends()
{
    switchPage(6);
}

void MainWindow::onOpenChallenges()
{
    switchPage(7);
}

void MainWindow::onOpenGuardian()
{
    switchPage(8);
}

void MainWindow::handleFocusFinalized(uint32_t recordId, uint32_t status,
                                      const FocusResultService::Outcome& outcome)
{
    monitor_.stopMonitoring();
    quoteTimer_.stop();
    hidePauseBreakOverlay();
    refreshGarden();
    refreshGuardianPage();
    refreshChallengePage();
    refreshAchievementsPage();

    if (!outcome.applied) {
        DialogPresenter::warning(this, QStringLiteral("数据更新失败"),
            QStringLiteral("专注记录已保存，但后续状态未能完成更新：%1").arg(outcome.error));
        return;
    }
    if (closingFocus_) return;
    const bool completed = static_cast<FocusRecordStatus>(status) == FocusRecordStatus::Success;
    if (outcome.alreadyApplied && !completed) return;
    showFocusResult(recordId, outcome, completed);
}

void MainWindow::showFocusResult(uint32_t recordId, const FocusResultService::Outcome& outcome,
                                 bool completed)
{
    const auto record = db_.readById(recordId);
    if (!record) {
        DialogPresenter::error(this, QStringLiteral("专注结果读取失败"), db_.lastError().isEmpty()
            ? QStringLiteral("结果记录暂不可用，请到我的森林重试。") : db_.lastError());
        return;
    }

    const FocusRecord& focus = record.value();
    const QString plantName = PlantCatalog::isKnown(focus.plantType)
        ? PlantCatalog::byType(focus.plantType).displayName : QStringLiteral("当前植物");
    const uint32_t minutes = (focus.actualSeconds + 59) / 60;

    QWidget backdrop(centralWidget());
    backdrop.setObjectName("focusResultBackdrop");
    backdrop.setStyleSheet("QWidget#focusResultBackdrop { background:rgba(12,45,34,118); border:none; }");
    backdrop.setAttribute(Qt::WA_TransparentForMouseEvents);
    backdrop.setGeometry(centralWidget()->rect());
    backdrop.show();
    backdrop.raise();
    QDialog dialog(this);
    dialog.setWindowTitle(completed ? QStringLiteral("专注完成") : QStringLiteral("本次专注已结束"));
    dialog.setProperty("testId", "focusResultDialog");
    DialogPresenter::prepare(dialog);
    dialog.setMinimumWidth(560);
    dialog.setMaximumWidth(640);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(30, 26, 30, 24);
    layout->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->setSpacing(20);
    auto* plant = new QLabel(&dialog);
    plant->setObjectName("focusResultPlant");
    plant->setFixedSize(96, 112);
    plant->setAlignment(Qt::AlignCenter);
    QPixmap resultPlant = PlantImageUtils::loadPlantIcon(
        PlantCatalog::byType(focus.plantType).iconPath);
    if (!completed && !resultPlant.isNull()) {
        QImage image = resultPlant.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int gray = qGray(pixels[x]);
                pixels[x] = qRgba(gray, gray, gray, qAlpha(pixels[x]));
            }
        }
        resultPlant = QPixmap::fromImage(image);
    }
    plant->setPixmap(resultPlant.scaled(plant->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    plant->setAccessibleName(plantName);
    header->addWidget(plant);
    auto* title = new QLabel(completed ? QStringLiteral("做得好，专注已经完成")
                                        : QStringLiteral("这次先停在这里"), &dialog);
    title->setObjectName("focusResultTitle");
    title->setWordWrap(true);
    title->setTextFormat(Qt::PlainText);
    title->setStyleSheet("font-size:22px; font-weight:700; color:#245543;");
    header->addWidget(title, 1);
    layout->addLayout(header);

    auto* summary = new QLabel(&dialog);
    summary->setObjectName("focusResultSummary");
    summary->setWordWrap(true);
    summary->setTextFormat(Qt::PlainText);
    summary->setStyleSheet("font-size:15px; color:#48675B; padding:12px 0;");
    if (completed) {
        summary->setText(QStringLiteral("%1 分钟专注 · %2\n获得 🪙 %3%4")
            .arg(minutes)
            .arg(plantName)
            .arg(outcome.focusCoins)
            .arg(outcome.unlockedAchievements.isEmpty()
                ? QString() : QStringLiteral("\n解锁 %1 项新成就").arg(outcome.unlockedAchievements.size())));
    } else {
        summary->setText(QStringLiteral("已记录约 %1 分钟 · %2\n植物会以枯萎状态保存在森林中，本次不发放专注奖励。")
            .arg(minutes).arg(plantName));
    }
    layout->addWidget(summary);

    auto* actions = new QHBoxLayout;
    actions->addStretch();
    auto* closeButton = new QPushButton(completed ? QStringLiteral("稍后再说") : QStringLiteral("返回主页"), &dialog);
    closeButton->setMinimumWidth(105);
    closeButton->setDefault(true);
    DialogPresenter::setSecondary(closeButton);
    actions->addWidget(closeButton);
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    bool startBreak = false;
    bool startAgain = false;
    if (completed) {
        auto* restButton = new QPushButton(QStringLiteral("开始 5 分钟休息"), &dialog);
        restButton->setProperty("testId", "focusRestButton");
        auto* againButton = new QPushButton(QStringLiteral("再专注一次"), &dialog);
        againButton->setProperty("testId", "focusAgainButton");
        restButton->setAutoDefault(false);
        againButton->setAutoDefault(false);
        DialogPresenter::setSecondary(restButton);
        DialogPresenter::setPrimary(againButton);
        actions->addWidget(restButton);
        actions->addWidget(againButton);
        QObject::connect(restButton, &QPushButton::clicked, &dialog, [&]() {
            startBreak = true;
            dialog.accept();
        });
        QObject::connect(againButton, &QPushButton::clicked, &dialog, [&]() {
            startAgain = true;
            dialog.accept();
        });
    }
    layout->addLayout(actions);
    if (qApp->property("forest.uiSmokeAutoClose").toBool()) {
        QTimer::singleShot(0, &dialog, &QDialog::reject);
    }
    dialog.adjustSize();
    QPainterPath resultShape;
    resultShape.addRoundedRect(QRectF(dialog.rect()), 22, 22);
    dialog.setMask(QRegion(resultShape.toFillPolygon().toPolygon()));
    dialog.exec();

    if (startBreak) {
        QDialog restDialog(this);
        restDialog.setWindowTitle(QStringLiteral("短暂休息"));
        restDialog.setProperty("testId", "focusRestDialog");
        DialogPresenter::prepare(restDialog);
        restDialog.setMinimumWidth(560);
        restDialog.setMaximumWidth(640);
        auto* restLayout = new QVBoxLayout(&restDialog);
        restLayout->setContentsMargins(34, 30, 34, 28);
        restLayout->setSpacing(18);
        auto* restTitle = new QLabel(QStringLiteral("休息 5 分钟"), &restDialog);
        restTitle->setObjectName("focusRestTitle");
        restTitle->setAlignment(Qt::AlignCenter);
        restTitle->setStyleSheet("font-size:22px; font-weight:700; color:#245543;");
        auto* remaining = new QLabel(&restDialog);
        remaining->setObjectName("focusRestRemaining");
        remaining->setAlignment(Qt::AlignCenter);
        remaining->setStyleSheet("font-size:52px; color:#287756; padding:12px 0;");
        auto* resumeButton = new QPushButton(QStringLiteral("返回专注"), &restDialog);
        resumeButton->setProperty("testId", "focusRestReturnButton");
        resumeButton->setDefault(true);
        DialogPresenter::setPrimary(resumeButton);
        auto* restDescription = new QLabel(QStringLiteral("离开屏幕、喝水或活动一下。休息不计入专注记录和奖励。"), &restDialog);
        restDescription->setObjectName("focusRestDescription");
        restDescription->setWordWrap(true);
        restDescription->setAlignment(Qt::AlignCenter);
        restDescription->setStyleSheet("font-size:15px; color:#48675B;");
        restLayout->addWidget(restTitle);
        restLayout->addWidget(restDescription);
        restLayout->addWidget(remaining);
        restLayout->addWidget(resumeButton, 0, Qt::AlignHCenter);

        const qint64 restDeadline = controller_.clockMilliseconds() + 300000;
        const auto updateBreakText = [&]() {
            const int seconds = static_cast<int>(qMax<qint64>(0,
                restDeadline - controller_.clockMilliseconds() + 999) / 1000);
            remaining->setText(QStringLiteral("%1:%2")
                .arg(seconds / 60, 2, 10, QLatin1Char('0'))
                .arg(seconds % 60, 2, 10, QLatin1Char('0')));
        };
        updateBreakText();
        QTimer restTimer(&restDialog);
        QObject::connect(&restTimer, &QTimer::timeout, &restDialog, [&]() {
            if (controller_.clockMilliseconds() >= restDeadline) {
                restDialog.accept();
                return;
            }
            updateBreakText();
        });
        QObject::connect(resumeButton, &QPushButton::clicked, &restDialog, &QDialog::accept);
        restTimer.start(1000);
        restDialog.adjustSize();
        QPainterPath restShape;
        restShape.addRoundedRect(QRectF(restDialog.rect()), 22, 22);
        restDialog.setMask(QRegion(restShape.toFillPolygon().toPolygon()));
        restDialog.exec();
    }
    if (startAgain) QTimer::singleShot(0, this, &MainWindow::onStartClicked);
}

