#include "CanvasDatabase.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDataStream>
#include <QBuffer>

// 内部辅助函数：将 DrawCmd 转换为 QJsonObject
static void cmdToJson(QJsonObject &data, const DrawCmd &cmd)
{
    data["id"] = cmd.id;
    data["layerId"] = cmd.layerId;
    data["presetId"] = cmd.presetId;
    data["cmdType"] = static_cast<int>(cmd.type);
    data["color"] = cmd.color.name(QColor::HexArgb);
    data["width"] = cmd.width;

    if (cmd.type == CmdType::Polygon) {
        QByteArray ba;
        QDataStream ds(&ba, QIODevice::WriteOnly);
        ds << cmd.polygon;
        data["polygon"] = QString::fromLatin1(ba.toBase64());
    }
    if (cmd.type == CmdType::Pen || cmd.type == CmdType::EraserStroke) {
        QByteArray ba;
        QDataStream ds(&ba, QIODevice::WriteOnly);
        ds << cmd.path;
        data["path"] = QString::fromLatin1(ba.toBase64());
    } else if (cmd.type == CmdType::PenPoint) {
        data["pointX"] = cmd.point.x();
        data["pointY"] = cmd.point.y();
    } else {
        data["startX"] = cmd.start.x();
        data["startY"] = cmd.start.y();
        data["endX"] = cmd.end.x();
        data["endY"] = cmd.end.y();
    }
}

// 内部辅助函数：从 QJsonObject 解析 DrawCmd
static void jsonToCmd(DrawCmd &cmd, const QJsonObject &data)
{
    cmd.id = data["id"].toInt();
    cmd.layerId = data["layerId"].toInt();
    cmd.presetId = data["presetId"].toInt(0);
    cmd.type = static_cast<CmdType>(data["cmdType"].toInt());
    cmd.color = QColor(data["color"].toString());
    cmd.width = data["width"].toInt();

    if (cmd.type == CmdType::Polygon) {
        QByteArray ba = QByteArray::fromBase64(data["polygon"].toString().toLatin1());
        QDataStream ds(ba);
        ds >> cmd.polygon;
    }
    if (cmd.type == CmdType::Pen || cmd.type == CmdType::EraserStroke) {
        QByteArray ba = QByteArray::fromBase64(data["path"].toString().toLatin1());
        QDataStream ds(ba);
        ds >> cmd.path;
    } else if (cmd.type == CmdType::PenPoint) {
        cmd.point = QPointF(data["pointX"].toDouble(), data["pointY"].toDouble());
    } else {
        cmd.start = QPointF(data["startX"].toDouble(), data["startY"].toDouble());
        cmd.end = QPointF(data["endX"].toDouble(), data["endY"].toDouble());
    }
}

bool CanvasDatabase::save(const QString &fileName,
                          const QList<DrawCmd> &commands,
                          const SimpleCanvasMetadata &metadata,
                          const QList<Layer> &layers,
                          const QList<BrushPreset> &brushPresets)
{
    QJsonObject root = exportToJson(commands, metadata, layers, brushPresets);
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson());
    file.close();
    return true;
}

bool CanvasDatabase::load(const QString &fileName,
                          QList<DrawCmd> &commands,
                          SimpleCanvasMetadata &metadata,
                          QList<Layer> &layers,
                          QList<BrushPreset> &brushPresets)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;

    return importFromJson(doc.object(), commands, metadata, layers, brushPresets);
}

bool CanvasDatabase::loadLegacy(const QString &fileName,
                                QList<DrawCmd> &commands,
                                QSize &canvasSize,
                                QList<BrushPreset> &brushPresets)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;

    QJsonObject root = doc.object();
    int width = root["width"].toInt(800);
    int height = root["height"].toInt(600);
    canvasSize = QSize(width, height);
    int version = root["version"].toInt(1);

    // 加载笔刷预设（版本>=3）
    if (version >= 3 && root.contains("brushPresets")) {
        QJsonArray presetsArray = root["brushPresets"].toArray();
        for (const auto &v : presetsArray) {
            BrushPreset p;
            p.fromJson(v.toObject());
            brushPresets.append(p);
        }
    }

    // 加载命令（旧格式，命令可能属于不同图层，但此处全部放到一个列表中）
    commands.clear();
    QJsonArray cmdsArray = root["commands"].toArray();
    for (const auto &v : cmdsArray) {
        DrawCmd cmd;
        jsonToCmd(cmd, v.toObject());
        commands.append(cmd);
    }
    return true;
}

QJsonObject CanvasDatabase::exportToJson(const QList<DrawCmd> &commands,
                                         const SimpleCanvasMetadata &metadata,
                                         const QList<Layer> &layers,
                                         const QList<BrushPreset> &brushPresets)
{
    QJsonObject root;
    root["version"] = CURRENT_CANVAS_VERSION;
    root["metadata"] = metadata.toJson();

    // 保存图层（包含每个图层的命令）
    QJsonArray layersArray;
    for (const Layer &layer : layers) {
        QJsonObject layerObj;
        layerObj["id"] = layer.id;
        layerObj["name"] = layer.name;
        layerObj["opacity"] = layer.opacity;
        layerObj["visible"] = layer.visible;

        QJsonArray cmdsArray;
        for (const DrawCmd &cmd : layer.commands) {
            QJsonObject cmdObj;
            cmdToJson(cmdObj, cmd);
            cmdsArray.append(cmdObj);
        }
        layerObj["commands"] = cmdsArray;
        layersArray.append(layerObj);
    }
    root["layers"] = layersArray;

    // 保存笔刷预设
    QJsonArray presetsArray;
    for (const BrushPreset &p : brushPresets) {
        presetsArray.append(p.toJson());
    }
    root["brushPresets"] = presetsArray;

    return root;
}

bool CanvasDatabase::importFromJson(const QJsonObject &root,
                                    QList<DrawCmd> &commands,
                                    SimpleCanvasMetadata &metadata,
                                    QList<Layer> &layers,
                                    QList<BrushPreset> &brushPresets)
{
    int version = root["version"].toInt(1);
    if (version < 2) {
        // 版本1：无图层，将所有命令放入一个图层
        QJsonArray cmdsArray = root["commands"].toArray();
        layers.clear();
        commands.clear();

        Layer defaultLayer(1, "背景层");
        for (const auto &v : cmdsArray) {
            DrawCmd cmd;
            jsonToCmd(cmd, v.toObject());
            cmd.layerId = defaultLayer.id;  // 强制设置图层ID
            commands.append(cmd);
            defaultLayer.commands.append(cmd);
        }
        layers.append(defaultLayer);
    } else {
        // 版本2及以上：从 layers 字段加载图层
        QJsonArray layersArray = root["layers"].toArray();
        layers.clear();
        for (const auto &lv : layersArray) {
            QJsonObject layerObj = lv.toObject();
            Layer layer(layerObj["id"].toInt(), layerObj["name"].toString());
            layer.opacity = layerObj["opacity"].toDouble(1.0);
            layer.visible = layerObj["visible"].toBool(true);

            QJsonArray cmdsArray = layerObj["commands"].toArray();
            for (const auto &cv : cmdsArray) {
                DrawCmd cmd;
                jsonToCmd(cmd, cv.toObject());
                layer.commands.append(cmd);
                commands.append(cmd);  // 同时收集到总列表（可选）
            }
            layers.append(layer);
        }
    }

    // 加载元数据
    if (root.contains("metadata")) {
        metadata.fromJson(root["metadata"].toObject());
    } else {
        // 兼容旧版本：从根节点读取宽高
        int w = root["width"].toInt(800);
        int h = root["height"].toInt(600);
        metadata.size = QSize(w, h);
        metadata.backgroundColor = Qt::white;
        metadata.canvasName = "未命名画布";
        metadata.createdAt = 0;
        metadata.modifiedAt = 0;
    }

    // 加载笔刷预设
    brushPresets.clear();
    if (root.contains("brushPresets")) {
        QJsonArray presetsArray = root["brushPresets"].toArray();
        for (const auto &v : presetsArray) {
            BrushPreset p;
            p.fromJson(v.toObject());
            brushPresets.append(p);
        }
    }

    return true;
}