#ifndef PLANTIMAGEUTILS_H
#define PLANTIMAGEUTILS_H

#include <QPixmap>
#include <QString>

namespace PlantImageUtils {

QPixmap removeLightEdgeBackground(const QPixmap& source);
QPixmap loadPlantIcon(const QString& path);

}

#endif // PLANTIMAGEUTILS_H
