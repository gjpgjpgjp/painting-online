#include "CanvasExporter.h"

CanvasExporter::CanvasExporter(QObject *parent) : QObject(parent) {}

bool CanvasExporter::exportToImage(QGraphicsScene *scene, const QSize &canvasSize,
                                   const QString &fileName, const QString &format) {
    QSize size = scene->sceneRect().size().toSize();
    if (size.isEmpty()) size = canvasSize;

    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene->render(&painter);
    painter.end();

    return image.save(fileName, format.toLatin1());
}