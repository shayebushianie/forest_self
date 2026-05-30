#include "ui/StatisticsDialog.h"
#include "core/StatisticsCalculator.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QtMath>
#include <cmath>

class PieChartWidget : public QWidget {
public:
    explicit PieChartWidget(QWidget* parent = nullptr) : QWidget(parent) {}
    struct Slice { QString label; double value; QColor color; };
    QVector<Slice> slices;
    double total = 0;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (slices.isEmpty() || total <= 0) return;

        int side = qMin(width(), height()) / 2 - 10;
        QPointF center(width() / 2.0, height() / 2.0);
        QRectF pieRect(center.x() - side, center.y() - side, side * 2, side * 2);

        double startAngle = 90.0;
        for (const auto& s : slices) {
            double span = (s.value / total) * 360.0 * 16.0;
            painter.setBrush(s.color);
            painter.setPen(QPen(QColor("#1e2922"), 2));
            painter.drawPie(pieRect, static_cast<int>(startAngle * 16), static_cast<int>(span));
            startAngle -= (s.value / total) * 360.0;
        }

        // 中心孔（甜甜圈效果）
        painter.setBrush(QColor("#1e2922"));
        painter.setPen(Qt::NoPen);
        int hole = side * 0.45;
        painter.drawEllipse(center, hole, hole);
    }
};

StatisticsDialog::StatisticsDialog(DatabaseManager& db, QWidget* parent)
    : QDialog(parent), db_(db)
{
    setWindowTitle(QStringLiteral("专注时间统计"));
    setFixedSize(480, 560);
    setObjectName("StatisticsDialog");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(12);

    StatisticsCalculator calculator(db_);
    auto dist = calculator.calculateTagDistribution();

    uint64_t totalSeconds = 0;
    for (const auto& p : dist) totalSeconds += p.second;

    if (totalSeconds == 0) {
        auto* emptyLabel = new QLabel(this);
        emptyLabel->setText(QStringLiteral("\n暂无历史专注记录\n\n快去主界面开始你的第一次专注吧！"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("font-size:16px; color:#8A9A86; background:transparent; border:none;");
        mainLayout->addStretch();
        mainLayout->addWidget(emptyLabel);
        mainLayout->addStretch();

        auto* btn = new QPushButton(QStringLiteral("开始专注"), this);
        btn->setObjectName("btnPrimary");
        QObject::connect(btn, &QPushButton::clicked, this, &QDialog::accept);
        mainLayout->addWidget(btn);
        return;
    }

    auto* title = new QLabel(QStringLiteral("标签时间分布"), this);
    title->setStyleSheet("font-size:16px; font-weight:bold; color:#E8EAE6; background:transparent; border:none;");
    title->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(title);

    auto* chart = new PieChartWidget(this);
    chart->setMinimumHeight(280);

    QStringList tagNames = {QStringLiteral("#无标签"), QStringLiteral("#学习"),
        QStringLiteral("#写代码"), QStringLiteral("#阅读"), QStringLiteral("#运动")};
    QList<QColor> colors = {
        QColor("#2e3f34"), QColor("#4E9F3D"), QColor("#1E5128"),
        QColor("#D8B257"), QColor("#B85C38")
    };

    for (uint32_t i = 0; i <= 4; ++i) {
        if (dist[i] > 0) {
            double mins = dist[i] / 60.0;
            chart->slices.append({tagNames[i], mins, colors[i]});
        }
    }
    for (const auto& s : chart->slices) chart->total += s.value;
    mainLayout->addWidget(chart, 1);

    auto* legend = new QWidget(this);
    auto* legendLayout = new QVBoxLayout(legend);
    legendLayout->setSpacing(4);

    for (const auto& s : chart->slices) {
        auto* row = new QWidget(legend);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto* dot = new QLabel(row);
        dot->setFixedSize(12, 12);
        dot->setStyleSheet(QString("background-color:%1; border-radius:6px;").arg(s.color.name()));
        rowLayout->addWidget(dot);

        auto* name = new QLabel(s.label, row);
        name->setStyleSheet("color:#E8EAE6; background:transparent; border:none; font-size:13px;");
        rowLayout->addWidget(name);
        rowLayout->addStretch();

        auto* val = new QLabel(QStringLiteral("%1 分钟").arg(qRound(s.value)), row);
        val->setStyleSheet("color:#8A9A86; background:transparent; border:none; font-size:12px;");
        rowLayout->addWidget(val);

        legendLayout->addWidget(row);
    }
    mainLayout->addWidget(legend);

    auto* summary = new QLabel(
        QStringLiteral("总专注时长: %1 小时 %2 分钟")
            .arg(totalSeconds / 3600)
            .arg((totalSeconds % 3600) / 60), this);
    summary->setStyleSheet("font-size:14px; color:#D8B257; background:transparent; border:none; padding-top:4px;");
    summary->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(summary);

    auto* btn = new QPushButton(QStringLiteral("关闭"), this);
    QObject::connect(btn, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addWidget(btn);
}
