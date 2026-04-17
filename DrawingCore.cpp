#include "DrawingCore.h"
#include "Command.h"
#include <QDataStream>
#include "CanvasDatabase.h"

// ==================== CanvasModel ====================

CanvasModel::CanvasModel(QObject *parent) : QObject(parent) {
    // 初始化默认元数据
    m_metadata.size = QSize(800, 600);
    m_metadata.canvasName = "未命名画布";
    m_metadata.backgroundColor = Qt::white;
    m_metadata.createdAt = QDateTime::currentMSecsSinceEpoch();
    m_metadata.modifiedAt = m_metadata.createdAt;

    addLayer("背景层");
    setCurrentLayer(0);
}

// 新增：辅助函数，将命令恢复到对应图层（放在前面以便使用）
void CanvasModel::restoreCommandToLayer(const DrawCmd &cmd) {
    if (Layer *layer = getLayer(cmd.layerId)) {
        // 检查是否已存在（避免重复）
        bool exists = false;
        for (const auto &existing : layer->commands) {
            if (existing.id == cmd.id) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            // 按ID升序插入，保持命令顺序
            bool inserted = false;
            for (int i = 0; i < layer->commands.size(); ++i) {
                if (layer->commands[i].id > cmd.id) {
                    layer->commands.insert(i, cmd);
                    inserted = true;
                    break;
                }
            }
            if (!inserted) {
                layer->commands.append(cmd);
            }
            // 更新ID计数器
            if (cmd.id >= m_nextCmdId) {
                m_nextCmdId = cmd.id + 1;
            }
        }
    }
}

QJsonObject CanvasModel::getFullState() const
{
    QJsonObject state;
    state["type"] = "fullState";
    state["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    state["metadata"] = m_metadata.toJson();
    state["layers"] = layersToJson();
    state["brushPresets"] = brushPresetsToJson();
    state["currentLayerId"] = m_currentLayerId;
    state["nextCmdId"] = m_nextCmdId;
    state["nextLayerId"] = m_nextLayerId;
    state["nextPresetId"] = m_nextPresetId;

    // 所有命令数据
    QJsonArray allCmds;
    for (const auto &layer : m_layers) {
        for (const auto &cmd : layer.commands) {
            QJsonObject cmdData;
            cmdToJson(cmdData, cmd);
            allCmds.append(cmdData);
        }
    }
    state["commands"] = allCmds;

    return state;
}

void CanvasModel::applyFullState(const QJsonObject &state)
{
    bool oldSuppress = m_suppressEmit;          // 保存旧值
    m_suppressEmit = true;                       // 临时禁止发射

    // 清空当前状态
    m_layers.clear();
    m_brushPresets.clear();
    clearHistory();

    // 应用元数据
    if (state.contains("metadata")) {
        m_metadata.fromJson(state["metadata"].toObject());
    }

    // 应用图层
    if (state.contains("layers")) {
        layersFromJson(state["layers"].toArray());
    }

    // 应用笔刷预设
    if (state.contains("brushPresets")) {
        brushPresetsFromJson(state["brushPresets"].toArray());
    }

    // 恢复ID计数器
    m_nextCmdId = state["nextCmdId"].toInt(1);
    m_nextLayerId = state["nextLayerId"].toInt(1);
    m_nextPresetId = state["nextPresetId"].toInt(1);
    m_currentLayerId = state["currentLayerId"].toInt(0);

    // 应用所有命令到对应图层
    QJsonArray cmds = state["commands"].toArray();
    for (const auto &v : cmds) {
        DrawCmd cmd;
        jsonToCmd(cmd, v.toObject());
        restoreCommandToLayer(cmd);  // 使用辅助函数
    }

    m_suppressEmit = oldSuppress;                 // 恢复原值

    // 仅在原状态允许发射时才发射信号
    if (!oldSuppress) {
        emit canvasReset();
        emit changed();
        emit layerChanged();
        emit brushPresetsChanged();
        emit fullStateReceived(state);
    }
}

QJsonObject CanvasModel::getDeltaState(qint64 sinceTimestamp) const
{
    QJsonObject delta;
    delta["type"] = "deltaState";
    delta["since"] = sinceTimestamp;
    delta["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    // 只包含指定时间之后的命令
    QJsonArray newCmds;
    for (const auto &layer : m_layers) {
        for (const auto &cmd : layer.commands) {
            // 这里假设命令没有直接的时间戳，可以通过ID或其他方式判断
            // 简化处理：返回所有命令，实际应该根据时间戳过滤
            QJsonObject cmdData;
            cmdToJson(cmdData, cmd);
            newCmds.append(cmdData);
        }
    }
    delta["commands"] = newCmds;

    return delta;
}

void CanvasModel::resetCanvas(const QSize &size, const QString &name)
{
    beginCommandGroup("resetCanvas");

    // 清空所有图层
    for (const auto &layer : m_layers) {
        if (!layer.commands.isEmpty()) {
            ClearLayerCommand *cmd = new ClearLayerCommand(layer.id);
            executeCommand(cmd);
        }
    }

    // 重置状态
    m_layers.clear();
    m_brushPresets.clear();
    clearHistory();

    // 重置元数据
    m_metadata = SimpleCanvasMetadata();
    m_metadata.size = size;
    m_metadata.canvasName = name;
    m_metadata.createdAt = QDateTime::currentMSecsSinceEpoch();
    m_metadata.modifiedAt = m_metadata.createdAt;

    m_nextCmdId = 1;
    m_nextLayerId = 1;
    m_nextPresetId = 1;
    m_currentLayerId = 0;

    // 创建默认背景层
    addLayerDirect("背景层");
    setCurrentLayer(0);

    endCommandGroup();

    emit canvasReset();
    emit changed();
    emit layerChanged();

    // 广播重置事件
    if (m_network && !m_suppressEmit) {
        QJsonObject resetMsg;
        resetMsg["type"] = "canvasReset";
        resetMsg["state"] = getFullState();
        m_network->sendCommand(resetMsg);
    }
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
    while (getLayer(m_nextLayerId)) {
        m_nextLayerId++;
    }
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

    // 关键修复：如果命令已有 layerId（来自网络），保留它
    if (newCmd.layerId == 0) {
        newCmd.layerId = m_currentLayerId;
    }
    newCmd.presetId = m_currentBrushPresetId;

    if (Layer *layer = getLayer(newCmd.layerId))
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
    cmd->setClientId(m_clientId);
    if (m_groupStackDepth > 0) {
        m_currentGroup.append(cmd);
    } else {
        pushUndo(cmd);
    }

    cmd->execute(this);
    if (!m_suppressEmit && m_network) {
        QJsonObject syncData = cmd->toJson();
        syncData["clientId"] = m_clientId;
        syncData["isNetworkEcho"] = false;
        m_network->sendCommand(syncData);
    }
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

    CommandType undoneType = cmd->type();
    QJsonObject undoMsg;

    undoMsg["type"] = "undoCmd";
    undoMsg["clientId"] = m_clientId;
    undoMsg["_fromSelf"] = true;
    undoMsg["undoneCmdType"] = static_cast<int>(undoneType);
    undoMsg["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    switch (undoneType) {
    case CommandType::DrawCmd: {
        DrawCommand* drawCmd = static_cast<DrawCommand*>(cmd);
        QJsonArray idsArray;
        idsArray.append(drawCmd->drawCmd().id);
        undoMsg["deletedIds"] = idsArray;
        break;
    }
    case CommandType::DeleteDrawCmds: {
        DeleteDrawCommand* delCmd = static_cast<DeleteDrawCommand*>(cmd);
        QJsonArray cmdsArray;
        for (const auto& c : delCmd->deletedCmds()) {
            QJsonObject cmdObj;
            CanvasModel::cmdToJson(cmdObj, c);
            cmdsArray.append(cmdObj);
        }
        undoMsg["restoredCmds"] = cmdsArray;
        QJsonArray idsArray;
        for (int id : delCmd->deletedCmdIds()) {
            idsArray.append(id);
        }
        undoMsg["restoredCmdIds"] = idsArray;
        QJsonObject layerMap;
        for (auto it = delCmd->deletedCmds().begin(); it != delCmd->deletedCmds().end(); ++it) {
            int id = it->id;
            int layerId = it->layerId;
            layerMap[QString::number(id)] = layerId;
        }
        undoMsg["layerIdMap"] = layerMap;
        break;
    }
    case CommandType::MoveDrawCmds: {
        MoveDrawCommand* moveCmd = static_cast<MoveDrawCommand*>(cmd);
        QJsonArray idsArray;
        for (int id : moveCmd->movedCmdIds()) {
            idsArray.append(id);
        }
        undoMsg["cmdIds"] = idsArray;
        undoMsg["offsetX"] = -moveCmd->offset().x();
        undoMsg["offsetY"] = -moveCmd->offset().y();
        break;
    }
    case CommandType::AddLayer: {
        AddLayerCommand* addCmd = static_cast<AddLayerCommand*>(cmd);
        undoMsg["layerId"] = addCmd->layerId();
        if (Layer* layer = getLayer(addCmd->layerId())) {
            QJsonObject layerObj;
            layerObj["id"] = layer->id;
            layerObj["name"] = layer->name;
            layerObj["opacity"] = layer->opacity;
            layerObj["visible"] = layer->visible;
            QJsonArray cmdsArray;
            for (const auto& c : layer->commands) {
                QJsonObject cmdObj;
                CanvasModel::cmdToJson(cmdObj, c);
                cmdsArray.append(cmdObj);
            }
            layerObj["commands"] = cmdsArray;
            undoMsg["layerData"] = layerObj;
        }
        break;
    }
    case CommandType::RemoveLayer: {
        RemoveLayerCommand* remCmd = static_cast<RemoveLayerCommand*>(cmd);
        undoMsg["layerId"] = remCmd->layerId();
        const Layer& saved = remCmd->savedLayer();
        QJsonObject layerObj;
        layerObj["id"] = saved.id;
        layerObj["name"] = saved.name;
        layerObj["opacity"] = saved.opacity;
        layerObj["visible"] = saved.visible;
        QJsonArray cmdsArray;
        for (const auto& c : saved.commands) {
            QJsonObject cmdObj;
            CanvasModel::cmdToJson(cmdObj, c);
            cmdsArray.append(cmdObj);
        }
        layerObj["commands"] = cmdsArray;
        undoMsg["restoredLayer"] = layerObj;
        break;
    }
    case CommandType::MoveLayer: {
        MoveLayerCommand* moveCmd = static_cast<MoveLayerCommand*>(cmd);
        undoMsg["fromIndex"] = moveCmd->toIndex();
        undoMsg["toIndex"] = moveCmd->fromIndex();
        break;
    }
    case CommandType::ClearLayer: {
        ClearLayerCommand* clearCmd = static_cast<ClearLayerCommand*>(cmd);
        undoMsg["layerId"] = clearCmd->layerId();
        QJsonArray cmdsArray;
        for (const auto& c : clearCmd->savedCmds()) {
            QJsonObject cmdObj;
            CanvasModel::cmdToJson(cmdObj, c);
            cmdsArray.append(cmdObj);
        }
        undoMsg["restoredCmds"] = cmdsArray;
        break;
    }
    case CommandType::Composite: {
        CompositeCommand* compCmd = static_cast<CompositeCommand*>(cmd);
        QJsonArray subCmdsArray;
        for (const auto* subCmd : compCmd->commands()) {
            subCmdsArray.append(subCmd->toJson());
        }
        undoMsg["compositeCommands"] = subCmdsArray;
        undoMsg["compositeName"] = compCmd->name();
        break;
    }
    default:
        break;
    }

    cmd->undo(this);
    m_redoStack.push(cmd);

    if (!m_suppressEmit) {
        emit changed();
        emit undoRedoStateChanged(canUndo(), canRedo());
    }

    if (!m_suppressEmit && m_network) {
        m_network->sendCommand(undoMsg);
    }
}

void CanvasModel::redo() {
    if (m_redoStack.isEmpty()) return;
    Command* cmd = m_redoStack.pop();

    CommandType redoneType = cmd->type();

    QJsonObject redoMsg;
    redoMsg["type"] = "redoCmd";
    redoMsg["clientId"] = m_clientId;
    redoMsg["_fromSelf"] = true;
    redoMsg["redoneCmdType"] = static_cast<int>(redoneType);
    redoMsg["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    redoMsg["commandData"] = cmd->toJson();

    cmd->execute(this);
    m_undoStack.push(cmd);

    if (!m_suppressEmit) {
        emit changed();
        emit undoRedoStateChanged(canUndo(), canRedo());
    }

    if (!m_suppressEmit && m_network) {
        m_network->sendCommand(redoMsg);
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
    for (auto it = extra.begin(); it != extra.end(); ++it)
        op[it.key()] = it.value();
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
    QString cmdClientId = obj["clientId"].toString();

    if (cmdClientId == m_clientId && obj["_fromSelf"].toBool()) {
        return;
    }

    if (type == "undoCmd") {
        CommandType undoneType = static_cast<CommandType>(obj["undoneCmdType"].toInt());
        bool oldSuppress = m_suppressEmit;
        m_suppressEmit = true;

        switch (undoneType) {
        case CommandType::DrawCmd: {
            QJsonArray deletedIds = obj["deletedIds"].toArray();
            QList<int> ids;
            for (const auto &v : deletedIds) ids.append(v.toInt());
            removeCommandsDirect(ids);
            break;
        }
        case CommandType::DeleteDrawCmds: {
            QJsonArray cmdsArray = obj["restoredCmds"].toArray();
            QJsonObject layerMap = obj["layerIdMap"].toObject();

            for (const auto &v : cmdsArray) {
                DrawCmd cmd;
                jsonToCmd(cmd, v.toObject());

                int targetLayerId = cmd.layerId;
                QString cmdIdStr = QString::number(cmd.id);
                if (layerMap.contains(cmdIdStr)) {
                    targetLayerId = layerMap[cmdIdStr].toInt();
                }
                cmd.layerId = targetLayerId;

                restoreCommandToLayer(cmd);
            }
            emit changed();
            break;
        }
        case CommandType::MoveDrawCmds: {
            QJsonArray idsArray = obj["cmdIds"].toArray();
            QList<int> ids;
            for (const auto &v : idsArray) ids.append(v.toInt());
            QPointF offset(obj["offsetX"].toDouble(), obj["offsetY"].toDouble());
            moveCommandsDirect(ids, offset);
            emit changed();
            break;
        }
        case CommandType::AddLayer: {
            int layerId = obj["layerId"].toInt();
            if (getLayer(layerId)) {
                removeLayerDirect(layerId);
                emit layerChanged();
                emit changed();
            }
            break;
        }
        case CommandType::RemoveLayer: {
            QJsonObject layerObj = obj["restoredLayer"].toObject();
            if (!layerObj.isEmpty()) {
                Layer layer(layerObj["id"].toInt(), layerObj["name"].toString());
                layer.opacity = layerObj["opacity"].toDouble(1.0);
                layer.visible = layerObj["visible"].toBool(true);

                QJsonArray cmdsArray = layerObj["commands"].toArray();
                for (const auto &v : cmdsArray) {
                    DrawCmd cmd;
                    jsonToCmd(cmd, v.toObject());
                    layer.commands.append(cmd);
                }

                addLayerDirect(layer);
                emit layerChanged();
                emit changed();
            }
            break;
        }
        case CommandType::MoveLayer: {
            int fromIndex = obj["fromIndex"].toInt();
            int toIndex = obj["toIndex"].toInt();
            if (fromIndex >= 0 && fromIndex < m_layers.size() &&
                toIndex >= 0 && toIndex < m_layers.size()) {
                moveLayerDirect(fromIndex, toIndex);
                emit layerChanged();
                emit changed();
            }
            break;
        }
        case CommandType::ClearLayer: {
            int layerId = obj["layerId"].toInt();
            QJsonArray cmdsArray = obj["restoredCmds"].toArray();
            if (Layer *layer = getLayer(layerId)) {
                for (const auto &v : cmdsArray) {
                    DrawCmd cmd;
                    jsonToCmd(cmd, v.toObject());
                    restoreCommandToLayer(cmd);
                }
                emit changed();
            }
            break;
        }
        case CommandType::Composite: {
            QJsonArray subCmdsArray = obj["compositeCommands"].toArray();
            for (int i = subCmdsArray.size() - 1; i >= 0; --i) {
                QJsonObject subCmdObj = subCmdsArray[i].toObject();
                QString subCmdType = subCmdObj["commandType"].toString();

                if (subCmdType == "draw") {
                    int cmdId = subCmdObj["id"].toInt();
                    removeCommandsDirect({cmdId});
                } else if (subCmdType == "deleteDrawCmds") {
                    QJsonArray restoredArray = subCmdObj["deletedCmds"].toArray();
                    for (const auto &v : restoredArray) {
                        DrawCmd cmd;
                        jsonToCmd(cmd, v.toObject());
                        restoreCommandToLayer(cmd);
                    }
                } else if (subCmdType == "addLayer") {
                    int lid = subCmdObj["layerId"].toInt();
                    if (getLayer(lid)) {
                        removeLayerDirect(lid);
                        emit layerChanged();
                    }
                } else if (subCmdType == "removeLayer") {
                    QJsonObject lobj = subCmdObj["restoredLayer"].toObject();
                    if (!lobj.isEmpty()) {
                        Layer layer(lobj["id"].toInt(), lobj["name"].toString());
                        layer.opacity = lobj["opacity"].toDouble(1.0);
                        layer.visible = lobj["visible"].toBool(true);
                        QJsonArray cmdsArray = lobj["commands"].toArray();
                        for (const auto &v : cmdsArray) {
                            DrawCmd cmd;
                            jsonToCmd(cmd, v.toObject());
                            layer.commands.append(cmd);
                        }
                        addLayerDirect(layer);
                        emit layerChanged();
                    }
                } else if (subCmdType == "clearLayer") {
                    int lid = subCmdObj["layerId"].toInt();
                    QJsonArray cmdsArray = subCmdObj["restoredCmds"].toArray();
                    if (Layer *layer = getLayer(lid)) {
                        for (const auto &v : cmdsArray) {
                            DrawCmd cmd;
                            jsonToCmd(cmd, v.toObject());
                            restoreCommandToLayer(cmd);
                        }
                    }
                } else if (subCmdType == "moveCmds") {
                    QJsonArray idsArray = subCmdObj["ids"].toArray();
                    QList<int> ids;
                    for (const auto &v : idsArray) ids.append(v.toInt());
                    QPointF offset(subCmdObj["dx"].toDouble(), subCmdObj["dy"].toDouble());
                    moveCommandsDirect(ids, QPointF(-offset.x(), -offset.y()));
                } else if (subCmdType == "moveLayer") {
                    int from = subCmdObj["from"].toInt();
                    int to = subCmdObj["to"].toInt();
                    if (from >= 0 && from < m_layers.size() && to >= 0 && to < m_layers.size()) {
                        moveLayerDirect(to, from);
                        emit layerChanged();
                    }
                }
            }
            emit changed();
            break;
        }
        default:
            if (m_network) {
                QJsonObject req;
                req["type"] = "syncRequest";
                m_network->sendCommand(req);
            }
            break;
        }

        m_suppressEmit = oldSuppress;

        if (!oldSuppress) {
            emit undoRedoStateChanged(canUndo(), canRedo());
        }
        return;
    }

    if (type == "redoCmd") {
        CommandType redoneType = static_cast<CommandType>(obj["redoneCmdType"].toInt());
        bool oldSuppress = m_suppressEmit;
        m_suppressEmit = true;

        QJsonObject cmdData = obj["commandData"].toObject();
        QString cmdTypeStr = cmdData["commandType"].toString();

        if (cmdTypeStr == "draw") {
            DrawCmd cmd;
            jsonToCmd(cmd, cmdData);
            restoreCommandToLayer(cmd);
            emit changed();
        } else if (cmdTypeStr == "deleteDrawCmds") {
            QJsonArray idsArray = cmdData["ids"].toArray();
            QList<int> ids;
            for (const auto &v : idsArray) ids.append(v.toInt());
            removeCommandsDirect(ids);
        } else if (cmdTypeStr == "moveCmds") {
            QJsonArray idsArray = cmdData["ids"].toArray();
            QList<int> ids;
            for (const auto &v : idsArray) ids.append(v.toInt());
            QPointF offset(cmdData["dx"].toDouble(), cmdData["dy"].toDouble());
            moveCommandsDirect(ids, offset);
            emit changed();
        } else if (cmdTypeStr == "addLayer") {
            QString name = cmdData["name"].toString();
            int lid = cmdData["layerId"].toInt();
            if (!getLayer(lid)) {
                Layer layer(lid, name);
                layer.opacity = cmdData["opacity"].toDouble(1.0);
                layer.visible = cmdData["visible"].toBool(true);
                addLayerDirect(layer);
                emit layerChanged();
                emit changed();
            }
        } else if (cmdTypeStr == "removeLayer") {
            int lid = cmdData["layerId"].toInt();
            if (getLayer(lid)) {
                removeLayerDirect(lid);
                emit layerChanged();
                emit changed();
            }
        } else if (cmdTypeStr == "moveLayer") {
            int from = cmdData["from"].toInt();
            int to = cmdData["to"].toInt();
            if (from >= 0 && from < m_layers.size() && to >= 0 && to < m_layers.size()) {
                moveLayerDirect(from, to);
                emit layerChanged();
                emit changed();
            }
        } else if (cmdTypeStr == "clearLayer") {
            int lid = cmdData["layerId"].toInt();
            if (Layer *layer = getLayer(lid)) {
                layer->commands.clear();
                emit changed();
            }
        } else if (cmdTypeStr == "composite") {
            QJsonArray subCmdsArray = cmdData["commands"].toArray();
            for (int i = 0; i < subCmdsArray.size(); ++i) {
                QJsonObject subCmdObj = subCmdsArray[i].toObject();
                QString subCmdType = subCmdObj["commandType"].toString();

                if (subCmdType == "draw") {
                    DrawCmd cmd;
                    jsonToCmd(cmd, subCmdObj);
                    restoreCommandToLayer(cmd);
                } else if (subCmdType == "deleteDrawCmds") {
                    QJsonArray idsArray = subCmdObj["ids"].toArray();
                    QList<int> ids;
                    for (const auto &v : idsArray) ids.append(v.toInt());
                    removeCommandsDirect(ids);
                } else if (subCmdType == "addLayer") {
                    QString name = subCmdObj["name"].toString();
                    int lid = subCmdObj["layerId"].toInt();
                    if (!getLayer(lid)) {
                        Layer layer(lid, name);
                        layer.opacity = subCmdObj["opacity"].toDouble(1.0);
                        layer.visible = subCmdObj["visible"].toBool(true);
                        addLayerDirect(layer);
                        emit layerChanged();
                    }
                } else if (subCmdType == "removeLayer") {
                    int lid = subCmdObj["layerId"].toInt();
                    if (getLayer(lid)) {
                        removeLayerDirect(lid);
                        emit layerChanged();
                    }
                } else if (subCmdType == "clearLayer") {
                    int lid = subCmdObj["layerId"].toInt();
                    if (Layer *layer = getLayer(lid)) {
                        layer->commands.clear();
                    }
                } else if (subCmdType == "moveCmds") {
                    QJsonArray idsArray = subCmdObj["ids"].toArray();
                    QList<int> ids;
                    for (const auto &v : idsArray) ids.append(v.toInt());
                    QPointF offset(subCmdObj["dx"].toDouble(), subCmdObj["dy"].toDouble());
                    moveCommandsDirect(ids, offset);
                } else if (subCmdType == "moveLayer") {
                    int from = subCmdObj["from"].toInt();
                    int to = subCmdObj["to"].toInt();
                    if (from >= 0 && from < m_layers.size() && to >= 0 && to < m_layers.size()) {
                        moveLayerDirect(from, to);
                        emit layerChanged();
                    }
                }
            }
            emit changed();
        }

        m_suppressEmit = oldSuppress;

        if (!oldSuppress) {
            emit undoRedoStateChanged(canUndo(), canRedo());
        }
        return;
    }

    if (type == "cmd") {
        if (obj["_fromSelf"].toBool()) return;
        DrawCmd cmd;
        jsonToCmd(cmd, obj["data"].toObject());
        restoreCommandToLayer(cmd);
        emit changed();
    } else if (type == "deleteCmds") {
        if (obj["_fromSelf"].toBool()) return;
        QList<int> ids;
        for (auto v : obj["ids"].toArray()) ids.append(v.toInt());
        removeCommandsDirect(ids);
    }
    else if (type == "moveCmds") {
        if (obj["_fromSelf"].toBool()) return;
        QList<int> ids;
        for (auto v : obj["ids"].toArray()) ids.append(v.toInt());
        QPointF offset(obj["dx"].toDouble(), obj["dy"].toDouble());
        moveCommandsDirect(ids, offset);
    } else if (type == "layerOp") {
        onNetworkLayerOp(obj);
    } else if (type == "layerSync") {
        QJsonArray layersArray = obj["layers"].toArray();
        layersFromJson(layersArray);
        int currentId = obj["currentLayerId"].toInt();
        if (getLayer(currentId))
            m_currentLayerId = currentId;
        emit layerChanged();
        emit changed();
    }
    else if (type == "restoreCmds") {
        QJsonArray cmdsArray = obj["cmds"].toArray();
        for (const auto &v : cmdsArray) {
            DrawCmd cmd;
            jsonToCmd(cmd, v.toObject());
            restoreCommandToLayer(cmd);
        }
        emit changed();
    }
    else if (type == "restoreLayer") {
        int layerId = obj["layerId"].toInt();
        QJsonArray cmdsArray = obj["cmds"].toArray();
        if (Layer *layer = getLayer(layerId)) {
            layer->commands.clear();
            for (const auto &v : cmdsArray) {
                DrawCmd cmd;
                jsonToCmd(cmd, v.toObject());
                restoreCommandToLayer(cmd);
            }
            emit changed();
        }
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

    // 检查是否是自己发起的操作
    QString cmdClientId = op["clientId"].toString();
    bool isOwnOperation = (cmdClientId == m_clientId);

    m_suppressEmit = true;

    if (opType == "add") {
        int newId = op["newId"].toInt();

        // 关键：更新自己的ID计数器，确保比网络ID大
        if (newId >= m_nextLayerId) {
            m_nextLayerId = newId + 1;
        }

        // 只有不是自己发起的，且不存在该ID时，才创建
        if (!isOwnOperation && !getLayer(newId)) {
            Layer layer(newId, op["name"].toString());
            layer.opacity = op["opacity"].toDouble(1.0);
            layer.visible = op["visible"].toBool(true);
            m_layers.append(layer);
            emit layerChanged();
        }
    } else if (opType == "remove") {
        removeLayerDirect(layerId);
        emit layerChanged();
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

int CanvasModel::addBrushPreset(const BrushPreset &preset) {
    // 生成新ID（如果预设自带ID且大于当前最大值，则更新计数器）
    int id = preset.id();
    if (id == 0) {
        id = m_nextPresetId++;
    } else {
        if (id >= m_nextPresetId)
            m_nextPresetId = id + 1;
    }

    // 检查是否已存在相同ID，若有则先移除
    for (int i = 0; i < m_brushPresets.size(); ++i) {
        if (m_brushPresets[i].id() == id) {
            m_brushPresets.removeAt(i);
            break;
        }
    }

    // 创建新笔刷副本（确保纹理独立）
    BrushPreset newPreset = preset;
    newPreset.setId(id);  // 需要为 BrushPreset 添加 setId 方法（见下方补充）
    m_brushPresets.append(newPreset);

    if (!m_suppressEmit)
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
    removeCommandsDirect(cmdIds);

    if (m_network && !m_suppressEmit) {
        QJsonObject obj;
        obj["type"] = "deleteCmds";
        obj["_fromSelf"] = true;
        obj["clientId"] = m_clientId;
        QJsonArray arr;
        for (int id : cmdIds) arr.append(id);
        obj["ids"] = arr;
        m_network->sendCommand(obj);
    }
}

void CanvasModel::moveCommands(const QList<int> &cmdIds, const QPointF &offset) {
    moveCommandsDirect(cmdIds, offset);

    if (m_network && !m_suppressEmit) {
        QJsonObject obj;
        obj["type"] = "moveCmds";
        obj["_fromSelf"] = true;
        obj["clientId"] = m_clientId;
        QJsonArray arr;
        for (int id : cmdIds) arr.append(id);
        obj["dx"] = offset.x();
        obj["dy"] = offset.y();
        m_network->sendCommand(obj);
    }
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

void CanvasModel::removeCommandsDirect(const QList<int> &cmdIds) {
    for (int id : cmdIds) {
        Layer *layer = nullptr;
        int index = -1;
        if (findCommandById(id, layer, index)) {
            layer->commands.removeAt(index);
        }
    }
    emit changed();
}

void CanvasModel::moveCommandsDirect(const QList<int> &cmdIds, const QPointF &offset) {
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