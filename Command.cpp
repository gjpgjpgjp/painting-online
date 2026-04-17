#include "Command.h"
#include "DrawingCore.h"

// ========== DrawCommand ==========
DrawCommand::DrawCommand(const DrawCmd& cmd, bool isStartOfStroke)
    : m_cmd(cmd), m_isStartOfStroke(isStartOfStroke) {
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
}

void DrawCommand::execute(CanvasModel* model) {
    m_assignedId = model->add(m_cmd, true);
}

void DrawCommand::undo(CanvasModel* model) {
    if (m_assignedId < 0) return;
    model->deleteCommands({m_assignedId});
    m_assignedId = -1;
}

QJsonObject DrawCommand::toJson() const {
    QJsonObject obj;
    obj["commandType"] = "draw";
    // 序列化 m_cmd 略
    return obj;
}

bool DrawCommand::isMergeable() const {
    return !m_isStartOfStroke && m_cmd.type == CmdType::PenPoint;
}

void DrawCommand::merge(const Command* other) {
    const DrawCommand* otherDraw = dynamic_cast<const DrawCommand*>(other);
    if (!otherDraw) return;
    m_cmd = otherDraw->m_cmd;
}

// ========== DeleteDrawCommand ==========
DeleteDrawCommand::DeleteDrawCommand(const QList<int>& cmdIds, const QList<DrawCmd>& cmds)
    : m_cmdIds(cmdIds), m_deletedCmds(cmds) {
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
}

void DeleteDrawCommand::execute(CanvasModel* model) {
    if (m_deletedCmds.isEmpty()) {
        for (int id : m_cmdIds) {
            Layer *layer = nullptr;
            int index = -1;
            if (model->findCommandById(id, layer, index)) {
                m_deletedCmds.append(layer->commands[index]);
                m_layerIdMap[id] = layer->id;
            }
        }
    }

    model->deleteCommands(m_cmdIds);
}

void DeleteDrawCommand::undo(CanvasModel* model) {
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);

    for (const auto& cmd : m_deletedCmds) {
        int layerId = m_layerIdMap.value(cmd.id, cmd.layerId);
        if (Layer* layer = model->getLayer(layerId)) {
            bool inserted = false;
            for (int i = 0; i < layer->commands.size(); ++i) {
                if (layer->commands[i].id > cmd.id) {
                    layer->commands.insert(i, cmd);
                    inserted = true;
                    break;
                }
            }
            if (!inserted) layer->commands.append(cmd);
        }
    }

    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) model->emitChanged();

    if (!oldSuppress && model->networkManager()) {
        QJsonObject msg;
        msg["type"] = "restoreCmds";
        msg["_fromSelf"] = true;
        msg["clientId"] = model->clientId();
        QJsonArray cmdsArray;
        for (const auto& cmd : m_deletedCmds) {
            QJsonObject cmdObj;
            CanvasModel::cmdToJson(cmdObj, cmd);
            cmdsArray.append(cmdObj);
        }
        msg["cmds"] = cmdsArray;
        QJsonObject layerMap;
        for (auto it = m_layerIdMap.begin(); it != m_layerIdMap.end(); ++it) {
            layerMap[QString::number(it.key())] = it.value();
        }
        msg["layerIdMap"] = layerMap;
        model->networkManager()->sendCommand(msg);
    }
}

QJsonObject DeleteDrawCommand::toJson() const {
    QJsonObject obj;
    obj["commandType"] = "deleteDrawCmds";
    obj["timestamp"] = m_timestamp;
    QJsonArray arr;
    for (int id : m_cmdIds) arr.append(id);
    obj["ids"] = arr;
    QJsonArray deletedArray;
    for (const auto& cmd : m_deletedCmds) {
        QJsonObject cmdObj;
        CanvasModel::cmdToJson(cmdObj, cmd);
        deletedArray.append(cmdObj);
    }
    obj["deletedCmds"] = deletedArray;
    QJsonObject layerMap;
    for (auto it = m_layerIdMap.begin(); it != m_layerIdMap.end(); ++it) {
        layerMap[QString::number(it.key())] = it.value();
    }
    obj["layerIdMap"] = layerMap;
    return obj;
}

// ========== MoveDrawCommand ==========
MoveDrawCommand::MoveDrawCommand(const QList<int>& cmdIds, const QPointF& offset)
    : m_cmdIds(cmdIds), m_offset(offset) {
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
}

void MoveDrawCommand::execute(CanvasModel* model) {
    model->moveCommands(m_cmdIds, m_offset);
}

void MoveDrawCommand::undo(CanvasModel* model) {
    model->moveCommands(m_cmdIds, -m_offset);
}

QJsonObject MoveDrawCommand::toJson() const {
    QJsonObject obj;
    obj["type"] = "moveCmds";
    QJsonArray arr;
    for (int id : m_cmdIds) arr.append(id);
    obj["dx"] = m_offset.x();
    obj["dy"] = m_offset.y();
    return obj;
}

// ========== AddLayerCommand ==========
AddLayerCommand::AddLayerCommand(const QString& name, int assignedId)
    : LayerCommand(-1), m_name(name), m_assignedId(assignedId) {
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
}

void AddLayerCommand::execute(CanvasModel* model) {
    // 关键修改：如果指定了ID（来自网络），直接使用该ID
    // 如果没指定ID（本地操作），才分配新ID
    if (m_assignedId > 0) {
        m_layerId = m_assignedId;
        // 检查是否已存在该ID的图层
        if (model->getLayer(m_layerId)) {
            // 已存在，说明已经通过消息创建过了，不再重复创建
            return;
        }
    } else {
        // 本地操作：分配新ID
        m_layerId = model->addLayerDirect(m_name);
    }

    // 确保ID计数器正确（关键！）
    model->ensureNextLayerId(m_layerId);

    m_savedLayer = *model->getLayer(m_layerId);

    if (!model->suppressEmit()) {
        emit model->layerChanged();
        QJsonObject extra;
        extra["newId"] = m_layerId;
        extra["name"] = m_name;
        extra["opacity"] = m_savedLayer.opacity;
        extra["visible"] = m_savedLayer.visible;
        extra["clientId"] = model->clientId();
        model->sendLayerOp("add", -1, extra);
    }
}

void AddLayerCommand::undo(CanvasModel* model) {
    model->removeLayerDirect(m_layerId);
    if (!model->suppressEmit()) {
        emit model->layerChanged();
        // 发送 remove 类型的 layerOp
        QJsonObject extra;
        extra["clientId"] = model->clientId();
        model->sendLayerOp("remove", m_layerId, extra);

        // 广播撤销信息
        if (model->networkManager()) {
            QJsonObject msg;
            msg["type"] = "undoCmd";
            msg["clientId"] = model->clientId();
            msg["_fromSelf"] = true;
            msg["undoneCmdType"] = static_cast<int>(CommandType::AddLayer);
            msg["layerId"] = m_layerId;
            model->networkManager()->sendCommand(msg);
        }
    }
}

QJsonObject AddLayerCommand::toJson() const {
    QJsonObject obj;
    obj["commandType"] = "addLayer";
    obj["name"] = m_name;
    obj["layerId"] = m_layerId;
    return obj;
}

// ========== RemoveLayerCommand ==========
RemoveLayerCommand::RemoveLayerCommand(int layerId)
    : LayerCommand(layerId) {
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
}

void RemoveLayerCommand::execute(CanvasModel* model) {
    Layer* layer = model->getLayer(m_layerId);
    if (!layer) return;
    m_savedLayer = *layer;
    m_oldCurrentLayerId = model->currentLayerId();
    model->removeLayerDirect(m_layerId);
    if (!model->suppressEmit()) {
        emit model->layerChanged();
        // 发送 remove 类型的 layerOp
        QJsonObject extra;
        extra["clientId"] = model->clientId();
        model->sendLayerOp("remove", m_layerId, extra);
    }
}

void RemoveLayerCommand::undo(CanvasModel* model) {
    // 恢复图层
    model->addLayerDirect(m_savedLayer);
    if (model->getLayer(m_oldCurrentLayerId))
        model->setCurrentLayer(m_oldCurrentLayerId);

    if (!model->suppressEmit()) {
        emit model->layerChanged();
        // 发送 add 类型的 layerOp（恢复图层）
        QJsonObject extra;
        extra["newId"] = m_savedLayer.id;
        extra["name"] = m_savedLayer.name;
        extra["opacity"] = m_savedLayer.opacity;
        extra["visible"] = m_savedLayer.visible;
        extra["clientId"] = model->clientId();
        model->sendLayerOp("add", -1, extra);

        // 广播撤销信息
        if (model->networkManager()) {
            QJsonObject msg;
            msg["type"] = "undoCmd";
            msg["clientId"] = model->clientId();
            msg["_fromSelf"] = true;
            msg["undoneCmdType"] = static_cast<int>(CommandType::RemoveLayer);
            msg["layerId"] = m_savedLayer.id;
            // 发送完整图层数据以便远程恢复
            QJsonObject layerObj;
            layerObj["id"] = m_savedLayer.id;
            layerObj["name"] = m_savedLayer.name;
            layerObj["opacity"] = m_savedLayer.opacity;
            layerObj["visible"] = m_savedLayer.visible;
            // 发送图层内的所有命令
            QJsonArray cmdsArray;
            for (const auto& cmd : m_savedLayer.commands) {
                QJsonObject cmdObj;
                CanvasModel::cmdToJson(cmdObj, cmd);
                cmdsArray.append(cmdObj);
            }
            layerObj["commands"] = cmdsArray;
            msg["restoredLayer"] = layerObj;
            model->networkManager()->sendCommand(msg);
        }
    }
}

QJsonObject RemoveLayerCommand::toJson() const {
    QJsonObject obj;
    obj["commandType"] = "removeLayer";
    obj["layerId"] = m_layerId;
    return obj;
}

// ========== MoveLayerCommand ==========
MoveLayerCommand::MoveLayerCommand(int fromIndex, int toIndex)
    : LayerCommand(-1), m_fromIndex(fromIndex), m_toIndex(toIndex) {
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
}

void MoveLayerCommand::execute(CanvasModel* model) {
    auto& layers = model->layers();
    if (m_fromIndex >= 0 && m_fromIndex < layers.size() &&
        m_toIndex >= 0 && m_toIndex < layers.size() &&
        m_fromIndex != m_toIndex) {

        m_layerId = layers[m_fromIndex].id;
        model->moveLayerDirect(m_fromIndex, m_toIndex);

        if (!model->suppressEmit()) {
            emit model->layerChanged();

            // 发送完整的图层顺序，确保所有客户端一致
            QJsonObject extra;
            QJsonArray order;
            for (const auto &l : model->layers())
                order.append(l.id);
            extra["order"] = order;
            extra["fromIndex"] = m_fromIndex;
            extra["toIndex"] = m_toIndex;
            extra["clientId"] = model->clientId();
            model->sendLayerOp("move", m_layerId, extra);
        }
    }
}

void MoveLayerCommand::undo(CanvasModel* model) {
    model->moveLayerDirect(m_toIndex, m_fromIndex);
    if (!model->suppressEmit()) {
        emit model->layerChanged();

        QJsonObject extra;
        QJsonArray order;
        for (const auto &l : model->layers())
            order.append(l.id);
        extra["order"] = order;
        extra["fromIndex"] = m_toIndex;
        extra["toIndex"] = m_fromIndex;
        extra["clientId"] = model->clientId();
        extra["isUndo"] = true;
        model->sendLayerOp("move", m_layerId, extra);

        // 广播撤销信息
        if (model->networkManager()) {
            QJsonObject msg;
            msg["type"] = "undoCmd";
            msg["clientId"] = model->clientId();
            msg["_fromSelf"] = true;
            msg["undoneCmdType"] = static_cast<int>(CommandType::MoveLayer);
            msg["needLayerSync"] = true;
            model->networkManager()->sendCommand(msg);
        }
    }
}

QJsonObject MoveLayerCommand::toJson() const {
    QJsonObject obj;
    obj["commandType"] = "moveLayer";
    obj["from"] = m_fromIndex;
    obj["to"] = m_toIndex;
    return obj;
}

// ========== ClearLayerCommand ==========
ClearLayerCommand::ClearLayerCommand(int layerId)
    : LayerCommand(layerId) {
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
}

void ClearLayerCommand::execute(CanvasModel* model) {
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    if (Layer* layer = model->getLayer(m_layerId)) {
        m_savedCmds = layer->commands;
        layer->commands.clear();
    }
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) {
        model->emitChanged();
        // 发送 clearLayer 消息同步到其他客户端
        QJsonObject extra;
        extra["layerId"] = m_layerId;
        extra["clientId"] = model->clientId();
        model->sendLayerOp("clearLayer", m_layerId, extra);
    }
}

void ClearLayerCommand::undo(CanvasModel* model) {
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    if (Layer* layer = model->getLayer(m_layerId)) {
        layer->commands = m_savedCmds;
    }
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) model->emitChanged();

    // 网络广播
    if (!oldSuppress && model->networkManager()) {
        QJsonObject msg;
        msg["type"] = "restoreLayer";
        msg["_fromSelf"] = true;
        msg["clientId"] = model->clientId();
        msg["layerId"] = m_layerId;
        QJsonArray cmdsArray;
        for (const auto& cmd : m_savedCmds) {
            QJsonObject cmdObj;
            CanvasModel::cmdToJson(cmdObj, cmd);
            cmdsArray.append(cmdObj);
        }
        msg["cmds"] = cmdsArray;
        model->networkManager()->sendCommand(msg);

        // 同时发送 undoCmd 以便统一处理
        QJsonObject undoMsg;
        undoMsg["type"] = "undoCmd";
        undoMsg["clientId"] = model->clientId();
        undoMsg["_fromSelf"] = true;
        undoMsg["undoneCmdType"] = static_cast<int>(CommandType::ClearLayer);
        undoMsg["layerId"] = m_layerId;
        // 由于 savedCmds 是 private 且没有 getter，这里不发送 restoredCmds
        // 而是依赖 needSync 标记或 restoreLayer 消息
        undoMsg["hasRestoreData"] = true;  // 标记有恢复数据通过 restoreLayer 发送
        model->networkManager()->sendCommand(undoMsg);
    }
}

QJsonObject ClearLayerCommand::toJson() const {
    QJsonObject obj;
    obj["commandType"] = "clearLayer";
    obj["layerId"] = m_layerId;
    return obj;
}

// 注意：savedCmds() 内联定义在头文件中，不需要在这里实现

// ========== CompositeCommand ==========
CompositeCommand::CompositeCommand(const QString& name) : m_name(name) {
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
}

CompositeCommand::~CompositeCommand() {
    for (auto cmd : m_commands) delete cmd;
}

void CompositeCommand::addCommand(Command* cmd) {
    m_commands.append(cmd);
}

void CompositeCommand::execute(CanvasModel* model) {
    for (auto cmd : m_commands) cmd->execute(model);
}

void CompositeCommand::undo(CanvasModel* model) {
    for (int i = m_commands.size() - 1; i >= 0; --i)
        m_commands[i]->undo(model);
}

QJsonObject CompositeCommand::toJson() const {
    QJsonObject obj;
    obj["commandType"] = "composite";
    obj["name"] = m_name;
    obj["timestamp"] = m_timestamp;
    QJsonArray arr;
    for (auto cmd : m_commands) {
        arr.append(cmd->toJson());
    }
    obj["commands"] = arr;
    return obj;
}

CommandType CompositeCommand::type() const {
    return CommandType::Composite;
}