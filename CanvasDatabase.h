#ifndef CANVASDATABASE_H
#define CANVASDATABASE_H

#include <QObject>
#include <QSize>
#include <QList>
#include "DrawingCore.h"
#include "BrushPreset.h"

class CanvasDatabase
{
public:
    static bool save(const QString &fileName, const QList<DrawCmd> &commands, const QSize &canvasSize, const QList<BrushPreset> &brushPresets = {});
    static bool load(const QString &fileName, QList<DrawCmd> &commands, QSize &canvasSize, QList<BrushPreset> &brushPresets);
};

#endif // CANVASDATABASE_H