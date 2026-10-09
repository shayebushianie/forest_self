#include "ui/AppStyle.h"
#include "ui/ForestDashboardWidget.h"
#include "ui/PlantImageUtils.h"
#include "ui/PaintedActionButton.h"
#include "config/PlantCatalog.h"
#include "core/FocusRecordSemantics.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QImage>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QMap>
#include <QSet>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QStyle>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

QString finalPlantPath(uint32_t plantType)
{
    return PlantCatalog::byType(plantType).iconPath;
}

QPixmap grayscalePixmap(const QPixmap& source)
{
    QImage img = source.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QColor color = QColor::fromRgba(line[x]);
            const int gray = qGray(color.rgb());
            line[x] = qRgba(gray, gray, gray, color.alpha());
        }
    }
    return QPixmap::fromImage(img);
}

qreal normalizedHash(uint32_t seed)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<qreal>(seed % 10000) / 10000.0;
}

}

ForestDashboardWidget::ForestDashboardWidget(QWidget* parent)
    : QWidget(parent), selectedDate_(QDate::currentDate())
{
    setMinimumSize(900, 720);
    setAttribute(Qt::WA_StyledBackground, true);
    setMouseTracking(true);
    islandNavigation_ = new QWidget(this);
    islandNavigation_->setObjectName(QStringLiteral("forestIslandNavigation"));
    auto* layout = new QHBoxLayout(islandNavigation_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    islandSummary_ = new QLabel(islandNavigation_);
    islandSummary_->setObjectName(QStringLiteral("forestIslandSummary"));
    islandSummary_->setWordWrap(true);
    islandPageLabel_ = new QLabel(islandNavigation_);
    islandPageLabel_->setObjectName(QStringLiteral("forestIslandPage"));
    islandPageLabel_->setAlignment(Qt::AlignCenter);
    islandPageLabel_->setMinimumWidth(66);
    auto makeArrow = [this](const QString& name, const QString& text, QStyle::StandardPixmap icon) {
        auto* button = new QToolButton(islandNavigation_);
        button->setObjectName(name);
        button->setIcon(style()->standardIcon(icon));
        button->setToolTip(text);
        button->setAccessibleName(text);
        button->setFixedSize(32, 32);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    islandPrevious_ = makeArrow(QStringLiteral("forestIslandPrevious"), QStringLiteral("上一页植物"), QStyle::SP_ArrowBack);
    islandNext_ = makeArrow(QStringLiteral("forestIslandNext"), QStringLiteral("下一页植物"), QStyle::SP_ArrowForward);
    const QString mutedColor = AppStyle::color(AppStyle::Color::Muted).name();
    const QString surfaceColor = AppStyle::color(AppStyle::Color::Surface).name();
    const QString borderColor = AppStyle::color(AppStyle::Color::Border).name();
    const QString selectionColor = AppStyle::color(AppStyle::Color::Selection).name();
    const QString primaryColor = AppStyle::color(AppStyle::Color::Primary).name();
    islandNavigation_->setStyleSheet(QStringLiteral(
        "QWidget#forestIslandNavigation { background: transparent; border: none; }"
        "QLabel { color: %1; background: transparent; font-size: 12px; }"
        "QToolButton { color: %2; background: %3; border: 1px solid %4; border-radius: 8px; }"
        "QToolButton:hover, QToolButton:pressed { background: %5; border-color: %6; }"
        "QToolButton:focus { border: 2px solid %6; }"
        "QToolButton:disabled { color: %1; background: %3; border-color: %4; }")
        .arg(mutedColor).arg(primaryColor).arg(surfaceColor).arg(borderColor)
        .arg(selectionColor).arg(primaryColor));
    layout->addWidget(islandSummary_, 1);
    layout->addWidget(islandPrevious_);
    layout->addWidget(islandPageLabel_);
    layout->addWidget(islandNext_);
    connect(islandPrevious_, &QToolButton::clicked, this, [this] { changeIslandPage(-1); });
    connect(islandNext_, &QToolButton::clicked, this, [this] { changeIslandPage(1); });
    updateIslandNavigation();
}

QRectF ForestDashboardWidget::islandPanelRect() const
{
    const QRectF content = QRectF(rect()).adjusted(26, 22, -26, -24);
    return QRectF(content.left(), content.top() + 92, content.width() * 0.60,
                  std::clamp<qreal>(height() * 0.40, 300, 420));
}

qreal ForestDashboardWidget::islandNavigationHeight() const
{
    const int lineHeight = QFontMetrics(islandSummary_->font()).height();
    return std::max<qreal>(48, lineHeight * 2 + 12);
}

void ForestDashboardWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const QRectF panel = islandPanelRect();
    const qreal navigationHeight = islandNavigationHeight();
    islandNavigation_->setGeometry(QRectF(panel.left() + 20, panel.bottom() - navigationHeight,
                                         panel.width() - 40, navigationHeight).toAlignedRect());
    islandPlantHitRects_.clear();
    hoveredRecordId_ = std::numeric_limits<uint32_t>::max();
    hoveredHeaderAction_ = -1;
    setToolTip({});
}

void ForestDashboardWidget::updateIslandNavigation()
{
    const auto& data = buildData();
    const int count = static_cast<int>(data.islandPlants.size());
    const int pages = std::max(1, (count + IslandPageCapacity - 1) / IslandPageCapacity);
    islandPage_ = std::clamp(islandPage_, 0, pages - 1);
    const int begin = islandPage_ * IslandPageCapacity;
    const int end = std::min(begin + IslandPageCapacity, count);
    islandSummary_->setText(count == 0 ? QStringLiteral("%1 · 暂无专注植物").arg(data.rangeText)
        : QStringLiteral("%1 · 第 %2–%3 棵 / 共 %4 棵\n其他页 %5 棵")
            .arg(data.rangeText).arg(begin + 1).arg(end).arg(count).arg(count - (end - begin)));
    islandPageLabel_->setText(QStringLiteral("%1 / %2 页").arg(islandPage_ + 1).arg(pages));
    islandPrevious_->setEnabled(islandPage_ > 0);
    islandNext_->setEnabled(islandPage_ + 1 < pages);
    islandPlantHitRects_.clear();
    hoveredRecordId_ = std::numeric_limits<uint32_t>::max();
    setToolTip({});
}

void ForestDashboardWidget::changeIslandPage(int delta)
{
    islandPage_ += delta;
    updateIslandNavigation();
    update();
}

void ForestDashboardWidget::setRecords(std::vector<FocusRecord> records)
{
    allRecords_ = std::move(records);
    setRecordFilter(recordFilter_);
}

void ForestDashboardWidget::setRecordFilter(const RecordFilter& filter)
{
    recordFilter_ = filter;
    records_.clear();
    records_.reserve(allRecords_.size());
    for (const FocusRecord& record : allRecords_) {
        if (recordFilter_.date.isValid() &&
            QDateTime::fromSecsSinceEpoch(static_cast<qint64>(record.startTimestamp), Qt::LocalTime).date() != recordFilter_.date) {
            continue;
        }
        if (recordFilter_.tagId != std::numeric_limits<uint32_t>::max() && record.tagId != recordFilter_.tagId) continue;
        if (recordFilter_.plantType != std::numeric_limits<uint32_t>::max() && record.plantType != recordFilter_.plantType) continue;
        if (recordFilter_.terminalState == 0 && record.status != FocusRecordStatus::Success) continue;
        if (recordFilter_.terminalState == 1 &&
            (record.status != FocusRecordStatus::Abandoned || isInterruptedFocus(record))) continue;
        if (recordFilter_.terminalState == 2 && record.status != FocusRecordStatus::Failed) continue;
        if (recordFilter_.terminalState == 3 && !isInterruptedFocus(record)) continue;
        records_.push_back(record);
    }
    statisticsIndex_.rebuild(records_);
    cachedData_.reset();
    islandPage_ = 0;
    updateIslandNavigation();
    update();
}

void ForestDashboardWidget::setTagNames(const QMap<uint32_t, QString>& tagNames)
{
    tagNames_ = tagNames;
    cachedData_.reset();
    updateIslandNavigation();
    update();
}

void ForestDashboardWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF pos = event->position();
    for (auto it = islandPlantHitRects_.crbegin(); it != islandPlantHitRects_.crend(); ++it) {
        if (it->first.contains(pos)) {
            emit focusRecordRequested(it->second);
            return;
        }
    }
    for (int i = 0; i < periodTabRects_.size(); ++i) {
        if (!periodTabRects_[i].contains(pos)) continue;
        periodMode_ = static_cast<PeriodMode>(i);
        cachedData_.reset();
        islandPage_ = 0;
        updateIslandNavigation();
        update();
        return;
    }
    if (prevDateRect_.contains(pos)) {
        switch (periodMode_) {
        case PeriodMode::Day: selectedDate_ = selectedDate_.addDays(-1); break;
        case PeriodMode::Week: selectedDate_ = selectedDate_.addDays(-7); break;
        case PeriodMode::Month: selectedDate_ = selectedDate_.addMonths(-1); break;
        case PeriodMode::Year: selectedDate_ = selectedDate_.addYears(-1); break;
        }
        cachedData_.reset();
        islandPage_ = 0;
        updateIslandNavigation();
        update();
        return;
    }
    if (nextDateRect_.contains(pos)) {
        switch (periodMode_) {
        case PeriodMode::Day: selectedDate_ = selectedDate_.addDays(1); break;
        case PeriodMode::Week: selectedDate_ = selectedDate_.addDays(7); break;
        case PeriodMode::Month: selectedDate_ = selectedDate_.addMonths(1); break;
        case PeriodMode::Year: selectedDate_ = selectedDate_.addYears(1); break;
        }
        cachedData_.reset();
        islandPage_ = 0;
        updateIslandNavigation();
        update();
        return;
    }
    if (settingsRect_.contains(pos)) {
        emit settingsRequested();
        return;
    }
    if (overviewRect_.contains(pos)) {
        emit overviewRequested();
        return;
    }
    if (filtersRect_.contains(pos)) {
        emit filtersRequested();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ForestDashboardWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    uint32_t hovered = std::numeric_limits<uint32_t>::max();
    QString hint;
    for (auto it = islandPlantHitRects_.crbegin(); it != islandPlantHitRects_.crend(); ++it) {
        if (!it->first.contains(pos)) continue;
        hovered = it->second;
        const auto record = std::find_if(records_.cbegin(), records_.cend(),
            [hovered](const FocusRecord& value) { return value.recordId == hovered; });
        if (record != records_.cend()) {
            hint = QStringLiteral("%1 · %2\n%3 · %4 分 %5 秒")
                .arg(PlantCatalog::byType(record->plantType).displayName,
                     FocusRecordSemantics::statusText(*record),
                     QDateTime::fromSecsSinceEpoch(record->startTimestamp, Qt::LocalTime)
                         .toString(QStringLiteral("M月d日 HH:mm")))
                .arg(record->actualSeconds / 60).arg(record->actualSeconds % 60);
        }
        break;
    }
    if (hoveredRecordId_ != hovered) {
        hoveredRecordId_ = hovered;
        update();
    }
    const bool onPlant = hovered != std::numeric_limits<uint32_t>::max();
    const bool onAction = overviewRect_.contains(pos) || filtersRect_.contains(pos) ||
        settingsRect_.contains(pos) || prevDateRect_.contains(pos) || nextDateRect_.contains(pos) ||
        std::any_of(periodTabRects_.cbegin(), periodTabRects_.cend(),
                    [&pos](const QRectF& rect) { return rect.contains(pos); });
    int headerAction = -1;
    if (overviewRect_.contains(pos)) headerAction = 0;
    else if (prevDateRect_.contains(pos)) headerAction = 5;
    else if (nextDateRect_.contains(pos)) headerAction = 6;
    else if (filtersRect_.contains(pos)) headerAction = 7;
    else if (settingsRect_.contains(pos)) headerAction = 8;
    else {
        for (int i = 0; i < periodTabRects_.size(); ++i) {
            if (periodTabRects_[i].contains(pos)) {
                headerAction = i + 1;
                break;
            }
        }
    }
    if (hoveredHeaderAction_ != headerAction) {
        hoveredHeaderAction_ = headerAction;
        update();
    }
    if (prevDateRect_.contains(pos)) hint = QStringLiteral("上一个时间范围");
    if (nextDateRect_.contains(pos)) hint = QStringLiteral("下一个时间范围");
    if (overviewRect_.contains(pos)) hint = QStringLiteral("查看总览与项目标签");
    if (filtersRect_.contains(pos)) hint = QStringLiteral("筛选森林记录");
    if (settingsRect_.contains(pos)) hint = QStringLiteral("打开系统设置");
    setToolTip(hint);
    setCursor(onPlant || onAction ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void ForestDashboardWidget::leaveEvent(QEvent* event)
{
    hoveredRecordId_ = std::numeric_limits<uint32_t>::max();
    hoveredHeaderAction_ = -1;
    setToolTip({});
    unsetCursor();
    update();
    QWidget::leaveEvent(event);
}

const ForestDashboardWidget::DashboardData& ForestDashboardWidget::buildData() const
{
    if (!cachedData_) {
        cachedData_ = FocusStatisticsQuery::query(statisticsIndex_, tagNames_, selectedDate_,
            static_cast<FocusStatisticsQuery::Period>(periodMode_));
    }
    return *cachedData_;
}
void ForestDashboardWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.fillRect(rect(), AppStyle::color(AppStyle::Color::Workspace));

    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10)));
    const DashboardData& data = buildData();

    const qreal margin = 26.0;
    QRectF content = QRectF(rect()).adjusted(margin, 22, -margin, -24);
    QRectF header(content.left(), content.top(), content.width(), 74);
    drawHeader(painter, header);

    qreal y = header.bottom() + 18;
    qreal topHeight = islandPanelRect().height();
    qreal gap = 18;
    const QRectF islandRect = islandPanelRect();
    QRectF overviewRect(islandRect.right() + gap, y, content.right() - islandRect.right() - gap, topHeight);
    drawIslandPanel(painter, islandRect, data);
    drawOverviewCards(painter, overviewRect, data);

    y += topHeight + 22;
    qreal bottomHeight = std::min<qreal>(700, content.bottom() - y);
    qreal cardGap = 18;
    qreal firstW = (content.width() - cardGap * 2) * 0.42;
    qreal secondW = (content.width() - cardGap * 2) * 0.29;
    QRectF timeRect(content.left(), y, firstW, bottomHeight);
    QRectF projectRect(timeRect.right() + cardGap, y, secondW, bottomHeight);
    QRectF treeRect(projectRect.right() + cardGap, y, content.right() - projectRect.right() - cardGap, bottomHeight);
    drawTimeCard(painter, timeRect, data);
    drawProjectCard(painter, projectRect, data);
    drawTreeCard(painter, treeRect, data);
    syncKeyboardActions();
}

void ForestDashboardWidget::syncKeyboardActions()
{
    int index = 0;
    auto bind = [this, &index](const QRectF& area, const QString& name, std::function<void()> action) {
        if (index == keyboardActions_.size()) {
            auto* button = new PaintedActionButton(this);
            button->setObjectName(QStringLiteral("forestKeyboardAction%1").arg(index));
            keyboardActions_.append(button);
        }
        keyboardActions_[index]->bind(area, name, std::move(action));
        if (index > 0) QWidget::setTabOrder(keyboardActions_[index - 1], keyboardActions_[index]);
        ++index;
    };
    auto clickArea = [this](const QRectF& area) {
        QMouseEvent event(QEvent::MouseButtonPress, area.center(), area.center(),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        mousePressEvent(&event);
    };
    bind(overviewRect_, QStringLiteral("总览与项目标签"), [this] { emit overviewRequested(); });
    const QStringList periods = {QStringLiteral("日统计"), QStringLiteral("周统计"),
                                 QStringLiteral("月统计"), QStringLiteral("年统计")};
    for (int i = 0; i < periodTabRects_.size(); ++i) {
        const QRectF area = periodTabRects_[i];
        bind(area, periods[i], [clickArea, area] { clickArea(area); });
    }
    bind(prevDateRect_, QStringLiteral("上一个时间范围"), [clickArea, area = prevDateRect_] { clickArea(area); });
    bind(nextDateRect_, QStringLiteral("下一个时间范围"), [clickArea, area = nextDateRect_] { clickArea(area); });
    bind(filtersRect_, QStringLiteral("筛选森林记录"), [this] { emit filtersRequested(); });
    bind(settingsRect_, QStringLiteral("系统设置"), [this] { emit settingsRequested(); });
    for (const auto& hit : islandPlantHitRects_) {
        const uint32_t id = hit.second;
        const auto record = std::find_if(records_.cbegin(), records_.cend(),
            [id](const FocusRecord& value) { return value.recordId == id; });
        const QString name = record == records_.cend() ? QStringLiteral("专注记录")
            : QStringLiteral("%1 · %2 · %3")
                .arg(PlantCatalog::byType(record->plantType).displayName,
                     FocusRecordSemantics::statusText(*record),
                     QDateTime::fromSecsSinceEpoch(record->startTimestamp, Qt::LocalTime)
                         .toString(QStringLiteral("M月d日 HH:mm")));
        bind(hit.first, name, [this, id] { emit focusRecordRequested(id); });
    }
    for (int i = index; i < keyboardActions_.size(); ++i) keyboardActions_[i]->hide();
    if (index > 0) QWidget::setTabOrder(keyboardActions_[index - 1], islandPrevious_);
}

void ForestDashboardWidget::drawHeader(QPainter& painter, const QRectF& rect) const
{
    painter.save();
    periodTabRects_.clear();
    prevDateRect_ = QRectF();
    nextDateRect_ = QRectF();
    settingsRect_ = QRectF();
    overviewRect_ = QRectF();
    filtersRect_ = QRectF();

    const QColor textColor = AppStyle::color(AppStyle::Color::Text);
    const QColor mutedColor = AppStyle::color(AppStyle::Color::Muted);
    const QColor surfaceColor = AppStyle::color(AppStyle::Color::Surface);
    const QColor primaryColor = AppStyle::color(AppStyle::Color::Primary);
    const QColor selectionColor = AppStyle::color(AppStyle::Color::Selection);
    const QColor borderColor = AppStyle::color(AppStyle::Color::Border);

    const QRectF titleRect(rect.left(), rect.top() + 2, 112, 28);
    painter.setPen(textColor);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 18, QFont::Bold)));
    drawTextFit(painter, titleRect, QStringLiteral("我的森林"), Qt::AlignLeft | Qt::AlignVCenter, 10);
    painter.setPen(mutedColor);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold)));
    const QRectF recordCountRect(rect.left(), rect.top() + 34, 112, 20);
    drawTextFit(painter, recordCountRect, QStringLiteral("%1 条记录").arg(records_.size()),
                Qt::AlignLeft | Qt::AlignVCenter, 8);

    overviewRect_ = QRectF(rect.left() + 122, rect.top() + 10, 74, 40);
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(hoveredHeaderAction_ == 0 ? selectionColor : surfaceColor);
    painter.drawRoundedRect(overviewRect_, 8, 8);
    painter.setPen(primaryColor);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::DemiBold)));
    drawTextFit(painter, overviewRect_.adjusted(5, 2, -5, -2), QStringLiteral("总览"),
                Qt::AlignCenter, 9);

    const qreal tabWidth = std::clamp(rect.width() * 0.17, 168.0, 220.0);
    QRectF tabs(overviewRect_.right() + 12, rect.top() + 8, tabWidth, 42);
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(surfaceColor);
    painter.drawRoundedRect(tabs, 8, 8);

    const QStringList names = {QStringLiteral("日"), QStringLiteral("周"),
                               QStringLiteral("月"), QStringLiteral("年")};
    const qreal tabW = tabs.width() / names.size();
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::DemiBold)));
    const int activeIndex = static_cast<int>(periodMode_);
    for (int i = 0; i < names.size(); ++i) {
        QRectF tab(tabs.left() + tabW * i + 2, tabs.top() + 2, tabW - 4, tabs.height() - 4);
        periodTabRects_.push_back(tab);
        const bool selected = i == activeIndex;
        const bool hovered = hoveredHeaderAction_ == i + 1;
        if (selected || hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(selectionColor);
            painter.drawRoundedRect(tab, 6, 6);
        }
        painter.setPen(selected || hovered ? primaryColor : mutedColor);
        painter.drawText(tab, Qt::AlignCenter, names[i]);
    }

    settingsRect_ = QRectF(rect.right() - 58, rect.top() + 10, 54, 38);
    filtersRect_ = QRectF(settingsRect_.left() - 70, rect.top() + 10, 62, 38);
    const qreal preferredDateWidth = std::clamp(rect.width() * 0.25, 220.0, 330.0);
    const qreal dateLeftLimit = tabs.right() + 16;
    const qreal dateRightLimit = filtersRect_.left() - 16;
    const qreal dateW = std::min(preferredDateWidth, std::max(120.0, dateRightLimit - dateLeftLimit));
    const qreal dateX = std::clamp(rect.center().x() - dateW / 2.0,
                                   dateLeftLimit, dateRightLimit - dateW);
    const QRectF dateRect(dateX, rect.top() + 8, dateW, 42);
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(surfaceColor);
    painter.drawRoundedRect(dateRect, 8, 8);
    painter.setPen(textColor);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 13, QFont::DemiBold)));
    drawTextFit(painter, QRectF(dateRect.left() + 40, dateRect.top(), dateRect.width() - 80,
                                dateRect.height()),
                FocusStatisticsQuery::periodLabel(
                    selectedDate_, static_cast<FocusStatisticsQuery::Period>(periodMode_)),
                Qt::AlignCenter, 9);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 23, QFont::Bold)));
    prevDateRect_ = QRectF(dateRect.left() + 6, dateRect.top(), 32, dateRect.height());
    nextDateRect_ = QRectF(dateRect.right() - 38, dateRect.top(), 32, dateRect.height());
    painter.setPen(Qt::NoPen);
    if (hoveredHeaderAction_ == 5) {
        painter.setBrush(selectionColor);
        painter.drawRoundedRect(prevDateRect_, 6, 6);
    }
    if (hoveredHeaderAction_ == 6) {
        painter.setBrush(selectionColor);
        painter.drawRoundedRect(nextDateRect_, 6, 6);
    }
    painter.setPen(primaryColor);
    drawTextFit(painter, prevDateRect_, QStringLiteral("<"), Qt::AlignCenter, 11);
    drawTextFit(painter, nextDateRect_, QStringLiteral(">"), Qt::AlignCenter, 11);

    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold)));
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(hoveredHeaderAction_ == 8 ? selectionColor : surfaceColor);
    painter.drawRoundedRect(settingsRect_, 8, 8);
    painter.setPen(hoveredHeaderAction_ == 8 ? primaryColor : mutedColor);
    drawTextFit(painter, settingsRect_, QStringLiteral("设置"), Qt::AlignCenter, 8);

    const bool filterActive = recordFilter_.isActive();
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(filterActive || hoveredHeaderAction_ == 7 ? selectionColor : surfaceColor);
    painter.drawRoundedRect(filtersRect_, 8, 8);
    painter.setPen(filterActive || hoveredHeaderAction_ == 7 ? primaryColor : mutedColor);
    drawTextFit(painter, filtersRect_, QStringLiteral("筛选"), Qt::AlignCenter, 8);

    painter.setPen(QPen(borderColor, 1));
    painter.drawLine(QPointF(rect.left(), rect.bottom() - 1),
                     QPointF(rect.right(), rect.bottom() - 1));
    painter.restore();
}

void ForestDashboardWidget::drawIslandPanel(QPainter& painter, const QRectF& rect, const DashboardData& data) const
{
    painter.save();
    drawCard(painter, rect);

    QRectF islandBounds = rect.adjusted(22, 26, -22, -88);
    QPointF center(islandBounds.center().x() - 8, islandBounds.center().y());
    const qreal w = std::min({islandBounds.width() * 0.80, 540.0,
                             (rect.height() - 106.0) / (0.52 * 1.22)});
    const qreal h = w * 0.52;
    const qreal topY = islandBounds.top();
    const qreal depth = h * 0.22;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(25, 75, 65, 62));
    painter.drawEllipse(QRectF(center.x() - w * 0.55, topY + h * 0.59, w * 1.10, h * 0.40));

    QPolygonF top;
    top << QPointF(center.x(), topY)
        << QPointF(center.x() + w / 2, topY + h / 2)
        << QPointF(center.x(), topY + h)
        << QPointF(center.x() - w / 2, topY + h / 2);

    const auto curveTo = [](QPainterPath& path, const QPointF& control1,
                            const QPointF& control2, const QPointF& end) {
        path.cubicTo(control1, control2, end);
    };

    QPainterPath grassPath;
    grassPath.moveTo(top[0]);
    curveTo(grassPath, QPointF(center.x() + w * 0.12, topY + h * 0.015),
            QPointF(center.x() + w * 0.42, topY + h * 0.30), top[1]);
    curveTo(grassPath, QPointF(center.x() + w * 0.51, topY + h * 0.53),
            QPointF(center.x() + w * 0.16, topY + h * 0.92), top[2]);
    curveTo(grassPath, QPointF(center.x() - w * 0.16, topY + h * 0.92),
            QPointF(center.x() - w * 0.51, topY + h * 0.53), top[3]);
    curveTo(grassPath, QPointF(center.x() - w * 0.42, topY + h * 0.30),
            QPointF(center.x() - w * 0.12, topY + h * 0.015), top[0]);
    grassPath.closeSubpath();

    QPainterPath lowerLayer = grassPath;
    lowerLayer.translate(0, depth);
    painter.setBrush(QColor("#155747"));
    painter.setPen(Qt::NoPen);
    painter.drawPath(lowerLayer);

    QPainterPath sideLayer;
    sideLayer.moveTo(top[3]);
    curveTo(sideLayer, QPointF(center.x() - w * 0.16, topY + h * 0.92),
            QPointF(center.x() - w * 0.05, topY + h * 0.99), top[2]);
    curveTo(sideLayer, QPointF(center.x() + w * 0.16, topY + h * 0.92),
            QPointF(center.x() + w * 0.51, topY + h * 0.53), top[1]);
    sideLayer.lineTo(top[1] + QPointF(0, depth));
    curveTo(sideLayer, QPointF(center.x() + w * 0.51, topY + h * 0.53 + depth),
            QPointF(center.x() + w * 0.16, topY + h * 0.92 + depth),
            top[2] + QPointF(0, depth));
    curveTo(sideLayer, QPointF(center.x() - w * 0.16, topY + h * 0.92 + depth),
            QPointF(center.x() - w * 0.51, topY + h * 0.53 + depth),
            top[3] + QPointF(0, depth));
    sideLayer.closeSubpath();
    painter.setBrush(QColor("#207B55"));
    painter.drawPath(sideLayer);

    QPainterPath rightSoil;
    rightSoil.moveTo(top[2]);
    curveTo(rightSoil, QPointF(center.x() + w * 0.16, topY + h * 0.92),
            QPointF(center.x() + w * 0.51, topY + h * 0.53), top[1]);
    rightSoil.lineTo(top[1] + QPointF(0, depth));
    curveTo(rightSoil, QPointF(center.x() + w * 0.51, topY + h * 0.53 + depth),
            QPointF(center.x() + w * 0.16, topY + h * 0.92 + depth),
            top[2] + QPointF(0, depth));
    rightSoil.closeSubpath();
    painter.setBrush(QColor("#166347"));
    painter.drawPath(rightSoil);

    const qreal grassLipDepth = depth * 0.30;
    QPainterPath grassLip;
    grassLip.moveTo(top[3]);
    curveTo(grassLip, QPointF(center.x() - w * 0.16, topY + h * 0.92),
            QPointF(center.x() - w * 0.05, topY + h * 0.99), top[2]);
    curveTo(grassLip, QPointF(center.x() + w * 0.16, topY + h * 0.92),
            QPointF(center.x() + w * 0.51, topY + h * 0.53), top[1]);
    grassLip.lineTo(top[1] + QPointF(0, grassLipDepth));
    curveTo(grassLip, QPointF(center.x() + w * 0.51, topY + h * 0.53 + grassLipDepth),
            QPointF(center.x() + w * 0.16, topY + h * 0.92 + grassLipDepth),
            top[2] + QPointF(0, grassLipDepth));
    curveTo(grassLip, QPointF(center.x() - w * 0.16, topY + h * 0.92 + grassLipDepth),
            QPointF(center.x() - w * 0.51, topY + h * 0.53 + grassLipDepth),
            top[3] + QPointF(0, grassLipDepth));
    grassLip.closeSubpath();
    painter.setBrush(QColor("#63AA59"));
    painter.drawPath(grassLip);

    painter.save();
    painter.setPen(QPen(QColor("#135A43"), 1.5, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 15; ++i) {
        const qreal progress = (i + 0.5) / 15.0;
        const qreal x = top[3].x() + (top[1].x() - top[3].x()) * progress;
        const qreal arch = 1.0 - std::abs(progress * 2.0 - 1.0);
        const qreal y = topY + h * (0.50 + arch * 0.50) + depth * 0.64;
        painter.drawLine(QPointF(x - 3, y), QPointF(x + 2, y + 2));
    }
    painter.restore();

    painter.setBrush(QColor("#F7E9B8"));
    painter.setPen(QPen(QColor("#FFEFC7"), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(grassPath);

    painter.save();
    painter.setClipPath(grassPath);

    const QVector<QPointF> ponds = {
        QPointF(center.x() - w * 0.26, topY + h * 0.44),
        QPointF(center.x() + w * 0.25, topY + h * 0.53),
        QPointF(center.x() - w * 0.04, topY + h * 0.73),
        QPointF(center.x() + w * 0.05, topY + h * 0.30)
    };
    for (int index = 0; index < ponds.size(); ++index) {
        drawPond(painter, ponds[index], w * (index == 3 ? 0.075 : 0.10), h * 0.09);
    }
    painter.setPen(QPen(QColor("#E8CC88"), 1.2, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 32; ++i) {
        const qreal x = center.x() + (normalizedHash(37u + i * 19u) - 0.5) * w * 0.82;
        const qreal y = topY + (0.16 + normalizedHash(91u + i * 29u) * 0.68) * h;
        painter.drawLine(QPointF(x - 2.5, y + 1.5), QPointF(x + 2.5, y - 1.5));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#E3D09B"));
    for (int i = 0; i < 8; ++i) {
        const qreal x = center.x() + (normalizedHash(313u + i * 71u) - 0.5) * w * 0.50;
        const qreal y = topY + (0.38 + normalizedHash(401u + i * 43u) * 0.34) * h;
        painter.drawEllipse(QRectF(x - 3, y - 1.8, 6, 3.6));
    }
    painter.restore();

    painter.save();
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 12; ++i) {
        const qreal progress = (i + 0.35) / 12.0;
        const qreal x = top[3].x() + (top[1].x() - top[3].x()) * progress;
        const qreal arch = 1.0 - std::abs(progress * 2.0 - 1.0);
        const qreal y = topY + h * (0.50 + arch * 0.50) + depth * 0.43;
        painter.setBrush(i % 2 == 0 ? QColor("#3B9655") : QColor("#4FAA5A"));
        painter.drawEllipse(QRectF(x - 6, y - 3, 12, 7));
        painter.drawEllipse(QRectF(x - 2, y - 6, 8, 7));
        if (i % 3 == 1) {
            const QColor petal = i % 2 == 0 ? QColor("#FFF3C4") : QColor("#F3C64F");
            painter.setBrush(petal);
            painter.drawEllipse(QRectF(x - 2.5, y - 5.5, 5, 5));
            painter.drawEllipse(QRectF(x - 5.5, y - 2.5, 5, 5));
            painter.drawEllipse(QRectF(x + 0.5, y - 2.5, 5, 5));
            painter.setBrush(QColor("#D89A43"));
            painter.drawEllipse(QRectF(x - 1, y - 1, 2, 2));
        }
    }
    painter.restore();

    struct PositionedPlant {
        const IslandPlant* plant = nullptr;
        QPointF base;
        qreal scale = 1.0;
    };

    QVector<const IslandPlant*> placementOrder;
    placementOrder.reserve(data.islandPlants.size());
    for (const auto& plant : data.islandPlants) {
        placementOrder.push_back(&plant);
    }
    std::sort(placementOrder.begin(), placementOrder.end(),
              [](const IslandPlant* a, const IslandPlant* b) {
                  return a->recordId > b->recordId;
              });

    const int begin = islandPage_ * IslandPageCapacity;
    const int end = std::min(begin + IslandPageCapacity, static_cast<int>(placementOrder.size()));
    const qreal densityScale = end - begin > 3 ? 0.65 : 0.85;
    QVector<PositionedPlant> positioned;
    positioned.reserve(end - begin);

    auto islandBase = [&](qreal u, qreal v) {
        return QPointF(center.x() + (u - v) * w * 0.46,
                       topY + (u + v) * h * 0.50 + h * 0.02);
    };

    // Stable, lightly jittered slots guarantee that every record on a page is reachable.
    const int slotOrder[] = {4, 2, 6, 0, 8, 1, 3, 5, 7};
    for (int index = begin; index < end; ++index) {
        const IslandPlant* plant = placementOrder[index];
        const int slot = slotOrder[index - begin];
        const qreal u = 0.22 + (slot % 3) * 0.28
            + (normalizedHash(plant->placementSeed) - 0.5) * 0.035;
        const qreal v = 0.22 + (slot / 3) * 0.28
            + (normalizedHash(plant->placementSeed ^ 0x85EBCA6Bu) - 0.5) * 0.035;
        const qreal scale = (w / 390.0) * plant->scale * densityScale;
        QPointF base = islandBase(u, v);
        base.setY(std::max(base.y(), rect.top() + 72.0 * scale + 2));
        positioned.push_back({plant, base, scale});
    }

    std::sort(positioned.begin(), positioned.end(),
              [](const PositionedPlant& a, const PositionedPlant& b) {
                  return a.base.y() < b.base.y();
              });

    islandPlantHitRects_.clear();
    // Keep plant images complete even when their canopies reach the island boundary.
    for (const auto& plant : positioned) {
        if (plant.plant->recordId == hoveredRecordId_) {
            QColor hoverFill = AppStyle::color(AppStyle::Color::Selection);
            hoverFill.setAlpha(112);
            painter.setPen(QPen(AppStyle::color(AppStyle::Color::Primary), 2));
            painter.setBrush(hoverFill);
            painter.drawEllipse(QRectF(plant.base.x() - 29 * plant.scale,
                                      plant.base.y() - 10 * plant.scale,
                                      58 * plant.scale, 20 * plant.scale));
        }
        drawPlantImage(painter, plant.base, plant.scale,
                       plant.plant->plantType, plant.plant->abandoned);
        const qreal hitWidth = 58.0 * plant.scale;
        const qreal hitHeight = 76.0 * plant.scale;
        islandPlantHitRects_.push_back({
            QRectF(plant.base.x() - hitWidth * 0.5, plant.base.y() - 68.0 * plant.scale,
                   hitWidth, hitHeight),
            plant.plant->recordId
        });
    }

    painter.setPen(QPen(QColor(255, 255, 255, 150), 1));
    for (int i = 0; i < 10; ++i) {
        QPointF a(top[3].x() + 18 + i * (w - 40) / 10, top[2].y() + depth * 0.48);
        painter.drawLine(a, a + QPointF(5, -5));
    }

    const qreal statTop = rect.bottom() - islandNavigationHeight() - 48;
    QRectF stat1(rect.left() + 20, statTop, 120, 34);
    QRectF stat2(stat1.right() + 10, stat1.top(), 130, 34);
    painter.setPen(Qt::NoPen);
    painter.setBrush(AppStyle::color(AppStyle::Color::Selection));
    painter.drawRoundedRect(stat1, 17, 17);
    painter.drawRoundedRect(stat2, 17, 17);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::Bold)));
    painter.setPen(AppStyle::color(AppStyle::Color::Primary));
    drawTextFit(painter, stat1.adjusted(6, 2, -6, -2),
                QStringLiteral("叶子 %1").arg(std::max(0, data.successCount * 2)),
                Qt::AlignCenter, 8);
    drawTextFit(painter, stat2.adjusted(6, 2, -6, -2),
                QStringLiteral("成长值 %1").arg(data.growthValue), Qt::AlignCenter, 8);

    painter.restore();
}

void ForestDashboardWidget::drawOverviewCards(QPainter& painter, const QRectF& rect,
                                               const DashboardData& data) const
{
    painter.save();
    const QVector<QPair<QString, QString>> items = {
        {QStringLiteral("累计专注时长"), QStringLiteral("%1 分钟").arg(data.totalMinutes)},
        {QStringLiteral("专注次数"), QStringLiteral("%1 次").arg(data.focusCount)},
        {QStringLiteral("成功 / 非成功"), QStringLiteral("%1 / %2").arg(data.successCount)
             .arg(data.abandonedCount + data.failedCount + data.interruptedCount)},
        {QStringLiteral("项目数量"), QStringLiteral("%1 项").arg(data.projectCount)},
        {QStringLiteral("常用树种"), data.topTree},
        {QStringLiteral("成长值"), QString::number(data.growthValue)},
    };

    const qreal gap = 12;
    const qreal cardW = (rect.width() - gap) / 2;
    const qreal cardH = std::min<qreal>(96, rect.height() * 0.38);
    for (int i = 0; i < 2; ++i) {
        const QRectF card(rect.left() + i * (cardW + gap), rect.top(), cardW, cardH);
        drawCard(painter, card);
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold)));
        drawTextFit(painter, card.adjusted(16, 10, -14, -card.height() * 0.55), items[i].first,
                    Qt::AlignLeft | Qt::AlignVCenter, 8);
        painter.setPen(AppStyle::color(AppStyle::Color::Primary));
        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 20, QFont::Bold)));
        drawTextFit(painter, card.adjusted(16, card.height() * 0.38, -14, -8), items[i].second,
                    Qt::AlignLeft | Qt::AlignVCenter, 11);
    }

    const qreal secondaryTop = rect.top() + cardH + 12;
    const qreal secondaryHeight = rect.bottom() - secondaryTop;
    const qreal columnW = rect.width() / 2;
    const qreal rowH = secondaryHeight / 2;
    painter.setPen(QPen(AppStyle::color(AppStyle::Color::Border), 1));
    painter.drawLine(QPointF(rect.left(), secondaryTop), QPointF(rect.right(), secondaryTop));
    painter.drawLine(QPointF(rect.center().x(), secondaryTop + 8),
                     QPointF(rect.center().x(), rect.bottom()));
    painter.drawLine(QPointF(rect.left(), secondaryTop + rowH),
                     QPointF(rect.right(), secondaryTop + rowH));

    for (int i = 2; i < items.size(); ++i) {
        const int row = (i - 2) / 2;
        const int col = (i - 2) % 2;
        const QRectF cell(rect.left() + col * columnW, secondaryTop + row * rowH,
                          columnW, rowH);
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9, QFont::DemiBold)));
        drawTextFit(painter, cell.adjusted(12, 6, -12, -cell.height() * 0.52), items[i].first,
                    Qt::AlignLeft | Qt::AlignVCenter, 8);
        painter.setPen(AppStyle::color(AppStyle::Color::Text));
        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 14, QFont::DemiBold)));
        drawTextFit(painter, cell.adjusted(12, cell.height() * 0.42, -12, -6), items[i].second,
                    Qt::AlignLeft | Qt::AlignVCenter, 10);
    }
    painter.restore();
}

void ForestDashboardWidget::drawTimeCard(QPainter& painter, const QRectF& rect, const DashboardData& data) const
{
    drawCard(painter, rect);
    painter.save();
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 17, QFont::Bold)));
    drawTextFit(painter, rect.adjusted(22, 18, -22, -rect.height() + 56),
                QStringLiteral("专注时间统计"), Qt::AlignLeft | Qt::AlignVCenter, 12);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::DemiBold)));
    painter.setPen(AppStyle::color(AppStyle::Color::Primary));
    drawTextFit(painter, rect.adjusted(22, 62, -22, -rect.height() + 92),
                QStringLiteral("累计专注时长：%1 分钟").arg(data.totalMinutes),
                Qt::AlignLeft | Qt::AlignVCenter, 9);

    QRectF chart = rect.adjusted(36, 112, -28, -116);
    painter.setPen(QPen(AppStyle::color(AppStyle::Color::Border), 1));
    const int maxVal = std::max(30, data.timeBuckets.isEmpty()
        ? 30
        : *std::max_element(data.timeBuckets.cbegin(), data.timeBuckets.cend()));
    for (int i = 0; i <= 3; ++i) {
        qreal y = chart.bottom() - chart.height() * i / 3.0;
        painter.drawLine(QPointF(chart.left(), y), QPointF(chart.right(), y));
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8)));
        painter.drawText(QRectF(rect.left() + 8, y - 10, 25, 20), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(maxVal * i / 3));
        painter.setPen(QPen(AppStyle::color(AppStyle::Color::Border), 1));
    }

    if (!data.hasData) {
        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 14, QFont::DemiBold)));
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter, chart, recordFilter_.isActive()
            ? QStringLiteral("当前筛选暂无专注记录") : QStringLiteral("当前时间范围暂无专注记录"),
            Qt::AlignCenter, 9);
    } else {
        const qreal barGap = data.timeBuckets.size() > 14 ? 3 : 7;
        const qreal barW = (chart.width() - barGap * (data.timeBuckets.size() - 1)) / data.timeBuckets.size();
        for (int i = 0; i < data.timeBuckets.size(); ++i) {
            qreal h = chart.height() * data.timeBuckets[i] / maxVal;
            QRectF bar(chart.left() + i * (barW + barGap), chart.bottom() - h, barW, h);
            painter.setPen(Qt::NoPen);
            painter.setBrush(AppStyle::color(AppStyle::Color::Primary));
            painter.drawRoundedRect(bar, 4, 4);
        }
    }

    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8)));
    for (int i = 0; i < data.axisLabels.size(); ++i) {
        qreal x = data.axisLabels.size() == 1
            ? chart.center().x()
            : chart.left() + chart.width() * i / (data.axisLabels.size() - 1);
        painter.drawText(QRectF(x - 24, chart.bottom() + 8, 48, 18), Qt::AlignCenter, data.axisLabels[i]);
    }

    QRectF eventsRect(rect.left() + 22, rect.bottom() - 82, rect.width() - 44, 58);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::Bold)));
    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    drawTextFit(painter, QRectF(eventsRect.left(), eventsRect.top(), eventsRect.width(), 18),
                QStringLiteral("最近专注记录"), Qt::AlignLeft | Qt::AlignVCenter, 7);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9, QFont::DemiBold)));
    if (data.recentEvents.isEmpty()) {
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter, eventsRect.adjusted(0, 22, 0, 0),
                    QStringLiteral("暂无已结束的专注记录"), Qt::AlignLeft | Qt::AlignTop, 8);
    } else {
        qreal eventY = eventsRect.top() + 22;
        const int maxRows = std::min(2, static_cast<int>(data.recentEvents.size()));
        for (int i = 0; i < maxRows; ++i) {
            const auto& event = data.recentEvents[i];
            painter.setPen(event.color);
            drawTextFit(painter, QRectF(eventsRect.left(), eventY, 64, 18), event.statusText,
                        Qt::AlignLeft | Qt::AlignVCenter, 8);
            painter.setPen(AppStyle::color(AppStyle::Color::Text));
            drawTextFit(painter,
                        QRectF(eventsRect.left() + 70, eventY, eventsRect.width() - 70, 18),
                        QStringLiteral("%1  %2").arg(event.timeText, event.projectName),
                        Qt::AlignLeft | Qt::AlignVCenter, 8);
            painter.setPen(AppStyle::color(AppStyle::Color::Muted));
            eventY += 18;
        }
    }
    painter.restore();
}

void ForestDashboardWidget::drawProjectCard(QPainter& painter, const QRectF& rect, const DashboardData& data) const
{
    drawCard(painter, rect);
    painter.save();
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 17, QFont::Bold)));
    drawTextFit(painter, rect.adjusted(22, 18, -22, -rect.height() + 56),
                QStringLiteral("专注项目分布"), Qt::AlignLeft | Qt::AlignVCenter, 12);
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9, QFont::DemiBold)));
    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    drawTextFit(painter, rect.adjusted(22, 48, -22, -rect.height() + 76),
                QStringLiteral("按项目标签使用次数统计"), Qt::AlignLeft | Qt::AlignVCenter, 8);

    const int rows = std::max(1, static_cast<int>(data.projects.size()));
    const qreal lineHeight = std::min<qreal>(
        QFontMetrics(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9))).height() + 3,
        (rect.height() - 102 - rows * 8) / (rows * 3));
    const qreal rowHeight = lineHeight * 3 + 8;
    const qreal legendOffset = std::max(80.0, std::min(rect.height() * 0.48,
                                                     rect.height() - 22 - rows * rowHeight));
    qreal size = std::min({rect.width() * 0.55, rect.height() * 0.30,
                          std::max(0.0, (legendOffset - 94) / 1.18)});
    QPointF center(rect.center().x(), rect.top() + 84 + size * 0.59);
    QRectF donut(center.x() - size / 2, center.y() - size / 2, size, size);
    if (size >= 40 && (!data.hasData || data.projects.isEmpty())) {
        painter.setPen(QPen(AppStyle::color(AppStyle::Color::Border), size * 0.18,
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(donut, 90 * 16, -360 * 16);
    } else if (size >= 40) {
        int start = 90 * 16;
        for (const auto& slice : data.projects) {
            int span = static_cast<int>(-slice.ratio * 360 * 16);
            painter.setPen(QPen(slice.color, size * 0.18, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(donut, start, span);
            start += span;
        }
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(AppStyle::color(AppStyle::Color::Workspace));
    painter.drawEllipse(donut.adjusted(size * 0.24, size * 0.24, -size * 0.24, -size * 0.24));

    qreal y = rect.top() + legendOffset;
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold)));
    if (!data.hasData || data.projects.isEmpty()) {
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter, QRectF(rect.left() + 24, y, rect.width() - 48, 24),
                    QStringLiteral("暂无项目分布"), Qt::AlignCenter, 8);
        painter.restore();
        return;
    }
    for (const auto& slice : data.projects) {
        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold)));
        painter.setPen(Qt::NoPen);
        painter.setBrush(slice.color);
        painter.drawEllipse(QRectF(rect.left() + 24, y + 4, 10, 10));
        painter.setPen(AppStyle::color(AppStyle::Color::Text));
        const QRectF nameRect(rect.left() + 42, y, rect.width() - 124, lineHeight);
        drawTextFit(painter, nameRect, slice.name, Qt::AlignLeft | Qt::AlignVCenter, 8);
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter, QRectF(rect.right() - 78, y, 56, lineHeight),
                    QStringLiteral("%1 次").arg(slice.totalCount), Qt::AlignRight | Qt::AlignVCenter, 8);
        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9, QFont::DemiBold)));
        const int successPercent = slice.totalCount > 0
            ? static_cast<int>(slice.successCount * 100.0 / slice.totalCount + 0.5)
            : 0;
        const int abandonedPercent = slice.totalCount > 0
            ? static_cast<int>(slice.abandonedCount * 100.0 / slice.totalCount + 0.5)
            : 0;
        const qreal metricLeft = rect.left() + 42;
        const qreal metricWidth = rect.width() - 60;
        const qreal metricGap = 8;
        const qreal metricColumnWidth = (metricWidth - metricGap) / 2.0;
        painter.setPen(AppStyle::color(AppStyle::Color::Primary));
        drawTextFit(painter,
                    QRectF(metricLeft, y + lineHeight, metricColumnWidth, lineHeight),
                    QStringLiteral("成功 %1%").arg(successPercent),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    8);
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter,
                    QRectF(metricLeft + metricColumnWidth + metricGap, y + lineHeight,
                           metricColumnWidth, lineHeight),
                    QStringLiteral("放弃 %1%").arg(abandonedPercent),
                    Qt::AlignRight | Qt::AlignVCenter,
                    8);
        const int failedPercent = slice.totalCount > 0
            ? static_cast<int>(slice.failedCount * 100.0 / slice.totalCount + 0.5) : 0;
        const int interruptedPercent = slice.totalCount > 0
            ? static_cast<int>(slice.interruptedCount * 100.0 / slice.totalCount + 0.5) : 0;
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter, QRectF(metricLeft, y + lineHeight * 2, metricColumnWidth, lineHeight),
                    QStringLiteral("失败 %1%").arg(failedPercent), Qt::AlignLeft | Qt::AlignVCenter, 8);
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter, QRectF(metricLeft + metricColumnWidth + metricGap, y + lineHeight * 2, metricColumnWidth, lineHeight),
                    QStringLiteral("中断 %1%").arg(interruptedPercent), Qt::AlignRight | Qt::AlignVCenter, 8);
        y += rowHeight;
    }
    painter.restore();
}

void ForestDashboardWidget::drawTreeCard(QPainter& painter, const QRectF& rect, const DashboardData& data) const
{
    drawCard(painter, rect);
    painter.save();
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 17, QFont::Bold)));
    drawTextFit(painter, rect.adjusted(22, 18, -22, -rect.height() + 56),
                QStringLiteral("常用树种"), Qt::AlignLeft | Qt::AlignVCenter, 12);

    if (!data.hasData || data.trees.isEmpty()) {
        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 14, QFont::DemiBold)));
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter, rect.adjusted(22, 74, -22, -22),
                    QStringLiteral("暂无树种记录"), Qt::AlignCenter, 9);
        painter.restore();
        return;
    }

    const int maxCount = data.trees.isEmpty() ? 1 : std::max(1, data.trees.first().count);
    qreal y = rect.top() + 78;
    for (int i = 0; i < data.trees.size(); ++i) {
        const auto& tree = data.trees[i];
        QRectF thumb(rect.left() + 22, y, 48, 54);
        drawPlantImage(painter, QPointF(thumb.center().x(), thumb.bottom() - 4),
                       0.68, tree.plantType, false);

        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::Bold)));
        painter.setPen(AppStyle::color(AppStyle::Color::Text));
        drawTextFit(painter, QRectF(rect.left() + 78, y + 2, rect.width() - 100, 24),
                    QStringLiteral("%1. %2").arg(i + 1).arg(tree.name), Qt::AlignLeft | Qt::AlignVCenter, 9);

        QRectF barBg(rect.left() + 78, y + 32, rect.width() - 160, 12);
        painter.setPen(Qt::NoPen);
        painter.setBrush(AppStyle::color(AppStyle::Color::Border));
        painter.drawRoundedRect(barBg, 6, 6);
        QRectF bar = barBg;
        bar.setWidth(barBg.width() * tree.count / maxCount);
        painter.setBrush(AppStyle::color(AppStyle::Color::Primary));
        painter.drawRoundedRect(bar, 6, 6);

        painter.setFont(AppStyle::scaledFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold)));
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter, QRectF(rect.right() - 78, y + 24, 56, 24),
                    QStringLiteral("%1 次").arg(tree.count), Qt::AlignRight | Qt::AlignVCenter, 8);
        y += 68;
    }
    painter.restore();
}

void ForestDashboardWidget::drawCard(QPainter& painter, const QRectF& rect, qreal radius) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(32, 60, 53, 18));
    painter.drawRoundedRect(rect.translated(0, 2), radius, radius);
    painter.setBrush(AppStyle::color(AppStyle::Color::Surface));
    painter.drawRoundedRect(rect, radius, radius);
    painter.setPen(QPen(AppStyle::color(AppStyle::Color::Border), 1));
    painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter.restore();
}

void ForestDashboardWidget::drawTree(QPainter& painter, QPointF base, qreal scale, const QColor& crown) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#8A5A33"));
    QRectF trunk(base.x() - 4 * scale, base.y() - 34 * scale, 8 * scale, 34 * scale);
    painter.drawRoundedRect(trunk, 3 * scale, 3 * scale);

    QPolygonF crownShape;
    crownShape << QPointF(base.x(), base.y() - 72 * scale)
               << QPointF(base.x() + 26 * scale, base.y() - 30 * scale)
               << QPointF(base.x(), base.y() - 14 * scale)
               << QPointF(base.x() - 25 * scale, base.y() - 30 * scale);
    painter.setBrush(crown);
    painter.drawPolygon(crownShape);
    painter.setBrush(crown.lighter(114));
    painter.drawEllipse(QRectF(base.x() - 15 * scale, base.y() - 60 * scale, 26 * scale, 22 * scale));
    painter.restore();
}

void ForestDashboardWidget::drawWitheredTree(QPainter& painter, QPointF base, qreal scale) const
{
    painter.save();
    painter.setPen(Qt::NoPen);

    painter.setBrush(QColor(76, 59, 43, 150));
    painter.drawEllipse(QRectF(base.x() - 18 * scale, base.y() - 8 * scale,
                               36 * scale, 12 * scale));

    QPen trunkPen(QColor("#7B5638"), 7 * scale, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(trunkPen);
    painter.drawLine(QPointF(base.x() - 3 * scale, base.y() - 2 * scale),
                     QPointF(base.x() + 5 * scale, base.y() - 42 * scale));

    QPen branchPen(QColor("#6E4A31"), 4 * scale, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(branchPen);
    QPointF joint(base.x() + 3 * scale, base.y() - 30 * scale);
    painter.drawLine(joint, joint + QPointF(-18 * scale, -12 * scale));
    painter.drawLine(joint, joint + QPointF(16 * scale, -10 * scale));
    painter.drawLine(joint + QPointF(-8 * scale, -6 * scale),
                     joint + QPointF(-20 * scale, 2 * scale));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#8E755E"));
    painter.drawEllipse(QRectF(base.x() - 8 * scale, base.y() - 54 * scale,
                               14 * scale, 10 * scale));
    painter.restore();
}

void ForestDashboardWidget::drawPlantImage(QPainter& painter, QPointF base, qreal scale,
                                           uint32_t plantType, bool abandoned) const
{
    QPixmap pm = PlantImageUtils::loadPlantIcon(finalPlantPath(plantType));
    if (pm.isNull()) {
        if (abandoned) drawWitheredTree(painter, base, scale);
        else drawTree(painter, base, scale, PlantCatalog::byType(plantType).chartColor);
        return;
    }
    if (abandoned) {
        pm = grayscalePixmap(pm);
    }

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(abandoned ? QColor(48, 48, 48, 70) : QColor(38, 94, 74, 58));
    painter.drawEllipse(QRectF(base.x() - 23 * scale, base.y() - 8 * scale,
                               46 * scale, 14 * scale));

    const QSizeF targetSize = pm.size().scaled(58 * scale, 72 * scale, Qt::KeepAspectRatio);
    QRectF target(base.x() - targetSize.width() / 2,
                  base.y() - targetSize.height() + 4 * scale,
                  targetSize.width(),
                  targetSize.height());
    painter.drawPixmap(target.toRect(), pm);
    painter.restore();
}

void ForestDashboardWidget::drawPond(QPainter& painter, const QPointF& center, qreal w, qreal h) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#ECD995"));
    painter.drawEllipse(QRectF(center.x() - w * 0.62, center.y() - h * 0.72, w * 1.24, h * 1.44));
    painter.setBrush(QColor("#75C6DE"));
    painter.drawEllipse(QRectF(center.x() - w / 2, center.y() - h / 2, w, h));
    painter.setBrush(QColor("#AEE4ED"));
    painter.drawEllipse(QRectF(center.x() - w * 0.23, center.y() - h * 0.30, w * 0.40, h * 0.24));
    painter.setPen(QPen(QColor("#59AFC9"), 1, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRectF(center.x() - w * 0.32, center.y() - h * 0.16, w * 0.64, h * 0.34), 195 * 16, 145 * 16);
    painter.restore();
}

void ForestDashboardWidget::drawTextFit(QPainter& painter, const QRectF& rect, const QString& text,
                                        int flags, int minPointSize) const
{
    QFont font = painter.font();
    while (font.pointSize() > minPointSize) {
        QFontMetrics fm(font);
        if (fm.horizontalAdvance(text) <= rect.width() && fm.height() <= rect.height()) break;
        font.setPointSize(font.pointSize() - 1);
    }
    painter.save();
    painter.setFont(font);
    painter.drawText(rect, flags, QFontMetrics(font).elidedText(text, Qt::ElideRight,
                                                            std::max(0, static_cast<int>(rect.width()))));
    painter.restore();
}

