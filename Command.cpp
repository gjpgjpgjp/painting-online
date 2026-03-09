#include "Command.h"
#include "DrawingCore.h"

// ========== DrawCommand ==========
DrawCommand::DrawCommand(const DrawCmd& cmd, bool isStartOfStroke)
    : m_cmd(cmd), m_isStartOfStroke(isStartOfStroke) {
    m_timestamp = QDateTime::currentMSecsSinceEpoch();
}

void DrawCommand::execute(CanvasModel* model) {
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    m_assignedId = model->add(m_cmd, false);
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) model->emitChanged();
}

void DrawCommand::undo(CanvasModel* model) {
    if (m_assignedId < 0) return;
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    for (auto& layer : model->layers()) {
        for (int i = 0; i < layer.commands.size(); ++i) {
            if (layer.commands[i].id == m_assignedId) {
                layer.commands.removeAt(i);
                break;
            }
        }
    }
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) model->emitChanged();
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
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    m_deletedCmds.clear();
    m_layerIdMap.clear();
    for (int id : m_cmdIds) {
        for (auto& layer : model->layers()) {
            for (int i = 0; i < layer.commands.size(); ++i) {
                if (layer.commands[i].id == id) {
                    m_deletedCmds.append(layer.commands[i]);
                    m_layerIdMap[id] = layer.id;
                    layer.commands.removeAt(i);
                    break;
                }
            }
        }
    }
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) model->emitChanged();
}

void DeleteDrawCommand::undo(CanvasModel* model) {
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    for (const auto& cmd : m_deletedCmds) {
        int layerId = m_layerIdMap.value(cmd.id, model->currentLayerId());
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
}

QJsonObject DeleteDrawCommand::toJson() const {
    QJsonObject obj;
    obj["commandType"] = "deleteDrawCmds";
    QJsonArray arr;
    for (int id : m_cmdIds) arr.append(id);
    obj["ids"] = arr;
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
    obj["commandType"] = "moveDrawCmds";
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
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    m_layerId = model->addLayerDirect(m_name);
    m_savedLayer = *model->getLayer(m_layerId);
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) {
        model->emitChanged();
        model->syncLayersToNetwork();
    }
}

void AddLayerCommand::undo(CanvasModel* model) {
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    model->removeLayerDirect(m_layerId);
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) {
        model->emitChanged();
        model->syncLayersToNetwork();
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
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    Layer* layer = model->getLayer(m_layerId);
    if (!layer) {
        model->setSuppressEmit(oldSuppress);
        return;
    }
    m_savedLayer = *layer;
    m_oldCurrentLayerId = model->currentLayerId();
    model->removeLayerDirect(m_layerId);
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) {
        model->emitChanged();
        model->syncLayersToNetwork();
    }
}

void RemoveLayerCommand::undo(CanvasModel* model) {
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    model->addLayerDirect(m_savedLayer);
    if (model->getLayer(m_oldCurrentLayerId))
        model->setCurrentLayer(m_oldCurrentLayerId);
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) {
        model->emitChanged();
        model->syncLayersToNetwork();
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
    if (m_fromIndex >= 0 && m_fromIndex < layers.size()) {
        m_layerId = layers[m_fromIndex].id;
        model->moveLayerDirect(m_fromIndex, m_toIndex);
    }
}

void MoveLayerCommand::undo(CanvasModel* model) {
    model->moveLayerDirect(m_toIndex, m_fromIndex);
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
    if (!oldSuppress) model->emitChanged();
}

void ClearLayerCommand::undo(CanvasModel* model) {
    bool oldSuppress = model->suppressEmit();
    model->setSuppressEmit(true);
    if (Layer* layer = model->getLayer(m_layerId)) {
        layer->commands = m_savedCmds;
    }
    model->setSuppressEmit(oldSuppress);
    if (!oldSuppress) model->emitChanged();
}

QJsonObject ClearLayerCommand::toJson() const {
    QJsonObject obj;
    obj["commandType"] = "clearLayer";
    obj["layerId"] = m_layerId;
    return obj;
}

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
    QJsonArray arr;
    for (auto cmd : m_commands) arr.append(cmd->toJson());
    obj["commands"] = arr;
    return obj;
}