#include "CanvasDatabase.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QDebug>

bool CanvasDatabase::save(const QString &fileName, const QList<DrawCmd> &commands, const QSize &canvasSize, const QList<BrushPreset> &brushPresets)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QJsonObject root;
    root["width"] = canvasSize.width();
    root["height"] = canvasSize.height();
    root["version"] = 3;  // 版本3支持笔刷预设和点指令

    // 保存笔刷预设
    QJsonArray presetsArray;
    for (const auto &p : brushPresets) {
        presetsArray.append(p.toJson());
    }
    root["brushPresets"] = presetsArray;

    // 保存指令
    QJsonArray cmdsArray;
    for (const auto &cmd : commands) {
        QJsonObject cmdObj;
        cmdObj["id"] = cmd.id;
        cmdObj["layerId"] = cmd.layerId;
        cmdObj["presetId"] = cmd.presetId;
        cmdObj["type"] = static_cast<int>(cmd.type);
        cmdObj["color"] = cmd.color.name(QColor::HexArgb);
        cmdObj["width"] = cmd.width;

        if (cmd.type == CmdType::Pen || cmd.type == CmdType::EraserStroke) {
            QByteArray ba;
            QDataStream ds(&ba, QIODevice::WriteOnly);
            ds << cmd.path;
            cmdObj["path"] = QString::fromLatin1(ba.toBase64());
        } else if (cmd.type == CmdType::PenPoint) {
            cmdObj["pointX"] = cmd.point.x();
            cmdObj["pointY"] = cmd.point.y();
        } else {
            cmdObj["startX"] = cmd.start.x();
            cmdObj["startY"] = cmd.start.y();
            cmdObj["endX"] = cmd.end.x();
            cmdObj["endY"] = cmd.end.y();
        }
        cmdsArray.append(cmdObj);
    }
    root["commands"] = cmdsArray;

    QJsonDocument doc(root);
    file.write(doc.toJson());
    file.close();
    return true;
}

bool CanvasDatabase::load(const QString &fileName, QList<DrawCmd> &commands, QSize &canvasSize, QList<BrushPreset> &brushPresets)
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
    brushPresets.clear();
    if (version >= 3 && root.contains("brushPresets")) {
        QJsonArray presetsArray = root["brushPresets"].toArray();
        for (const auto &v : presetsArray) {
            BrushPreset p;
            p.fromJson(v.toObject());
            brushPresets.append(p);
        }
    }

    commands.clear();
    QJsonArray cmdsArray = root["commands"].toArray();
    for (const auto &v : cmdsArray) {
        QJsonObject obj = v.toObject();
        DrawCmd cmd;
        cmd.id = obj["id"].toInt();
        cmd.layerId = obj["layerId"].toInt(0);
        cmd.presetId = obj["presetId"].toInt(0);
        cmd.type = static_cast<CmdType>(obj["type"].toInt());
        cmd.color = QColor(obj["color"].toString());
        cmd.width = obj["width"].toInt();

        if (cmd.type == CmdType::Pen || cmd.type == CmdType::EraserStroke) {
            QString str = obj["path"].toString();
            QByteArray ba = QByteArray::fromBase64(str.toLatin1());
            QDataStream ds(ba);
            ds >> cmd.path;
        } else if (cmd.type == CmdType::PenPoint) {
            cmd.point = QPointF(obj["pointX"].toDouble(), obj["pointY"].toDouble());
        } else {
            cmd.start = QPointF(obj["startX"].toDouble(), obj["startY"].toDouble());
            cmd.end = QPointF(obj["endX"].toDouble(), obj["endY"].toDouble());
        }
        commands.append(cmd);
    }
    return true;
}