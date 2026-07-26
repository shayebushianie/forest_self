#ifndef APPSTYLE_H
#define APPSTYLE_H

#include <QString>

class QApplication;
class QWidget;

class AppStyle {
public:
    static QString styleSheet(int fontScalePercent = 100, bool highContrast = false);
    static void installFocusVisibility(QApplication& application);
    static bool keyboardFocusVisible(const QWidget* widget);
};

#endif // APPSTYLE_H
