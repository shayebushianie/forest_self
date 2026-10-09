#include <QGuiApplication>
#include <QImageReader>
#include <QDir>
#include <cstdio>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 2) return 1;
    const QDir directory(QString::fromLocal8Bit(argv[1]));
    for (const QString& name : {QStringLiteral("hourglass"), QStringLiteral("timer")}) {
        QImageReader reader(directory.filePath(name + ".svg"));
        reader.setScaledSize(QSize(72, 72));
        const QImage image = reader.read();
        if (image.isNull() || !image.save(directory.filePath(name + ".png"))) {
            std::fprintf(stderr, "Icon conversion failed\n");
            return 2;
        }
    }
    return 0;
}
