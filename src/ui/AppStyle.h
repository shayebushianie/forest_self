#ifndef APPSTYLE_H
#define APPSTYLE_H

#include <QString>
#include <QFont>
#include <QColor>

class QApplication;
class QWidget;

class AppStyle {
public:
    enum class Color { Workspace, Surface, Text, Muted, Primary, PrimaryHover, PrimaryPressed,
                       Selection, Border };
    static QColor color(Color role);
    static QString styleSheet();
    static void installSmoothInteractions(QApplication& app);
    static QString scaledStyleSheet(const QString& style, int percent);
    static QFont scaledFont(QFont font);
    static void applyAccessibility(QWidget& widget);
};

#endif // APPSTYLE_H
