#ifndef APPSTYLE_H
#define APPSTYLE_H

#include <QString>

class QApplication;

class AppStyle {
public:
    static QString styleSheet();
    static void installSmoothInteractions(QApplication& app);
};

#endif // APPSTYLE_H
