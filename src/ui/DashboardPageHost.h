#ifndef DASHBOARD_PAGE_HOST_H
#define DASHBOARD_PAGE_HOST_H

class QWidget;
class QString;

namespace DashboardPageHost {

// Wraps an existing dashboard widget in its page-level background and scroll policy.
QWidget* create(QWidget* dashboard, const QString& objectName, const QString& styleSheet,
                bool scrollable, QWidget* parent = nullptr);

}

#endif // DASHBOARD_PAGE_HOST_H
