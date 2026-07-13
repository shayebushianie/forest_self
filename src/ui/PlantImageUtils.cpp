#include "ui/PlantImageUtils.h"

#include <QImage>
#include <QPoint>
#include <QVector>

namespace {

bool isLightEdgeBackground(QRgb pixel)
{
    if (qAlpha(pixel) == 0) {
        return true;
    }

    const int r = qRed(pixel);
    const int g = qGreen(pixel);
    const int b = qBlue(pixel);
    const int maxChannel = qMax(r, qMax(g, b));
    const int minChannel = qMin(r, qMin(g, b));

    return minChannel >= 222 && (maxChannel - minChannel) <= 48;
}

void addSeedIfBackground(const QImage& image,
                         QVector<unsigned char>& visited,
                         QVector<QPoint>& stack,
                         int x,
                         int y)
{
    const int width = image.width();
    const int index = y * width + x;
    if (visited[index] || !isLightEdgeBackground(image.pixel(x, y))) {
        return;
    }
    visited[index] = 1;
    stack.push_back(QPoint(x, y));
}

} // namespace

namespace PlantImageUtils {

QPixmap removeLightEdgeBackground(const QPixmap& source)
{
    if (source.isNull()) {
        return source;
    }

    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0) {
        return source;
    }

    QVector<unsigned char> visited(width * height, 0);
    QVector<QPoint> stack;
    stack.reserve(width + height);

    for (int x = 0; x < width; ++x) {
        addSeedIfBackground(image, visited, stack, x, 0);
        addSeedIfBackground(image, visited, stack, x, height - 1);
    }
    for (int y = 1; y < height - 1; ++y) {
        addSeedIfBackground(image, visited, stack, 0, y);
        addSeedIfBackground(image, visited, stack, width - 1, y);
    }

    while (!stack.isEmpty()) {
        const QPoint point = stack.takeLast();
        const int x = point.x();
        const int y = point.y();

        if (x > 0) {
            addSeedIfBackground(image, visited, stack, x - 1, y);
        }
        if (x + 1 < width) {
            addSeedIfBackground(image, visited, stack, x + 1, y);
        }
        if (y > 0) {
            addSeedIfBackground(image, visited, stack, x, y - 1);
        }
        if (y + 1 < height) {
            addSeedIfBackground(image, visited, stack, x, y + 1);
        }
    }

    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            if (!visited[y * width + x]) {
                continue;
            }
            const QRgb pixel = line[x];
            line[x] = qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), 0);
        }
    }

    return QPixmap::fromImage(image);
}

QPixmap loadPlantIcon(const QString& path)
{
    return removeLightEdgeBackground(QPixmap(path));
}

} // namespace PlantImageUtils
