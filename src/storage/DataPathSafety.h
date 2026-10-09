#ifndef DATAPATHSAFETY_H
#define DATAPATHSAFETY_H

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace DataPathSafety {

// Qt's canonical path alone does not resolve Windows junction ancestors.
inline QString resolvedPath(const QString& path, int depth = 0)
{
    if (depth > 32) return {};
    QString current = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList missing;
    while (true) {
        const QFileInfo info(current);
        if (info.isJunction() || info.isSymLink()) {
            const QString linkTarget = info.isJunction() ? info.junctionTarget() : info.symLinkTarget();
            if (linkTarget.isEmpty()) return {};
            current = resolvedPath(linkTarget, depth + 1);
            if (current.isEmpty()) return {};
            break;
        }
        const QString parent = info.dir().absolutePath();
        if (parent == current) break;
        missing.prepend(info.fileName());
        current = parent;
    }
    const QString canonical = QFileInfo(current).canonicalFilePath();
    if (!canonical.isEmpty()) current = canonical;
    for (const QString& component : missing) current = QDir(current).filePath(component);
    return QDir::cleanPath(QDir::fromNativeSeparators(current));
}

inline bool contains(const QString& parent, const QString& child)
{
    const QString root = resolvedPath(parent);
    const QString item = resolvedPath(child);
    if (root.isEmpty() || item.isEmpty()) return false;
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return item.compare(root, sensitivity) == 0 ||
        item.startsWith(root.endsWith('/') ? root : root + '/', sensitivity);
}

inline bool overlaps(const QString& first, const QString& second)
{
    if (resolvedPath(first).isEmpty() || resolvedPath(second).isEmpty()) return true;
    return contains(first, second) || contains(second, first);
}

inline bool hasLinkedComponent(const QString& root, const QString& path)
{
    const QString base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    QString current = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    while (true) {
        const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction()) return true;
        if (current.compare(base, sensitivity) == 0) return false;
        const QString parent = info.dir().absolutePath();
        if (parent == current) return true;
        current = parent;
    }
}

} // namespace DataPathSafety

#endif // DATAPATHSAFETY_H
