#ifndef CANVASDATABASE_H
#define CANVASDATABASE_H

#include <QObject>
#include <QSize>
#include <QColor>
#include <QString>
#include <QJsonObject>
#include <QList>
#include "DrawingCore.h"      // 包含 DrawCmd, Layer, SimpleCanvasMetadata
#include "BrushPreset.h"

static constexpr int CURRENT_CANVAS_VERSION = 4;

class CanvasDatabase
{
public:
    // 保存完整画布状态
    static bool save(const QString &fileName,
                     const QList<DrawCmd> &commands,
                     const SimpleCanvasMetadata &metadata,
                     const QList<Layer> &layers,
                     const QList<BrushPreset> &brushPresets);

    // 加载完整画布状态
    static bool load(const QString &fileName,
                     QList<DrawCmd> &commands,
                     SimpleCanvasMetadata &metadata,
                     QList<Layer> &layers,
                     QList<BrushPreset> &brushPresets);

    // 旧版本兼容加载
    static bool loadLegacy(const QString &fileName,
                           QList<DrawCmd> &commands,
                           QSize &canvasSize,
                           QList<BrushPreset> &brushPresets);

    // 导出为JSON
    static QJsonObject exportToJson(const QList<DrawCmd> &commands,
                                    const SimpleCanvasMetadata &metadata,
                                    const QList<Layer> &layers,
                                    const QList<BrushPreset> &brushPresets);

    // 从JSON导入
    static bool importFromJson(const QJsonObject &root,
                               QList<DrawCmd> &commands,
                               SimpleCanvasMetadata &metadata,
                               QList<Layer> &layers,
                               QList<BrushPreset> &brushPresets);
};

#endif // CANVASDATABASE_H