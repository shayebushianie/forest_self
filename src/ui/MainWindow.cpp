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
#include "ui/SettingsPage.h"
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
#include <QImage>
#include <QColor>
#include <QScreen>
#include <QSignalBlocker>
#include <QEvent>
#include <QFileDialog>
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
#include <functional>
#include <limits>
#include "config/UserPreferences.h"
#include "storage/BackupService.h"
#include "common/PathConfig.h"

namespace {

class FocusStartButton final : public QPushButton {
public:
    explicit FocusStartButton(QWidget* parent = nullptr) : QPushButton(parent) {}

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QRectF bounds = rect().adjusted(1, 1, -1, -1);
        QColor textColor("#185442");
        QColor borderColor(255, 255, 255, 215);
        QLinearGradient fill(bounds.topLeft(), bounds.bottomLeft());
        fill.setColorAt(0.0, QColor("#83DFC3"));
        fill.setColorAt(1.0, QColor("#5FC9AE"));

        if (!isEnabled()) {
            fill.setColorAt(0.0, QColor(99, 170, 148, 125));
            fill.setColorAt(1.0, QColor(76, 147, 126, 125));
            textColor = QColor(247, 255, 247, 115);
            borderColor = QColor(255, 255, 255, 70);
        } else if (isDown()) {
            fill.setColorAt(0.0, QColor("#69C6AA"));
            fill.setColorAt(1.0, QColor("#52B99C"));
        } else if (underMouse()) {
            fill.setColorAt(0.0, QColor("#A0E9D0"));
            fill.setColorAt(1.0, QColor("#76D8BC"));
        }

        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(bounds, bounds.height() / 2.0, bounds.height() / 2.0);

        if (AppStyle::keyboardFocusVisible(this)) {
            painter.setPen(QPen(QColor("#F2F4C6"), 3, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(bounds.center().x() - 18, bounds.bottom() - 7),
                             QPointF(bounds.center().x() + 18, bounds.bottom() - 7));
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
        if (!record) return;
        const FocusRecord& focus = record.value();
        const QString plantName = PlantCatalog::isKnown(focus.plantType)
            ? PlantCatalog::byType(focus.plantType).displayName : QStringLiteral("未知植物");
        QString tagName = QStringLiteral("无标签");
        if (focus.tagId != 0 && m_tagMgr) {
            const auto tag = m_tagMgr->tag(focus.tagId);
            if (!tag.name.trimmed().isEmpty()) tagName = tag.name.trimmed();
        }
        const QString state = focus.status == FocusRecordStatus::Success
            ? QStringLiteral("成功完成") : QStringLiteral("主动放弃");
        const QString started = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(focus.startTimestamp), Qt::LocalTime).toString(QStringLiteral("yyyy年M月d日 HH:mm"));
        QDialog detail(this);
        detail.setWindowTitle(QStringLiteral("专注记录"));
        DialogPresenter::prepare(detail);
        detail.setMinimumWidth(380);
        auto* layout = new QVBoxLayout(&detail);
        layout->setContentsMargins(28, 24, 28, 22);
        auto* title = new QLabel(QStringLiteral("专注记录"), &detail);
        title->setStyleSheet("font-size:20px; font-weight:800; color:#245543;");
        auto* content = new QLabel(QStringLiteral("%1\n%2\n时长：%3 分钟\n项目：%4\n植物：%5\n状态：%6\n奖励：🪙 %7")
            .arg(started)
            .arg(focus.focusMode == PersistedFocusMode::Deep ? QStringLiteral("深度专注") : QStringLiteral("专注"))
            .arg((focus.actualSeconds + 59) / 60).arg(tagName).arg(plantName).arg(state).arg(focus.coinsEarned), &detail);
        content->setWordWrap(true);
        content->setStyleSheet("font-size:14px; color:#48675B;");
        auto* close = new QPushButton(QStringLiteral("关闭"), &detail);
        close->setObjectName("btnSecondary");
        layout->addWidget(title);
        layout->addWidget(content);
        layout->addWidget(close, 0, Qt::AlignRight);
        QObject::connect(close, &QPushButton::clicked, &detail, &QDialog::accept);
        detail.exec();
    });
    QObject::connect(forestDashboard_, &ForestDashboardWidget::filtersRequested,
                     this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("筛选森林记录"));
        DialogPresenter::prepare(dialog);
        dialog.setMinimumWidth(360);
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout;
        const auto current = forestDashboard_->recordFilter();

        auto* dateEnabled = new QCheckBox(QStringLiteral("指定日期"), &dialog);
        auto* dateEdit = new QDateEdit(current.date.isValid() ? current.date : QDate::currentDate(), &dialog);
        dateEdit->setCalendarPopup(true);
        dateEnabled->setChecked(current.date.isValid());
        dateEdit->setEnabled(dateEnabled->isChecked());
        QObject::connect(dateEnabled, &QCheckBox::toggled, dateEdit, &QWidget::setEnabled);

        auto* tagFilter = new QComboBox(&dialog);
        tagFilter->addItem(QStringLiteral("全部项目"), QVariant::fromValue<qulonglong>(std::numeric_limits<uint32_t>::max()));
        tagFilter->addItem(QStringLiteral("无标签"), 0);
        if (m_tagMgr) {
            for (const auto& tag : m_tagMgr->all())
                tagFilter->addItem(tag.name, tag.id);
        }
        const int tagIndex = current.tagId == std::numeric_limits<uint32_t>::max()
            ? 0 : tagFilter->findData(current.tagId);
        tagFilter->setCurrentIndex(tagIndex >= 0 ? tagIndex : 0);

        auto* plantFilter = new QComboBox(&dialog);
        plantFilter->addItem(QStringLiteral("全部植物"), QVariant::fromValue<qulonglong>(std::numeric_limits<uint32_t>::max()));
        for (const PlantDefinition& plant : PlantCatalog::all()) {
            plantFilter->addItem(plant.displayName, plant.type);
        }
        const int plantIndex = current.plantType == std::numeric_limits<uint32_t>::max()
            ? 0 : plantFilter->findData(current.plantType);
        plantFilter->setCurrentIndex(plantIndex >= 0 ? plantIndex : 0);

        auto* statusFilter = new QComboBox(&dialog);
        statusFilter->addItem(QStringLiteral("全部状态"), -1);
        statusFilter->addItem(QStringLiteral("成功完成"), 0);
        statusFilter->addItem(QStringLiteral("主动放弃"), 1);
        const int statusIndex = statusFilter->findData(current.terminalState);
        statusFilter->setCurrentIndex(statusIndex >= 0 ? statusIndex : 0);

        form->addRow(dateEnabled, dateEdit);
        form->addRow(QStringLiteral("项目标签:"), tagFilter);
        form->addRow(QStringLiteral("植物:"), plantFilter);
        form->addRow(QStringLiteral("状态:"), statusFilter);
        layout->addLayout(form);
        auto* actions = new QHBoxLayout;
        auto* reset = new QPushButton(QStringLiteral("清除筛选"), &dialog);
        auto* cancel = new QPushButton(QStringLiteral("取消"), &dialog);
        auto* apply = new QPushButton(QStringLiteral("应用"), &dialog);
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
        dialog.exec();
    });
    QObject::connect(forestDashboard_, &ForestDashboardWidget::overviewRequested,
                     this, [this]() {
        QDialog dialog(this);
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setWindowTitle(QStringLiteral("总览筛选"));
        DialogPresenter::prepare(dialog);
        dialog.setAttribute(Qt::WA_TranslucentBackground, true);
        dialog.setObjectName("forestOverviewDialog");
        dialog.setFixedWidth(640);
        dialog.setStyleSheet(
            "QDialog#forestOverviewDialog {"
            " background:transparent; border:none; }"
            "QFrame#forestOverviewSurface {"
            " background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #E8F6C9,stop:1 #BCE9C5);"
            " border:2px solid #3F9670; border-radius:24px; }"
            "QLabel#forestOverviewLeaf { background:#F4D96E; border:2px solid #D4AD42; border-radius:22px;"
            " color:#315747; font-size:21px; }"
            "QLabel#forestOverviewTitle { color:#245543; font-size:22px; font-weight:900; }"
            "QLabel#forestOverviewDescription { color:#4C735E; font-size:13px; }"
            "QWidget#forestOverviewTagCard { background:rgba(255,255,255,188);"
            " border:1px solid rgba(63,150,112,125); border-radius:16px; }"
            "QLabel#forestOverviewTagLabel { color:#315747; font-size:14px; font-weight:800; }"
            "QScrollArea#forestOverviewTagScroll { background:transparent; border:none; }"
            "QScrollArea#forestOverviewTagScroll > QWidget > QWidget { background:transparent; }"
            "QLineEdit#forestOverviewAddInput { background:#FFFDEB; border:1px solid #8FC486;"
            " border-radius:10px; color:#245543; padding:7px 10px; }"
            "QPushButton#forestOverviewAddTag { background:#E9ECEA; border:1px dashed #9DB9A5;"
            " border-radius:16px; color:#526A5D; font-size:26px; font-weight:500; }"
            "QPushButton#forestOverviewAddTag:hover { background:#FFFFFF; border-color:#3E9A6F; color:#245543; }");

        auto* windowLayout = new QVBoxLayout(&dialog);
        windowLayout->setContentsMargins(0, 0, 0, 0);
        windowLayout->setSpacing(0);
        auto* surface = new QFrame(&dialog);
        surface->setObjectName("forestOverviewSurface");
        windowLayout->addWidget(surface);
        auto* layout = new QVBoxLayout(surface);
        layout->setContentsMargins(28, 26, 28, 24);
        layout->setSpacing(16);
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
        tagLayout->setContentsMargins(16, 14, 16, 16);
        tagLayout->setSpacing(8);
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
        tagOptions->setStyleSheet("background:transparent; border:none;");
        auto* tagOptionsLayout = new QGridLayout(tagOptions);
        tagOptionsLayout->setContentsMargins(0, 0, 0, 0);
        tagOptionsLayout->setHorizontalSpacing(10);
        tagOptionsLayout->setVerticalSpacing(10);
        for (int column = 0; column < 4; ++column) tagOptionsLayout->setColumnStretch(column, 1);
        int tagOptionCount = 0;
        const auto addTagOption = [&](uint32_t tagId, const QString& name, const QColor& color) {
            auto* option = new QRadioButton(name, tagOptions);
            option->setObjectName("forestOverviewTagOption");
            option->setProperty("tagId", QVariant::fromValue<qulonglong>(tagId));
            option->setChecked(tagId == selectedTagId);
            option->setCursor(Qt::PointingHandCursor);
            option->setFixedHeight(44);
            option->setToolTip(name);
            option->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            option->setStyleSheet(QStringLiteral(
                "QRadioButton { background:#E9ECEA; border:1px solid transparent; border-radius:18px;"
                " color:#3B5348; font-size:14px; font-weight:800; padding:8px 12px; }"
                "QRadioButton:hover { background:#F7FAF5; border-color:%1; }"
                "QRadioButton:checked { background:#FFFFFF; border-color:#FFFFFF; color:#245543; }"
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
        addTagButton->setCursor(Qt::PointingHandCursor);
        addTagButton->setFixedHeight(44);
        addTagButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const auto refreshTagGrid = [&]() {
            tagOptionsLayout->removeWidget(addTagButton);
            tagOptionsLayout->addWidget(addTagButton, tagOptionCount / 4, tagOptionCount % 4);
            const int totalChips = tagOptionCount + 1;
            const int visibleRows = qMin(4, (totalChips + 3) / 4);
            const int allRows = (totalChips + 3) / 4;
            tagScroll->setVerticalScrollBarPolicy(
                totalChips > 16 ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
            tagScroll->setFixedHeight(visibleRows * 44 + qMax(0, visibleRows - 1) * 10 + 2);
            tagOptions->setMinimumHeight(allRows * 44 + qMax(0, allRows - 1) * 10 + 2);
        };
        refreshTagGrid();
        tagScroll->setWidget(tagOptions);

        auto* addRow = new QWidget(tagCard);
        auto* addRowLayout = new QHBoxLayout(addRow);
        addRowLayout->setContentsMargins(0, 0, 0, 0);
        addRowLayout->setSpacing(8);
        auto* addInput = new QLineEdit(addRow);
        addInput->setObjectName("forestOverviewAddInput");
        addInput->setPlaceholderText(QStringLiteral("输入新标签名称"));
        auto* addConfirm = new QPushButton(QStringLiteral("添加"), addRow);
        addConfirm->setProperty("testId", "forestOverviewAddConfirm");
        DialogPresenter::setPrimary(addConfirm);
        addRowLayout->addWidget(addInput, 1);
        addRowLayout->addWidget(addConfirm);
        addRow->hide();

        QObject::connect(addTagButton, &QPushButton::clicked, &dialog, [addRow, addInput]() {
            addRow->show();
            addInput->setFocus();
        });
        QObject::connect(addConfirm, &QPushButton::clicked, &dialog,
                         [this, &selectedTagId, addInput, addRow, addTagOption, refreshTagGrid]() {
            if (!m_tagMgr) return;
            const QString name = addInput->text().trimmed();
            if (name.isEmpty()) {
                DialogPresenter::warning(this, QStringLiteral("标签名称为空"),
                                         QStringLiteral("请输入一个项目标签名称。"));
                return;
            }
            for (const TagDef& tag : m_tagMgr->all()) {
                if (tag.name.compare(name, Qt::CaseInsensitive) == 0) {
                    DialogPresenter::warning(this, QStringLiteral("标签已存在"),
                                             QStringLiteral("请使用一个不同的项目标签名称。"));
                    return;
                }
            }
            const QStringList palette = {QStringLiteral("#A8C76A"), QStringLiteral("#F8BF56"),
                                         QStringLiteral("#59C8C4"), QStringLiteral("#B8A8E7"),
                                         QStringLiteral("#F08A7C"), QStringLiteral("#78A9E3")};
            const QString color = palette.at(m_tagMgr->count() % palette.size());
            const uint32_t tagId = m_tagMgr->add(name, color);
            if (!m_tagMgr->save()) {
                m_tagMgr->remove(tagId);
                DialogPresenter::error(this, QStringLiteral("保存标签失败"),
                                       QStringLiteral("新标签未能安全保存，请稍后重试。"));
                return;
            }
            selectedTagId = tagId;
            addTagOption(tagId, name, QColor(color))->setChecked(true);
            refreshTagGrid();
            refreshTagOptions();
            addInput->clear();
            addRow->hide();
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
        dialog.exec();
    });
    challengeDashboard_ = new ChallengeDashboardWidget;
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
    QObject::connect(challengeDashboard_, &ChallengeDashboardWidget::rewardCoinsRequested,
                     this, [this](int amount) {
        if (amount > 0) {
            coinManager_.earn(static_cast<uint32_t>(amount));
            refreshChallengePage();
        }
    });

    // ========== Page 0: 专注主页 ==========
    auto* page0 = new QWidget;
    page0->setObjectName("focusPage");
    page0->setStyleSheet(
        "QWidget#focusPage {"
        " background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 #4EA488, stop:1 #68BBA2);"
        "}");
    auto* p0Layout = new QVBoxLayout(page0);
    p0Layout->setContentsMargins(28, 72, 28, 22);
    p0Layout->setSpacing(0);

    auto* focusContent = new QWidget(page0);
    focusContent->setObjectName("focusContent");
    focusContent->setMinimumWidth(620);
    focusContent->setMaximumWidth(980);
    focusContent->setStyleSheet("QWidget#focusContent { background:transparent; border:none; }");
    auto* focusPageLayout = new QVBoxLayout(focusContent);
    focusPageLayout->setContentsMargins(0, 0, 0, 0);
    focusPageLayout->setSpacing(10);
    p0Layout->addWidget(focusContent, 1, Qt::AlignHCenter);

    auto* statusLayout = new QHBoxLayout;
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);

    auto* modeSwitch = new QWidget(page0);
    modeSwitch->setObjectName("focusModeSwitch");
    modeSwitch->setStyleSheet(
        "QWidget#focusModeSwitch { background:rgba(255,255,255,16);"
        " border:1px solid rgba(255,255,255,36); border-radius:20px; }"
        "QLabel[modeTitle=\"true\"] { color:rgba(247,255,247,120); font-size:16px;"
        " font-weight:900; background:transparent; border:none; padding:0 12px; }"
        "QLabel[modeTitle=\"true\"][active=\"true\"] { color:#F7FFF7; }");
    auto* modeSwitchLayout = new QHBoxLayout(modeSwitch);
    modeSwitchLayout->setContentsMargins(0, 0, 0, 0);
    modeSwitchLayout->setSpacing(14);
    homeCountdownModeLabel_ = new QLabel(QStringLiteral("倒计时"), modeSwitch);
    homeStopwatchModeLabel_ = new QLabel(QStringLiteral("正计时"), modeSwitch);
    homeCountdownModeLabel_->setProperty("modeTitle", "true");
    homeStopwatchModeLabel_->setProperty("modeTitle", "true");
    homeCountdownModeLabel_->setAlignment(Qt::AlignCenter);
    homeStopwatchModeLabel_->setAlignment(Qt::AlignCenter);
    homeCountdownModeLabel_->setCursor(Qt::PointingHandCursor);
    homeStopwatchModeLabel_->setCursor(Qt::PointingHandCursor);
    homeCountdownModeLabel_->installEventFilter(this);
    homeStopwatchModeLabel_->installEventFilter(this);

    auto* modePill = new QWidget(modeSwitch);
    modePill->setObjectName("topModePill");
    modePill->setStyleSheet(
        "QWidget#topModePill { background:transparent; border:none; }"
        "QPushButton[mode=\"true\"] { background:transparent; border:none;"
        " border-radius:16px; color:rgba(247,255,247,135); font-size:22px;"
        " font-weight:900; padding:0; }"
        "QPushButton[mode=\"true\"]:hover { background-color:rgba(255,255,255,18); color:#FFFFFF; }"
        "QPushButton[mode=\"true\"][active=\"true\"] { background:transparent;"
        " border:none; color:#F7FFF7; }");
    modePill->setFixedHeight(42);
    auto* topModeLayout = new QHBoxLayout(modePill);
    topModeLayout->setContentsMargins(5, 5, 5, 5);
    topModeLayout->setSpacing(0);
    homeCountdownBtn_ = new QPushButton(QString::fromUtf8(u8"⌛"), modePill);
    homeStopwatchBtn_ = new QPushButton(QString::fromUtf8(u8"⏱"), modePill);
    homeCountdownBtn_->setObjectName("homeCountdownModeButton");
    homeStopwatchBtn_->setObjectName("homeStopwatchModeButton");
    homeCountdownBtn_->setAccessibleName(QStringLiteral("倒计时模式设置"));
    homeStopwatchBtn_->setAccessibleName(QStringLiteral("正计时模式设置"));
    homeCountdownBtn_->setProperty("mode", "true");
    homeStopwatchBtn_->setProperty("mode", "true");
    const QString modeButtonStyle = QStringLiteral(
        "QPushButton { background:transparent; border:none; border-radius:16px; }"
        "QPushButton:hover { background-color:rgba(255,255,255,22); }");
    homeCountdownBtn_->setStyleSheet(modeButtonStyle);
    homeStopwatchBtn_->setStyleSheet(modeButtonStyle);
    homeCountdownBtn_->setToolTip(QStringLiteral("倒计时"));
    homeStopwatchBtn_->setToolTip(QStringLiteral("正计时"));
    homeCountdownBtn_->setCursor(Qt::PointingHandCursor);
    homeStopwatchBtn_->setCursor(Qt::PointingHandCursor);
    homeCountdownBtn_->setFixedSize(52, 32);
    homeStopwatchBtn_->setFixedSize(52, 32);
    topModeLayout->addWidget(homeCountdownBtn_);
    topModeLayout->addWidget(homeStopwatchBtn_);
    modeSwitchLayout->addWidget(homeCountdownModeLabel_);
    modeSwitchLayout->addWidget(modePill);
    modeSwitchLayout->addWidget(homeStopwatchModeLabel_);

    coinLabel_ = new QLabel(page0);
    coinLabel_->setAlignment(Qt::AlignCenter);
    coinLabel_->setStyleSheet(
        "background-color:rgba(255,255,255,18); border:1px solid rgba(255,255,255,36);"
        "border-radius:16px; padding:8px 16px; color:#F7FFF7; font-weight:800;");
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
    focusHint->setAlignment(Qt::AlignCenter);
    focusHint->setStyleSheet(
        "font-size:14px; font-weight:700; color:rgba(248,255,233,190);"
        "padding:5px 16px; background:transparent; border:none;");
    auto* focusHintLayout = new QHBoxLayout;
    focusHintLayout->setContentsMargins(0, 0, 0, 0);
    focusHintLayout->addStretch();
    focusHintLayout->addWidget(focusHint);
    focusHintLayout->addStretch();
    focusPageLayout->addLayout(focusHintLayout);
    timerRing_->setMaximumHeight(720);
    focusPageLayout->addWidget(timerRing_, 1);

    homeModeOverlay_ = new QWidget(centralWidget());
    homeModeOverlay_->setObjectName("homeModeOverlay");
    homeModeOverlay_->setStyleSheet(
        "QWidget#homeModeOverlay { background-color:rgba(0,0,0,96); border:none; }");
    homeModeOverlay_->hide();
    homeModeOverlay_->setFocusPolicy(Qt::StrongFocus);
    homeModeOverlay_->installEventFilter(this);
    auto* overlayLayout = new QVBoxLayout(homeModeOverlay_);
    overlayLayout->setContentsMargins(16, 86, 16, 16);
    overlayLayout->setSpacing(0);
    overlayLayout->addStretch();

    homeModePanel_ = new QWidget(homeModeOverlay_);
    auto* modePanel = homeModePanel_;
    modePanel->setObjectName("homeModePanel");
    modePanel->setStyleSheet(
        "QWidget#homeModePanel { background-color:rgba(109,181,156,232);"
        " border:1px solid rgba(255,255,255,95); border-radius:8px; }"
        "QWidget[optionRow=\"true\"] { background:transparent; border:none; }"
        "QLabel[optionIcon=\"true\"] { color:rgba(247,255,247,220); font-size:24px;"
        " background:transparent; border:none; }"
        "QLabel[optionTitle=\"true\"] { color:#F7FFF7; font-size:16px; font-weight:900;"
        " background:transparent; border:none; }"
        "QLabel[optionDesc=\"true\"] { color:rgba(247,255,247,185); font-size:13px;"
        " background:transparent; border:none; }"
        "QCheckBox { background:transparent; color:#F7FFF7; font-weight:700; spacing:8px; }"
        "QCheckBox::indicator { width:36px; height:20px; border-radius:10px;"
        " background-color:rgba(43,108,84,72); border:1px solid rgba(255,255,255,70); }"
        "QCheckBox::indicator:checked { background-color:#F2F4C6; border-color:#F7FFF7; }");
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
        check->setFixedWidth(54);

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
        QStringLiteral("只能使用白名单中的软件"),
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
    homeDeepFocusCheck_->setObjectName("homeDeepFocusOption");
    homeGroupPlantCheck_->setToolTip(QStringLiteral("多人一起种功能暂未接入当前专注流程"));
    optionList->addWidget(deepFocusRow);
    optionList->addWidget(groupPlantRow);
    optionList->addWidget(autoExtendRow);
    modePanelLayout->addWidget(countdownOptionsWidget_);

    pauseBreakOverlay_ = new QWidget(centralWidget());
    pauseBreakOverlay_->setObjectName("pauseBreakOverlay");
    pauseBreakOverlay_->setStyleSheet(
        "QWidget#pauseBreakOverlay { background-color:rgba(0,0,0,118); border:none; }"
        "QFrame#pauseBreakPanel { background-color:rgba(109,181,156,238);"
        " border:1px solid rgba(255,255,255,110); border-radius:14px; }"
        "QLabel#pauseBreakTitle { color:#F7FFF7; font-size:20px; font-weight:900;"
        " background:transparent; border:none; }"
        "QLabel#pauseBreakTime { color:#F2F4C6; font-size:52px; font-weight:300;"
        " background:transparent; border:none; }"
        "QPushButton#pauseBreakContinue { background-color:#F2F4C6; color:#2B6C54;"
        " border:1px solid rgba(255,255,255,130); border-radius:10px;"
        " padding:10px 34px; font-weight:900; font-size:15px; }"
        "QPushButton#pauseBreakContinue:hover { background-color:#F7FFF7; }"
        "QPushButton#pauseBreakContinue:pressed { background-color:#D7DCA5; }");
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
        if (controller_.currentState() != FocusController::State::PAUSED) {
            hidePauseBreakOverlay();
            return;
        }
        if (pauseBreakRemainingSeconds_ > 0) {
            --pauseBreakRemainingSeconds_;
        }
        updatePauseBreakText();
        if (pauseBreakRemainingSeconds_ <= 0) {
            resumeFromPauseBreak();
        }
    });
    auto* closeModeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), homeModeOverlay_);
    closeModeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(closeModeShortcut, &QShortcut::activated,
                     this, &MainWindow::hideHomeModeOptions);

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
    pauseBtn_->setEnabled(false);
    abandonBtn_->setEnabled(false);
    pauseBtn_->setVisible(false);
    abandonBtn_->setVisible(false);
    startBtn_->setCursor(Qt::PointingHandCursor);
    pauseBtn_->setCursor(Qt::PointingHandCursor);
    abandonBtn_->setCursor(Qt::PointingHandCursor);
    startBtn_->setMinimumSize(294, 64);
    startBtn_->setFont(QFont("Microsoft YaHei", 22, QFont::Normal));
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
    btnLayout->setContentsMargins(0, 42, 0, 0);
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
        "QWidget#forestPage {"
        " background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 #2F7B62, stop:0.35 #64BCA3, stop:1 #90CEE3);"
        "}");
    auto* p1Layout = new QVBoxLayout(page1);
    p1Layout->setContentsMargins(0, 0, 0, 0);

    auto* forestScroll = new QScrollArea(page1);
    forestScroll->setWidgetResizable(true);
    forestScroll->setFrameShape(QFrame::NoFrame);
    forestScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    forestScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    forestScroll->setStyleSheet(
        "QScrollArea { background:transparent; border:none; }"
        "QScrollArea > QWidget > QWidget { background:transparent; }");
    forestDashboard_->setParent(forestScroll);
    forestScroll->setWidget(forestDashboard_);
    p1Layout->addWidget(forestScroll);

    stackedWidget_->addWidget(page1);

    // ========== Page 2: 植物商城 ==========
    stackedWidget_->addWidget(shopPage_->page());

    // ========== Page 3: 系统设置 ==========
    settingsPage_ = new SettingsPage;
    stackedWidget_->addWidget(settingsPage_);
    refreshPlantCombo();
    refreshTagOptions();
    settingsPage_->setBlacklist(ruleEngine_.blacklist());
    settingsPage_->setDataSummary(
        PathConfig::isPortableMode() ? QStringLiteral("便携模式") : QStringLiteral("安装模式"),
        DatabaseManager::schemaVersion());
    settingsPage_->setGuardianGoalMinutes(m_guardian ? m_guardian->dailyGoalMinutes() : 30);

    SettingsPage::FocusSettings initialFocus;
    initialFocus.allowPause = UserPreferences::instance().allowPause();
    settingsPage_->setFocusSettings(initialFocus);

    QObject::connect(settingsPage_, &SettingsPage::focusSettingsChanged, this, [this]() {
        const auto focus = settingsPage_->focusSettings();
        timerRing_->setPlantType(focus.plantType);
        if (!focus.stopwatch) timerRing_->setSelectedMinutes(static_cast<uint32_t>(focus.minutes));
        if (homeDeepFocusCheck_ && homeDeepFocusCheck_->isChecked() != focus.deepFocus) {
            const QSignalBlocker blocker(homeDeepFocusCheck_);
            homeDeepFocusCheck_->setChecked(focus.deepFocus);
        }
        UserPreferences::instance().setAllowPause(focus.allowPause);
        refreshHomeModeControls();
        refreshHomeTagBadge();
    });

    const auto accessibility = UserPreferences::instance().accessibilityOptions();
    settingsPage_->setAccessibilitySettings(
        {accessibility.fontScalePercent, accessibility.reducedMotion, accessibility.highContrast});
    QObject::connect(settingsPage_, &SettingsPage::accessibilitySettingsChanged, this, [this]() {
        const auto current = settingsPage_->accessibilitySettings();
        UserPreferences::AccessibilityOptions options;
        options.fontScalePercent = current.fontScalePercent;
        options.reducedMotion = current.reducedMotion;
        options.highContrast = current.highContrast;
        if (UserPreferences::instance().setAccessibilityOptions(options))
            applyAccessibilityPreferences();
    });

    QObject::connect(homeCountdownBtn_, &QPushButton::clicked, this, [this]() {
        showHomeModeOptions(false);
    });
    QObject::connect(homeStopwatchBtn_, &QPushButton::clicked, this, [this]() {
        showHomeModeOptions(true);
    });
    QObject::connect(homeDeepFocusCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (!settingsPage_) return;
        auto focus = settingsPage_->focusSettings();
        focus.deepFocus = checked;
        settingsPage_->setFocusSettings(focus);
    });
    QObject::connect(timerRing_, &PlantTimerWidget::sig_timeSelected, this, [this](uint32_t minutes) {
        if (!settingsPage_) return;
        auto focus = settingsPage_->focusSettings();
        focus.minutes = static_cast<int>(minutes);
        settingsPage_->setFocusSettings(focus);
    });

    refreshHomeTagBadge();
    QObject::connect(settingsPage_, &SettingsPage::guardianGoalChanged, this, [this](int minutes) {
        if (!m_guardian) return;
        m_guardian->setDailyGoalMinutes(static_cast<uint32_t>(minutes));
        m_guardian->save();
        refreshGuardianPage();
    });
    QObject::connect(settingsPage_, &SettingsPage::blacklistAddRequested, this,
                     [this](const QString& processName) {
        ruleEngine_.addToBlacklist(processName);
        settingsPage_->setBlacklist(ruleEngine_.blacklist());
    });
    QObject::connect(settingsPage_, &SettingsPage::blacklistRemoveRequested, this,
                     [this](const QString& processName) {
        ruleEngine_.removeFromBlacklist(processName);
        settingsPage_->setBlacklist(ruleEngine_.blacklist());
    });

    QObject::connect(settingsPage_, &SettingsPage::backupRequested, this, [this]() {
        const QString root = QFileDialog::getExistingDirectory(this, QStringLiteral("选择备份保存位置"));
        if (root.isEmpty()) return;
        QString backupDir, error;
        if (BackupService::createBackup(PathConfig::getAppDataDir(), root, &backupDir, &error)) {
            DialogPresenter::information(this, QStringLiteral("备份完成"), QStringLiteral("备份已创建：\n%1").arg(backupDir));
        } else {
            DialogPresenter::warning(this, QStringLiteral("备份失败"), error);
        }
    });
    QObject::connect(settingsPage_, &SettingsPage::restoreRequested, this, [this]() {
        const QString backupDir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 Forest 备份目录"));
        if (backupDir.isEmpty()) return;
        QString error;
        if (BackupService::requestRestore(PathConfig::getAppDataDir(), backupDir, &error)) {
            DialogPresenter::information(this, QStringLiteral("恢复已安排"), QStringLiteral("重启 Forest 后将执行恢复。恢复前会自动创建安全快照。"));
        } else {
            DialogPresenter::warning(this, QStringLiteral("恢复请求失败"), error);
        }
    });
    QObject::connect(settingsPage_, &SettingsPage::exportRequested, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出专注记录"),
            QDir::home().filePath(QStringLiteral("Forest-focus-records.csv")), QStringLiteral("CSV 文件 (*.csv)"));
        if (path.isEmpty()) return;
        QString error;
        if (!BackupService::exportFocusCsv(db_, path, &error)) {
            DialogPresenter::warning(this, QStringLiteral("导出失败"), error);
        }
    });
    QObject::connect(settingsPage_, &SettingsPage::openDataDirectoryRequested, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(PathConfig::getAppDataDir()));
    });

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
        QStringLiteral("QWidget#challengePage { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #C8EFE1, stop:0.58 #DFF4EC, stop:1 #C7E8F5); }"),
        true, stackedWidget_));

    // ========== Page 8: 时间守护 ==========
    guardianDashboard_ = new GuardianDashboardWidget;
    stackedWidget_->addWidget(DashboardPageHost::create(
        guardianDashboard_, QStringLiteral("guardianPage"), QString(), false, stackedWidget_));

    QObject::connect(guardianDashboard_, &GuardianDashboardWidget::dailyGoalChanged, this, [this](int v) {
        if (!m_guardian) return;
        m_guardian->setDailyGoalMinutes(static_cast<uint32_t>(v));
        m_guardian->save();
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
            warningOverlay_->show(); warningOverlay_->raise();
        });

    QObject::connect(&controller_, &FocusController::sig_stateChanged,
        this, [this](FocusController::State s) {
            if (s != FocusController::State::WARNING && warningOverlay_) warningOverlay_->hide();
            if (s != FocusController::State::PAUSED) hidePauseBreakOverlay();
            updateUI();
        });

    QObject::connect(focusSession_.get(), &FocusSessionCoordinator::terminalResultReady,
        this, [this](uint32_t recordId, uint32_t status, const FocusResultService::Outcome& outcome) {
            handleFocusFinalized(recordId, status, outcome);
        });
    QObject::connect(focusSession_.get(), &FocusSessionCoordinator::startRegistrationFailed,
        this, [this]() {
            DialogPresenter::error(this, QStringLiteral("数据保存失败"),
                QStringLiteral("无法安全登记本次专注，专注将停止。"));
        });

    QObject::connect(&controller_, &FocusController::sig_growthStageChanged,
        this, [this](uint32_t) { updateUI(); });

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
        if (controller_.currentState() == FocusController::State::IDLE) onStartClicked();
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
        if (FocusPageState::from(controller_).active) onAbandonClicked();
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
        "QWidget#sidebarWidget { background-color: #3E8D75; border-right: 1px solid rgba(255,255,255,65); }"
        "QPushButton[nav=\"true\"] { background:transparent; border:none; border-radius:0;"
        "  border-left:3px solid transparent; text-align:left; padding:12px 16px;"
        "  font-size:14px; font-weight:bold; color:rgba(247,255,247,185); }"
        "QPushButton[nav=\"true\"]:hover { background-color:rgba(255,255,255,35); color:#FFFFFF; }"
        "QPushButton[nav=\"true\"][active=\"true\"] { background-color:rgba(242,244,198,48); color:#F7FFF7;"
        "  border-left:4px solid #F2F4C6; }"
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
    if (!settingsPage_) return;
    const uint32_t currentType = settingsPage_->focusSettings().plantType;
    QVector<SettingsPage::Option> options;
    for (const auto& plant : PlantCatalog::all()) {
        if (coinManager_.isPlantUnlocked(plant.type))
            options.push_back({plant.displayName, plant.type});
    }
    settingsPage_->setPlantOptions(options, currentType);
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
    challengeDashboard_->setSnapshot(
        snapshot.coinBalance,
        snapshot.challenges,
        snapshot.records);
}

void MainWindow::refreshTagOptions()
{
    if (!m_tagMgr || !settingsPage_) return;
    const uint32_t currentId = settingsPage_->focusSettings().tagId;
    QVector<SettingsPage::Option> options = {{QStringLiteral("无标签"), 0}};
    for (const auto& tag : m_tagMgr->all())
        options.push_back({tag.name, tag.id});
    settingsPage_->setTagOptions(options, currentId);
    refreshHomeTagBadge();
}

void MainWindow::setSelectedTagId(uint32_t tagId)
{
    if (!settingsPage_) return;
    if (tagId != 0 && m_tagMgr) {
        bool found = false;
        for (const auto& t : m_tagMgr->all()) {
            if (t.id == tagId) {
                found = true;
                break;
            }
        }
        if (!found) tagId = 0;
    }
    auto focus = settingsPage_->focusSettings();
    focus.tagId = tagId;
    settingsPage_->setFocusSettings(focus);
    refreshHomeTagBadge();
}

void MainWindow::refreshHomeTagBadge()
{
    if (!timerRing_) return;

    const uint32_t tagId = settingsPage_ ? settingsPage_->focusSettings().tagId : 0;
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
    if (controller_.currentState() == FocusController::State::RUNNING ||
        controller_.currentState() == FocusController::State::PAUSED ||
        controller_.currentState() == FocusController::State::WARNING) {
        return;
    }
    refreshPlantCombo();

    if (homePlantSelector_) {
        homePlantSelector_->show();
        homePlantSelector_->raise();
        homePlantSelector_->activateWindow();
        return;
    }

    auto* dialog = new QDialog(this, Qt::Popup | Qt::FramelessWindowHint);
    homePlantSelector_ = dialog;
    dialog->setObjectName(QStringLiteral("homePlantSelectorPopup"));
    dialog->setWindowTitle(QStringLiteral("选择植物"));
    dialog->setWindowModality(Qt::NonModal);
    dialog->setModal(false);
    dialog->setAttribute(Qt::WA_StyledBackground, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setStyleSheet(
        "QDialog { background-color:#5BAE93; }"
        "QLabel#selectorTitle { color:#F7FFF7; font-size:18px; font-weight:900; }"
        "QPushButton[plantCard=\"true\"] { background-color:rgba(255,255,255,42);"
        " border:1px solid rgba(255,255,255,95); border-radius:10px; padding:10px;"
        " color:#F7FFF7; font-weight:800; text-align:center; }"
        "QPushButton[plantCard=\"true\"]:hover { background-color:rgba(242,244,198,72);"
        " border-color:#F2F4C6; }");

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("选择本次专注植物"), dialog);
    title->setObjectName("selectorTitle");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* grid = new QGridLayout;
    grid->setSpacing(12);
    int unlockedCount = 0;
    for (const auto& plant : PlantCatalog::all()) {
        if (!coinManager_.isPlantUnlocked(plant.type)) continue;
        auto* btn = new QPushButton(plant.displayName, dialog);
        btn->setProperty("plantCard", "true");
        btn->setProperty("plantType", plant.type);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(QIcon(PlantImageUtils::loadPlantIcon(plant.iconPath)));
        btn->setIconSize(QSize(76, 76));
        btn->setMinimumSize(132, 116);
        grid->addWidget(btn, unlockedCount / 3, unlockedCount % 3);
        QObject::connect(btn, &QPushButton::clicked, dialog, [this, dialog, type = plant.type]() {
            if (settingsPage_) {
                auto focus = settingsPage_->focusSettings();
                focus.plantType = type;
                settingsPage_->setFocusSettings(focus);
            }
            if (timerRing_) timerRing_->setPlantType(type);
            dialog->close();
        });
        ++unlockedCount;
    }

    if (unlockedCount == 0) {
        auto* empty = new QLabel(QStringLiteral("暂无已解锁植物"), dialog);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet("color:#F7FFF7; font-weight:800;");
        layout->addWidget(empty);
    } else {
        layout->addLayout(grid);
    }

    dialog->adjustSize();
    const QPoint anchor = timerRing_
        ? timerRing_->mapToGlobal(timerRing_->rect().center())
        : mapToGlobal(rect().center());
    QScreen* screen = QApplication::screenAt(anchor);
    const QRect available = screen ? screen->availableGeometry() : QRect(anchor - QPoint(640, 400),
                                                                          QSize(1280, 800));
    const QSize popupSize = dialog->size();
    const int x = qBound(available.left() + 12, anchor.x() - popupSize.width() / 2,
                         available.right() - popupSize.width() - 12);
    const int y = qBound(available.top() + 12, anchor.y() - popupSize.height() / 2,
                         available.bottom() - popupSize.height() - 12);
    dialog->move(x, y);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
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
    if (!settingsPage_) return;
    auto focus = settingsPage_->focusSettings();
    focus.stopwatch = stopwatch;
    settingsPage_->setFocusSettings(focus);
    refreshHomeModeControls();
}

void MainWindow::showHomeModeOptions(bool stopwatch)
{
    setHomeTimerMode(stopwatch);
    if (!homeModeOverlay_) return;
    homeModeReturnFocus_ = stopwatch
        ? static_cast<QWidget*>(homeStopwatchBtn_)
        : static_cast<QWidget*>(homeCountdownBtn_);

    QWidget* host = centralWidget();
    if (host) {
        homeModeOverlay_->setGeometry(host->rect());
    }
    homeModeOverlay_->show();
    homeModeOverlay_->raise();
    if (stackedWidget_) stackedWidget_->setEnabled(false);
    if (sidebarWidget_) sidebarWidget_->setEnabled(false);
    if (homeDeepFocusCheck_) homeDeepFocusCheck_->setFocus(Qt::OtherFocusReason);
}

void MainWindow::hideHomeModeOptions()
{
    if (homeModeOverlay_) {
        homeModeOverlay_->hide();
    }
    if (stackedWidget_) stackedWidget_->setEnabled(true);
    if (sidebarWidget_) sidebarWidget_->setEnabled(true);
    if (homeModeReturnFocus_) {
        homeModeReturnFocus_->setFocus(Qt::OtherFocusReason);
        homeModeReturnFocus_ = nullptr;
    }
}

void MainWindow::showPauseBreakOverlay()
{
    if (!pauseBreakOverlay_) return;
    pauseBreakRemainingSeconds_ = 5 * 60;
    updatePauseBreakText();

    QWidget* host = centralWidget();
    if (host) {
        pauseBreakOverlay_->setGeometry(host->rect());
    }
    pauseBreakOverlay_->show();
    pauseBreakOverlay_->raise();
    if (stackedWidget_) stackedWidget_->setEnabled(false);
    if (sidebarWidget_) sidebarWidget_->setEnabled(false);
    if (pauseBreakContinueBtn_) pauseBreakContinueBtn_->setFocus(Qt::OtherFocusReason);
    pauseBreakTimer_.start(1000);
}

void MainWindow::hidePauseBreakOverlay()
{
    pauseBreakTimer_.stop();
    if (pauseBreakOverlay_) {
        pauseBreakOverlay_->hide();
    }
    if (stackedWidget_) stackedWidget_->setEnabled(true);
    if (sidebarWidget_) sidebarWidget_->setEnabled(true);
}

void MainWindow::resumeFromPauseBreak()
{
    hidePauseBreakOverlay();
    if (controller_.currentState() != FocusController::State::PAUSED) {
        updateUI();
        return;
    }
    focusSession_->resume();
    monitor_.startMonitoring();
    quoteTimer_.start(10000);
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
    if (!settingsPage_ || !homeCountdownBtn_ || !homeStopwatchBtn_) return;

    const auto focus = settingsPage_->focusSettings();
    const bool stopwatch = focus.stopwatch;
    homeCountdownBtn_->setProperty("active", stopwatch ? "false" : "true");
    homeStopwatchBtn_->setProperty("active", stopwatch ? "true" : "false");
    if (homeCountdownModeLabel_) homeCountdownModeLabel_->setProperty("active", stopwatch ? "false" : "true");
    if (homeStopwatchModeLabel_) homeStopwatchModeLabel_->setProperty("active", stopwatch ? "true" : "false");
    homeCountdownBtn_->style()->unpolish(homeCountdownBtn_);
    homeCountdownBtn_->style()->polish(homeCountdownBtn_);
    homeStopwatchBtn_->style()->unpolish(homeStopwatchBtn_);
    homeStopwatchBtn_->style()->polish(homeStopwatchBtn_);
    if (homeCountdownModeLabel_) {
        homeCountdownModeLabel_->style()->unpolish(homeCountdownModeLabel_);
        homeCountdownModeLabel_->style()->polish(homeCountdownModeLabel_);
    }
    if (homeStopwatchModeLabel_) {
        homeStopwatchModeLabel_->style()->unpolish(homeStopwatchModeLabel_);
        homeStopwatchModeLabel_->style()->polish(homeStopwatchModeLabel_);
    }

    if (countdownOptionsWidget_) {
        auto* groupRow = countdownOptionsWidget_->findChild<QWidget*>("groupPlantOptionRow");
        auto* autoExtendRow = countdownOptionsWidget_->findChild<QWidget*>("autoExtendOptionRow");
        if (groupRow) groupRow->setVisible(!stopwatch);
        if (autoExtendRow) autoExtendRow->setVisible(!stopwatch);
        countdownOptionsWidget_->setVisible(true);
        int targetHeight = countdownOptionsWidget_->sizeHint().height();

        countdownOptionsWidget_->setMaximumHeight(targetHeight);
    }
    if (homeDeepFocusCheck_ && homeDeepFocusCheck_->isChecked() != focus.deepFocus) {
        const QSignalBlocker blocker(homeDeepFocusCheck_);
        homeDeepFocusCheck_->setChecked(focus.deepFocus);
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
    if (settingsPage_) {
        auto focus = settingsPage_->focusSettings();
        focus.minutes = static_cast<int>(setup.minutes);
        focus.plantType = setup.plantType;
        focus.tagId = setup.tagId;
        focus.stopwatch = setup.stopwatch;
        focus.deepFocus = setup.deepFocus;
        focus.allowPause = UserPreferences::instance().allowPause();
        settingsPage_->setFocusSettings(focus);
    }
    if (timerRing_) timerRing_->setSelectedMinutes(setup.minutes);
    if (homeDeepFocusCheck_) homeDeepFocusCheck_->setChecked(setup.deepFocus);
    if (homeAutoExtendCheck_) homeAutoExtendCheck_->setChecked(setup.autoExtend);
    refreshHomeModeControls();
    refreshHomeTagBadge();
}

void MainWindow::persistFocusSetup()
{
    const auto focus = settingsPage_ ? settingsPage_->focusSettings()
                                     : SettingsPage::FocusSettings{};
    UserPreferences::FocusSetup setup;
    setup.minutes = timerRing_ ? timerRing_->selectedMinutes() : 25;
    setup.plantType = focus.plantType;
    setup.tagId = focus.tagId;
    setup.stopwatch = focus.stopwatch;
    setup.deepFocus = focus.deepFocus;
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
        if (watched == homeCountdownModeLabel_) {
            showHomeModeOptions(false);
            return true;
        }
        if (watched == homeStopwatchModeLabel_) {
            showHomeModeOptions(true);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (controller_.currentState() == FocusController::State::RUNNING ||
        controller_.currentState() == FocusController::State::PAUSED ||
        controller_.currentState() == FocusController::State::WARNING) {
        const bool confirmed = DialogPresenter::confirmDanger(this,
            QStringLiteral("确认退出"),
            QStringLiteral("正在专注中！退出将会导致小树枯萎。\n确定要退出吗？"));
        if (confirmed) {
            focusSession_->abandon();
            event->accept();
        } else { event->ignore(); }
    } else { event->accept(); }
}

void MainWindow::onStartClicked()
{
    hideHomeModeOptions();

    const auto focus = settingsPage_->focusSettings();
    const bool isCountdown = !focus.stopwatch;
    auto mode = !isCountdown
        ? FocusController::TimerMode::STOPWATCH : FocusController::TimerMode::COUNTDOWN;
    auto fm = focus.deepFocus
        ? FocusController::FocusMode::STRICT_MODE : FocusController::FocusMode::GENTLE_MODE;

    uint32_t minutes = timerRing_->selectedMinutes();
    FocusSessionCoordinator::StartRequest request;
    request.plantType = focus.plantType;
    request.plannedMinutes = minutes;
    request.timerMode = mode;
    request.focusMode = fm;
    request.tagId = focus.tagId;
    request.allowPause = isCountdown && focus.allowPause;
    request.autoExtend = isCountdown && homeAutoExtendCheck_ && homeAutoExtendCheck_->isChecked();
    if (!focusSession_->start(request)) {
        DialogPresenter::warning(this, QStringLiteral("无法开始专注"),
            QStringLiteral("专注记录未能安全创建，请稍后重试。"));
        return;
    }
    persistFocusSetup();
    timerRing_->setPlantType(focus.plantType);
    timerRing_->setDisplaySeconds(isCountdown ? minutes * 60 : 0, !isCountdown);
    monitor_.startMonitoring();
    quoteTimer_.start(10000);
    timerRing_->setOath(focus.oath);
    switchPage(0);
    updateUI();
}

void MainWindow::onPauseResumeClicked()
{
    if (controller_.currentState() == FocusController::State::RUNNING) {
        focusSession_->pause(); monitor_.stopMonitoring(); quoteTimer_.stop();
        showPauseBreakOverlay();
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
    startBtn_->setVisible(state.idle);
    startBtn_->setEnabled(state.idle);
    pauseBtn_->setVisible(state.canPause && !state.paused);
    abandonBtn_->setVisible(state.active && !state.paused);
    pauseBtn_->setEnabled(state.canPause);
    abandonBtn_->setEnabled(state.active && !state.paused);
    if (state.paused && pauseBreakOverlay_ && !pauseBreakOverlay_->isVisible()) {
        showPauseBreakOverlay();
    }
    if (homeCountdownBtn_) homeCountdownBtn_->setEnabled(state.idle);
    if (homeStopwatchBtn_) homeStopwatchBtn_->setEnabled(state.idle);
    if (homeAllowPauseCheck_) homeAllowPauseCheck_->setEnabled(state.idle);
    if (homeDeepFocusCheck_) homeDeepFocusCheck_->setEnabled(state.idle);
    if (homeGroupPlantCheck_) homeGroupPlantCheck_->setEnabled(false);
    if (homeAutoExtendCheck_) homeAutoExtendCheck_->setEnabled(state.idle);
    if (settingsPage_) settingsPage_->setAllowPauseControlEnabled(state.idle);
    if (sidebarWidget_) sidebarWidget_->setVisible(!state.active);

    if (state.idle) {
        const auto focus = settingsPage_ ? settingsPage_->focusSettings()
                                         : SettingsPage::FocusSettings{};
        const bool stopwatch = focus.stopwatch;
        timerRing_->setDisplaySeconds(0, stopwatch);
        timerRing_->setOath(QString());
        timerRing_->setPlantType(focus.plantType);
        refreshHomeModeControls();
    }

    abandonBtn_->setText(QStringLiteral("放弃"));
    if (state.running) pauseBtn_->setText(QStringLiteral("暂停"));
    else if (state.paused) pauseBtn_->setText(QStringLiteral("继续"));

    updateTrayStatus();
}

void MainWindow::refreshGarden()
{
    auto snapshot = dashboardSnapshots_.forest();
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
    menu->addAction(QStringLiteral("退出"), qApp, &QApplication::quit);
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
        trayPauseAction_->setVisible(state.active);
        trayPauseAction_->setEnabled(state.paused || state.canPause);
        trayPauseAction_->setText(state.paused ? QStringLiteral("继续专注")
                                                : QStringLiteral("暂停专注"));
    }
    if (trayAbandonAction_) {
        trayAbandonAction_->setVisible(state.active && !state.paused);
        trayAbandonAction_->setEnabled(state.active && !state.paused);
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

    qApp->setStyleSheet(AppStyle::styleSheet(options.fontScalePercent, options.highContrast));
    if (sidebarNavigation_) sidebarNavigation_->setReducedMotion(options.reducedMotion);
    if (guardianDashboard_) guardianDashboard_->setReducedMotion(options.reducedMotion);
    if (settingsPage_) settingsPage_->setReducedMotion(options.reducedMotion);
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
    const bool completed = static_cast<FocusRecordStatus>(status) == FocusRecordStatus::Success;
    if (outcome.alreadyApplied && !completed) return;
    showFocusResult(recordId, outcome, completed);
}

void MainWindow::showFocusResult(uint32_t recordId, const FocusResultService::Outcome& outcome,
                                 bool completed)
{
    const auto record = db_.readById(recordId);
    if (!record) return;

    const FocusRecord& focus = record.value();
    const QString plantName = PlantCatalog::isKnown(focus.plantType)
        ? PlantCatalog::byType(focus.plantType).displayName : QStringLiteral("当前植物");
    const uint32_t minutes = (focus.actualSeconds + 59) / 60;

    QDialog dialog(this);
    dialog.setWindowTitle(completed ? QStringLiteral("专注完成") : QStringLiteral("本次专注已结束"));
    DialogPresenter::prepare(dialog);
    dialog.setMinimumWidth(410);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(30, 26, 30, 24);
    layout->setSpacing(14);

    auto* title = new QLabel(completed ? QStringLiteral("做得好，专注已经完成")
                                        : QStringLiteral("这次先停在这里"), &dialog);
    title->setStyleSheet("font-size:22px; font-weight:700; color:#245543;");
    layout->addWidget(title);

    auto* summary = new QLabel(&dialog);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size:15px; line-height:1.45; color:#48675B;");
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
    DialogPresenter::setSecondary(closeButton);
    actions->addWidget(closeButton);
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    bool startBreak = false;
    bool startAgain = false;
    if (completed) {
        auto* restButton = new QPushButton(QStringLiteral("开始 5 分钟休息"), &dialog);
        auto* againButton = new QPushButton(QStringLiteral("再专注一次"), &dialog);
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
    dialog.exec();

    if (startBreak) {
        QDialog restDialog(this);
        restDialog.setWindowTitle(QStringLiteral("短暂休息"));
        DialogPresenter::prepare(restDialog);
        auto* restLayout = new QVBoxLayout(&restDialog);
        restLayout->setContentsMargins(34, 30, 34, 28);
        auto* restTitle = new QLabel(QStringLiteral("休息 5 分钟"), &restDialog);
        restTitle->setStyleSheet("font-size:22px; font-weight:700; color:#245543;");
        auto* remaining = new QLabel(&restDialog);
        remaining->setAlignment(Qt::AlignCenter);
        remaining->setStyleSheet("font-size:38px; color:#48A47E;");
        auto* resumeButton = new QPushButton(QStringLiteral("返回专注"), &restDialog);
        DialogPresenter::setPrimary(resumeButton);
        restLayout->addWidget(restTitle);
        restLayout->addWidget(new QLabel(QStringLiteral("离开屏幕、喝水或活动一下。休息不计入专注记录和奖励。"), &restDialog));
        restLayout->addWidget(remaining);
        restLayout->addWidget(resumeButton, 0, Qt::AlignRight);

        int seconds = 5 * 60;
        const auto updateBreakText = [&]() {
            remaining->setText(QStringLiteral("%1:%2")
                .arg(seconds / 60, 2, 10, QLatin1Char('0'))
                .arg(seconds % 60, 2, 10, QLatin1Char('0')));
        };
        updateBreakText();
        QTimer restTimer(&restDialog);
        QObject::connect(&restTimer, &QTimer::timeout, &restDialog, [&]() {
            if (--seconds <= 0) {
                restDialog.accept();
                return;
            }
            updateBreakText();
        });
        QObject::connect(resumeButton, &QPushButton::clicked, &restDialog, &QDialog::accept);
        restTimer.start(1000);
        restDialog.exec();
    }
    if (startAgain) QTimer::singleShot(0, this, &MainWindow::onStartClicked);
}

