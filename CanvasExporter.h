#ifndef CANVASEXPORTER_H
#define CANVASEXPORTER_H

#include <QObject>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QSize>

class CanvasExporter : public QObject {
    Q_OBJECT
public:
    explicit CanvasExporter(QObject *parent = nullptr);

    bool exportToImage(QGraphicsScene *scene, const QSize &canvasSize,
                       const QString &fileName, const QString &format);
};

#endif // CANVASEXPORTER_H