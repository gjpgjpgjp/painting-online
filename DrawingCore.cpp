#include "DrawingCore.h"
#include "Command.h"
#include <QDataStream>

// ==================== CanvasModel ====================

CanvasModel::CanvasModel(QObject *parent) : QObject(parent) {
    addLayer("背景层");
    setCurrentLayer(0);
}

int CanvasModel::addLayer(const QString &name) {
    AddLayerCommand* cmd = new AddLayerCommand(name);
    executeCommand(cmd);
    return cmd->layerId();
}

void CanvasModel::removeLayer(int layerId) {
    RemoveLayerCommand* cmd = new RemoveLayerCommand(layerId);
    executeCommand(cmd);
}

void CanvasModel::moveLayer(int fromIndex, int toIndex) {
    MoveLayerCommand* cmd = new MoveLayerCommand(fromIndex, toIndex);
    executeCommand(cmd);
}

void CanvasModel::setLayerOpacity(int layerId, qreal opacity) {
    if (Layer *layer = getLayer(layerId)) {
        layer->opacity = qBound(0.0, opacity, 1.0);
        emit changed();
        QJsonObject extra;
        extra["opacity"] = layer->opacity;
        sendLayerOp("opacity", layerId, extra);
    }
}

void CanvasModel::setLayerVisible(int layerId, bool visible) {
    if (Layer *layer = getLayer(layerId)) {
        layer->visible = visible;
        emit changed();
        QJsonObject extra;
        extra["visible"] = visible;
        sendLayerOp("visible", layerId, extra);
    }
    emit layerChanged();
}

int CanvasModel::addLayerDirect(const QString &name) {
    Layer layer(m_nextLayerId++, name);
    m_layers.append(layer);
    return layer.id;
}

int CanvasModel::addLayerDirect(const Layer &layer) {
    m_layers.append(layer);
    if (layer.id >= m_nextLayerId)
        m_nextLayerId = layer.id + 1;
    return layer.id;
}

void CanvasModel::removeLayerDirect(int layerId) {
    m_layers.removeIf([layerId](const Layer &l) { return l.id == layerId; });
    if (m_currentLayerId == layerId && !m_layers.isEmpty())
        m_currentLayerId = m_layers.first().id;
}

void CanvasModel::moveLayerDirect(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= m_layers.size() ||
        toIndex < 0 || toIndex >= m_layers.size() || fromIndex == toIndex)
        return;
    Layer layer = m_layers.takeAt(fromIndex);
    m_layers.insert(toIndex, layer);
    m_currentLayerId = layer.id;
}

int CanvasModel::add(const DrawCmd &cmd, bool emitChanged) {
    DrawCmd newCmd = cmd;
    newCmd.id = m_nextCmdId++;
    newCmd.layerId = m_currentLayerId;
    newCmd.presetId = m_currentBrushPresetId;
    if (Layer *layer = getLayer(m_currentLayerId))
        layer->commands.append(newCmd);

    QJsonObject data;
    cmdToJson(data, newCmd);
    if (m_network && !m_suppressEmit) {
        QJsonObject obj;
        obj["type"] = "cmd";
        obj["data"] = data;
        if (m_isRoomOwner) obj["_fromSelf"] = true;
        m_network->sendCommand(obj);
    }
    if (!m_suppressEmit) emit commandAdded(data);
    if (emitChanged && !m_suppressEmit) emit changed();
    return newCmd.id;
}

void CanvasModel::executeDrawCommand(const DrawCmd &cmd, bool isStartOfStroke) {
    DrawCommand* drawCmd = new DrawCommand(cmd, isStartOfStroke);
    executeCommand(drawCmd);
}

void CanvasModel::executeCommand(Command* cmd) {
    if (!cmd) return;

    if (m_groupStackDepth > 0) {
        m_currentGroup.append(cmd);
    } else {
        pushUndo(cmd);
    }

    bool oldSuppress = m_suppressEmit;
    m_suppressEmit = true;
    cmd->execute(this);
    m_suppressEmit = oldSuppress;

    if (!m_suppressEmit) {
        emit changed();
        emit undoRedoStateChanged(canUndo(), canRedo());
    }

    clearStack(m_redoStack);
}

void CanvasModel::pushUndo(Command* cmd) {
    m_undoStack.push(cmd);
    while (m_undoStack.size() > MAX_HISTORY_SIZE) {
        Command* old = m_undoStack.takeFirst();
        delete old;
    }
}

void CanvasModel::undo() {
    if (m_undoStack.isEmpty()) return;

    Command* cmd = m_undoStack.pop();

    bool oldSuppress = m_suppressEmit;
    m_suppressEmit = true;
    cmd->undo(this);
    m_suppressEmit = oldSuppress;

    m_redoStack.push(cmd);

    if (!m_suppressEmit) {
        emit changed();
        emit undoRedoStateChanged(canUndo(), canRedo());
    }
}

void CanvasModel::redo() {
    if (m_redoStack.isEmpty()) return;

    Command* cmd = m_redoStack.pop();

    bool oldSuppress = m_suppressEmit;
    m_suppressEmit = true;
    cmd->execute(this);
    m_suppressEmit = oldSuppress;

    m_undoStack.push(cmd);

    if (!m_suppressEmit) {
        emit changed();
        emit undoRedoStateChanged(canUndo(), canRedo());
    }
}

void CanvasModel::clearHistory() {
    clearStack(m_undoStack);
    clearStack(m_redoStack);
    m_currentGroup.clear();
    m_groupStackDepth = 0;
    if (!m_suppressEmit) {
        emit undoRedoStateChanged(false, false);
    }
}

void CanvasModel::clearStack(QStack<Command*>& stack) {
    while (!stack.isEmpty()) {
        delete stack.pop();
    }
}

void CanvasModel::beginCommandGroup(const QString& name) {
    if (m_groupStackDepth == 0) {
        m_currentGroupName = name;
        m_currentGroup.clear();
    }
    m_groupStackDepth++;
}

void CanvasModel::endCommandGroup() {
    if (m_groupStackDepth <= 0) return;

    m_groupStackDepth--;

    if (m_groupStackDepth == 0 && !m_currentGroup.isEmpty()) {
        CompositeCommand* composite = new CompositeCommand(m_currentGroupName);
        for (Command* cmd : m_currentGroup) {
            composite->addCommand(cmd);
        }
        m_currentGroup.clear();
        pushUndo(composite);

        if (!m_suppressEmit) {
            emit undoRedoStateChanged(canUndo(), canRedo());
        }
    }
}

void CanvasModel::clear() {
    beginCommandGroup("clearAll");
    for (auto& layer : m_layers) {
        if (!layer.commands.isEmpty()) {
            ClearLayerCommand* cmd = new ClearLayerCommand(layer.id);
            executeCommand(cmd);
        }
    }
    endCommandGroup();

    // 原有实现（保留以清空数据，但已被命令覆盖）
    for (auto &l : m_layers) l.commands.clear();
    emit changed();
    sendLayerOp("clear", 0);
}

void CanvasModel::clearLayer(int layerId) {
    ClearLayerCommand* cmd = new ClearLayerCommand(layerId);
    executeCommand(cmd);
}

Layer* CanvasModel::getLayer(int layerId) {
    for (auto &l : m_layers) if (l.id == layerId) return &l;
    return nullptr;
}

int CanvasModel::getLayerIndex(int layerId) const {
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].id == layerId) return i;
    }
    return -1;
}

QList<DrawCmd>& CanvasModel::currentLayerCommands() {
    if (Layer *l = getLayer(m_currentLayerId)) return l->commands;
    static QList<DrawCmd> empty;
    return empty;
}

void CanvasModel::sendLayerOp(const QString &opType, int layerId, const QJsonObject &extra) {
    if (!m_network || m_suppressEmit) return;
    QJsonObject op;
    op["type"] = "layerOp";
    op["opType"] = opType;
    op["layerId"] = layerId;
    for (auto it = extra.begin(); it != extra.end(); ++it) op[it.key()] = it.value();
    m_network->sendCommand(op);
}

void CanvasModel::cmdToJson(QJsonObject &data, const DrawCmd &cmd) {
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

void CanvasModel::jsonToCmd(DrawCmd &cmd, const QJsonObject &data) {
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

QList<DrawCmd> CanvasModel::allCommands() const {
    QList<DrawCmd> result;
    for (const auto &l : m_layers) {
        if (!l.visible) continue;
        for (auto cmd : l.commands) {
            QColor c = cmd.color;
            c.setAlphaF(c.alphaF() * l.opacity);
            cmd.color = c;
            result.append(cmd);
        }
    }
    return result;
}

QList<QJsonObject> CanvasModel::getCommandData() const {
    QList<QJsonObject> list;
    for (const auto &l : m_layers) {
        for (const auto &cmd : l.commands) {
            QJsonObject data;
            cmdToJson(data, cmd);
            list.append(data);
        }
    }
    return list;
}

void CanvasModel::onNetworkCommand(const QJsonObject &obj) {
    QString type = obj["type"].toString();
    if (type == "cmd") {
        if (obj["_fromSelf"].toBool()) return;
        DrawCmd cmd;
        jsonToCmd(cmd, obj["data"].toObject());
        if (Layer *layer = getLayer(cmd.layerId)) {
            layer->commands.append(cmd);
            emit changed();
        }
    } else if (type == "deleteCmds") {
        QList<int> ids;
        for (auto v : obj["ids"].toArray()) ids.append(v.toInt());
        deleteCommands(ids);
    } else if (type == "moveCmds") {
        QList<int> ids;
        for (auto v : obj["ids"].toArray()) ids.append(v.toInt());
        QPointF offset(obj["dx"].toDouble(), obj["dy"].toDouble());
        moveCommands(ids, offset);
    } else if (type == "layerOp" || type == "layerSync") {
        onNetworkLayerOp(obj);
    }
}

QJsonArray CanvasModel::layersToJson() const {
    QJsonArray result;
    for (const auto &l : m_layers) {
        QJsonObject obj;
        obj["id"] = l.id;
        obj["name"] = l.name;
        obj["opacity"] = l.opacity;
        obj["visible"] = l.visible;
        result.append(obj);
    }
    return result;
}

void CanvasModel::layersFromJson(const QJsonArray &array) {
    m_layers.clear();
    for (const auto &v : array) {
        QJsonObject obj = v.toObject();
        Layer layer(obj["id"].toInt(), obj["name"].toString());
        layer.opacity = obj["opacity"].toDouble(1.0);
        layer.visible = obj["visible"].toBool(true);
        m_layers.append(layer);
        m_nextLayerId = qMax(m_nextLayerId, layer.id + 1);
    }
    emit layerChanged();
}

void CanvasModel::syncLayersToNetwork() {
    if (m_network && !m_suppressEmit) {
        QJsonObject obj;
        obj["type"] = "layerSync";
        obj["layers"] = layersToJson();
        obj["currentLayerId"] = m_currentLayerId;
        m_network->sendCommand(obj);
    }
}

void CanvasModel::onNetworkLayerOp(const QJsonObject &op) {
    QString opType = op["opType"].toString();
    int layerId = op["layerId"].toInt();
    m_suppressEmit = true;

    if (opType == "add") {
        Layer layer(op["newId"].toInt(), op["name"].toString());
        layer.opacity = op["opacity"].toDouble(1.0);
        layer.visible = op["visible"].toBool(true);
        m_layers.append(layer);
        m_nextLayerId = qMax(m_nextLayerId, layer.id + 1);
        emit layerChanged();
    } else if (opType == "remove") {
        removeLayerDirect(layerId);
    } else if (opType == "visible") {
        setLayerVisible(layerId, op["visible"].toBool());
    } else if (opType == "opacity") {
        setLayerOpacity(layerId, op["opacity"].toDouble());
    } else if (opType == "clear") {
        clear();
    } else if (opType == "clearLayer") {
        if (Layer *layer = getLayer(layerId)) {
            layer->commands.clear();
            emit changed();
        }
    } else if (opType == "fullSync") {
        layersFromJson(op["layers"].toArray());
        m_currentLayerId = op["currentLayerId"].toInt(m_layers.first().id);
    } else if (opType == "move") {
        QJsonArray order = op["order"].toArray();
        QList<Layer> newLayers;
        for (const auto &v : order) {
            int id = v.toInt();
            for (const auto &l : m_layers) {
                if (l.id == id) {
                    newLayers.append(l);
                    break;
                }
            }
        }
        for (const auto &l : m_layers) {
            bool found = false;
            for (const auto &v : order) {
                if (v.toInt() == l.id) { found = true; break; }
            }
            if (!found) newLayers.append(l);
        }
        m_layers = newLayers;
        m_currentLayerId = layerId;
        emit layerChanged();
        emit changed();
    }

    m_suppressEmit = false;
}

// 笔刷预设管理（保持不变）
int CanvasModel::addBrushPreset(const BrushPreset &preset) {
    int id = preset.id() ? preset.id() : m_nextPresetId++;
    if (preset.id()) m_nextPresetId = qMax(m_nextPresetId, id + 1);
    m_brushPresets.append(preset);
    emit brushPresetsChanged();
    return id;
}

void CanvasModel::removeBrushPreset(int id) {
    if (id == 0) return;
    m_brushPresets.removeIf([id](const BrushPreset &p) { return p.id() == id; });
    if (m_currentBrushPresetId == id) m_currentBrushPresetId = 0;
    emit brushPresetsChanged();
}

BrushPreset* CanvasModel::getBrushPreset(int id) {
    for (auto &p : m_brushPresets) if (p.id() == id) return &p;
    return nullptr;
}

void CanvasModel::setBrushPresets(const QList<BrushPreset> &presets) {
    m_brushPresets = presets;
    int maxId = 0;
    for (const auto &p : presets) if (p.id() > maxId) maxId = p.id();
    m_nextPresetId = maxId + 1;
    emit brushPresetsChanged();
}

QJsonArray CanvasModel::brushPresetsToJson() const {
    QJsonArray arr;
    for (const auto &p : m_brushPresets) arr.append(p.toJson());
    return arr;
}

void CanvasModel::brushPresetsFromJson(const QJsonArray &array) {
    m_brushPresets.clear();
    int maxId = 0;
    for (const auto &v : array) {
        BrushPreset p;
        p.fromJson(v.toObject());
        m_brushPresets.append(p);
        if (p.id() > maxId) maxId = p.id();
    }
    m_nextPresetId = maxId + 1;
    emit brushPresetsChanged();
}

void CanvasModel::setCurrentBrushPresetId(int id) {
    if (getBrushPreset(id)) m_currentBrushPresetId = id;
}

void CanvasModel::deleteCommands(const QList<int> &cmdIds) {
    for (int id : cmdIds) {
        Layer *layer = nullptr;
        int index = -1;
        if (findCommandById(id, layer, index)) {
            layer->commands.removeAt(index);
        }
    }
    emit changed();
}

void CanvasModel::moveCommands(const QList<int> &cmdIds, const QPointF &offset) {
    for (int id : cmdIds) {
        Layer *layer = nullptr;
        int index = -1;
        if (findCommandById(id, layer, index)) {
            auto &cmd = layer->commands[index];
            switch (cmd.type) {
            case CmdType::Pen:
            case CmdType::EraserStroke:
                cmd.path.translate(offset);
                break;
            case CmdType::Rect:
            case CmdType::Ellipse:
                cmd.start += offset;
                cmd.end += offset;
                break;
            case CmdType::PenPoint:
                cmd.point += offset;
                break;
            default: break;
            }
        }
    }
    emit changed();
}

bool CanvasModel::findCommandById(int id, Layer *&outLayer, int &outIndex) {
    for (auto &layer : m_layers) {
        for (int i = 0; i < layer.commands.size(); ++i) {
            if (layer.commands[i].id == id) {
                outLayer = &layer;
                outIndex = i;
                return true;
            }
        }
    }
    return false;
}