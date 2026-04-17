#ifndef COMMAND_H
#define COMMAND_H

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QDateTime>
#include "DrawingCore.h"

class CanvasModel;

enum class CommandType {
    DrawCmd,
    DeleteDrawCmds,
    MoveDrawCmds,
    AddLayer,
    RemoveLayer,
    MoveLayer,
    SetLayerOpacity,
    SetLayerVisible,
    ClearLayer,
    ClearAll,
    Composite
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(CanvasModel* model) = 0;
    virtual void undo(CanvasModel* model) = 0;
    virtual CommandType type() const = 0;
    virtual QJsonObject toJson() const = 0;
    virtual bool isMergeable() const { return false; }
    virtual void merge(const Command* other) { Q_UNUSED(other) }
    qint64 timestamp() const { return m_timestamp; }
    void setClientId(const QString &id) { m_clientId = id; }
    QString clientId() const { return m_clientId; }
protected:
    qint64 m_timestamp = 0;
    QString m_clientId;
};

class DrawCommand : public Command {
public:
    DrawCommand(const DrawCmd& cmd, bool isStartOfStroke = false);
    void execute(CanvasModel* model) override;
    void undo(CanvasModel* model) override;
    CommandType type() const override { return CommandType::DrawCmd; }
    QJsonObject toJson() const override;
    bool isMergeable() const override;
    void merge(const Command* other) override;
    const DrawCmd& drawCmd() const { return m_cmd; }
    bool isStartOfStroke() const { return m_isStartOfStroke; }
private:
    DrawCmd m_cmd;
    int m_assignedId = -1;
    bool m_isStartOfStroke;
};

class DeleteDrawCommand : public Command {
public:
    DeleteDrawCommand(const QList<int>& cmdIds, const QList<DrawCmd>& cmds);
    void execute(CanvasModel* model) override;
    void undo(CanvasModel* model) override;
    CommandType type() const override { return CommandType::DeleteDrawCmds; }
    QJsonObject toJson() const override;
    const QList<int>& deletedCmdIds() const { return m_cmdIds; }
    const QList<DrawCmd>& deletedCmds() const { return m_deletedCmds; }
private:
    QList<int> m_cmdIds;
    QList<DrawCmd> m_deletedCmds;
    QMap<int, int> m_layerIdMap;
};

class MoveDrawCommand : public Command {
public:
    MoveDrawCommand(const QList<int>& cmdIds, const QPointF& offset);
    void execute(CanvasModel* model) override;
    void undo(CanvasModel* model) override;
    CommandType type() const override { return CommandType::MoveDrawCmds; }
    QJsonObject toJson() const override;
    const QList<int>& movedCmdIds() const { return m_cmdIds; }
    QPointF offset() const { return m_offset; }
private:
    QList<int> m_cmdIds;
    QPointF m_offset;
};

class LayerCommand : public Command {
public:
    int layerId() const { return m_layerId; }
protected:
    LayerCommand(int layerId) : m_layerId(layerId) {}
    int m_layerId;
};

class AddLayerCommand : public LayerCommand {
public:
    AddLayerCommand(const QString& name, int assignedId = -1);
    void execute(CanvasModel* model) override;
    void undo(CanvasModel* model) override;
    CommandType type() const override { return CommandType::AddLayer; }
    QJsonObject toJson() const override;
    QString layerName() const { return m_name; }
    int assignedId() const { return m_assignedId; }
private:
    QString m_name;
    int m_assignedId;
    Layer m_savedLayer;
};

class RemoveLayerCommand : public LayerCommand {
public:
    RemoveLayerCommand(int layerId);
    void execute(CanvasModel* model) override;
    void undo(CanvasModel* model) override;
    CommandType type() const override { return CommandType::RemoveLayer; }
    QJsonObject toJson() const override;
    const Layer& savedLayer() const { return m_savedLayer; }
    int oldCurrentLayerId() const { return m_oldCurrentLayerId; }
private:
    Layer m_savedLayer;
    int m_oldCurrentLayerId;
    int m_newCurrentLayerId;
};

class MoveLayerCommand : public LayerCommand {
public:
    MoveLayerCommand(int fromIndex, int toIndex);
    void execute(CanvasModel* model) override;
    void undo(CanvasModel* model) override;
    CommandType type() const override { return CommandType::MoveLayer; }
    QJsonObject toJson() const override;
    int fromIndex() const { return m_fromIndex; }
    int toIndex() const { return m_toIndex; }
private:
    int m_fromIndex;
    int m_toIndex;
};

class ClearLayerCommand : public LayerCommand {
public:
    ClearLayerCommand(int layerId);
    void execute(CanvasModel* model) override;
    void undo(CanvasModel* model) override;
    CommandType type() const override { return CommandType::ClearLayer; }
    QJsonObject toJson() const override;
    // 内联实现 savedCmds()，避免链接错误
    const QList<DrawCmd>& savedCmds() const { return m_savedCmds; }
private:
    QList<DrawCmd> m_savedCmds;
};

class CompositeCommand : public Command {
public:
    CompositeCommand(const QString& name);
    ~CompositeCommand();
    void addCommand(Command* cmd);
    void execute(CanvasModel* model) override;
    void undo(CanvasModel* model) override;
    CommandType type() const override;
    QJsonObject toJson() const override;
    const QList<Command*>& commands() const { return m_commands; }
    QString name() const { return m_name; }
private:
    QString m_name;
    QList<Command*> m_commands;
};
#endif // COMMAND_H