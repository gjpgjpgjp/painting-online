#ifndef DRAWINGCORE_H
#define DRAWINGCORE_H

#include <QObject>
#include <QPainterPath>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <QStack>
#include "NetworkManager.h"
#include "BrushPreset.h"

// 前向声明
class Command;

// 简单的画布元数据结构（内联定义，避免不完整类型问题）
struct SimpleCanvasMetadata {
    QSize size;
    QColor backgroundColor = Qt::white;
    QString createdBy;
    qint64 createdAt = 0;
    qint64 modifiedAt = 0;
    QString canvasName;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["width"] = size.width();
        obj["height"] = size.height();
        obj["backgroundColor"] = backgroundColor.name(QColor::HexArgb);
        obj["createdBy"] = createdBy;
        obj["createdAt"] = createdAt;
        obj["modifiedAt"] = modifiedAt;
        obj["canvasName"] = canvasName;
        return obj;
    }

    void fromJson(const QJsonObject &obj) {
        int width = obj["width"].toInt(800);
        int height = obj["height"].toInt(600);
        size = QSize(width, height);
        backgroundColor = QColor(obj["backgroundColor"].toString("#FFFFFFFF"));
        createdBy = obj["createdBy"].toString();
        createdAt = obj["createdAt"].toVariant().toLongLong();
        modifiedAt = obj["modifiedAt"].toVariant().toLongLong();
        canvasName = obj["canvasName"].toString("未命名画布");
    }
};

enum class CmdType { Pen, Rect, Ellipse, EraserStroke, PenPoint, Polygon };

struct DrawCmd {
    CmdType type = CmdType::Pen;
    int id = 0;
    int layerId = 0;
    int presetId = 0;
    int width = 2;
    QPainterPath path;
    QPointF start;
    QPointF end;
    QPointF point;
    QColor color = Qt::black;
    QPolygonF polygon;
};

struct Layer {
    int id = 0;
    QString name;
    qreal opacity = 1.0;
    bool visible = true;
    QList<DrawCmd> commands;

    Layer() = default;
    Layer(int i, const QString &n) : id(i), name(n) {}
};

class CanvasModel : public QObject {
    Q_OBJECT
public:
    void ensureNextLayerId(int usedId) {
        if (usedId >= m_nextLayerId) {
            m_nextLayerId = usedId + 1;
        }
    }
    explicit CanvasModel(QObject *parent = nullptr);
    void sendLayerOp(const QString &opType, int layerId, const QJsonObject &extra = QJsonObject());
    // 图层管理（命令版本）
    int addLayer(const QString &name);
    void removeLayer(int layerId);
    void moveLayer(int fromIndex, int toIndex);
    void setLayerOpacity(int layerId, qreal opacity);
    void setLayerVisible(int layerId, bool visible);
    void setCurrentLayer(int layerId) { if (getLayer(layerId)) m_currentLayerId = layerId; }
    int currentLayerId() const { return m_currentLayerId; }
    QList<Layer>& layers() { return m_layers; }
    const QList<Layer>& layers() const { return m_layers; }
    Layer* getLayer(int layerId);
    int getLayerIndex(int layerId) const;
    QJsonArray layersToJson() const;
    void layersFromJson(const QJsonArray &array);
    void syncLayersToNetwork();

    // 直接操作图层（不经过命令，用于命令内部实现）
    int addLayerDirect(const QString &name);
    int addLayerDirect(const Layer &layer);
    void removeLayerDirect(int layerId);
    void moveLayerDirect(int fromIndex, int toIndex);

    // 绘制指令（直接添加，用于网络同步等）
    int add(const DrawCmd &cmd, bool emitChanged = true);

    // 通过命令系统添加绘制指令
    void executeDrawCommand(const DrawCmd &cmd, bool isStartOfStroke = false);

    // 执行命令（自动加入撤销栈）
    void executeCommand(Command* cmd);

    // 撤销重做
    void undo();
    void redo();
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    void clearHistory();

    // 命令组
    void beginCommandGroup(const QString& name);
    void endCommandGroup();
    bool isInCommandGroup() const { return m_groupStackDepth > 0; }

    // 清空操作
    void clear();
    void clearLayer(int layerId);

    QList<DrawCmd> allCommands() const;
    QList<QJsonObject> getCommandData() const;

    // 网络
    void setNetworkManager(NetworkManager *net) { m_network = net; }
    void setRoomOwner(bool isOwner) { m_isRoomOwner = isOwner; }
    void setClientId(const QString &id) { m_clientId = id; }
    QString clientId() const { return m_clientId; }
    bool isRoomOwner() const { return m_isRoomOwner; }
    NetworkManager* networkManager() const { return m_network; }

    // 笔刷预设
    int addBrushPreset(const BrushPreset &preset);
    void removeBrushPreset(int id);
    BrushPreset* getBrushPreset(int id);
    QList<BrushPreset> brushPresets() const { return m_brushPresets; }
    void setBrushPresets(const QList<BrushPreset> &presets);
    QJsonArray brushPresetsToJson() const;
    void brushPresetsFromJson(const QJsonArray &array);
    int currentBrushPresetId() const { return m_currentBrushPresetId; }
    void setCurrentBrushPresetId(int id);

    // 选区操作支持（直接修改，但可通过命令包装）
    void deleteCommands(const QList<int> &cmdIds);
    void moveCommands(const QList<int> &cmdIds, const QPointF &offset);
    void emitChanged() { emit changed(); }

    bool suppressEmit() const { return m_suppressEmit; }
    void setSuppressEmit(bool suppress) { m_suppressEmit = suppress; }

    bool isOwnCommand(const QJsonObject &cmdData) const;
    QJsonObject getFullState() const;
    void applyFullState(const QJsonObject &state);

    // 增量同步（用于大画布优化）
    QJsonObject getDeltaState(qint64 sinceTimestamp) const;

    // 画布元数据 - 使用简单结构体
    void setCanvasMetadata(const SimpleCanvasMetadata &meta) { m_metadata = meta; }
    SimpleCanvasMetadata canvasMetadata() const { return m_metadata; }

    // 重置画布（用于新建画布）
    void resetCanvas(const QSize &size, const QString &name);

    // 获取画布尺寸
    QSize canvasSize() const { return m_metadata.size; }

    static void cmdToJson(QJsonObject &data, const DrawCmd &cmd);
    static void jsonToCmd(DrawCmd &cmd, const QJsonObject &data);
    bool findCommandById(int id, Layer *&outLayer, int &outIndex);

    // 新增：辅助函数，将命令恢复到对应图层（带重复检查和ID排序）
    void restoreCommandToLayer(const DrawCmd &cmd);

signals:
    void changed();
    void layerChanged();
    void commandAdded(const QJsonObject &cmdData);
    void layersSynced(const QJsonObject &layersData);
    void layerOperation(const QJsonObject &op);
    void brushPresetsChanged();
    void undoRedoStateChanged(bool canUndo, bool canRedo);
    // 画布重置信号
    void canvasReset();
    void fullStateReceived(const QJsonObject &state);

public slots:
    void onNetworkCommand(const QJsonObject &obj);
    void onNetworkLayerOp(const QJsonObject &op);

private:
    QList<Layer> m_layers;
    int m_currentLayerId = 0;
    int m_nextCmdId = 1;
    int m_nextLayerId = 1;
    int m_nextPresetId = 1;
    int m_currentBrushPresetId = 0;
    NetworkManager *m_network = nullptr;
    bool m_suppressEmit = false;
    bool m_isRoomOwner = false;
    QList<BrushPreset> m_brushPresets;
    QString m_clientId;

    QList<DrawCmd>& currentLayerCommands();


    // 命令历史
    QStack<Command*> m_undoStack;
    QStack<Command*> m_redoStack;
    QList<Command*> m_currentGroup;
    int m_groupStackDepth = 0;
    QString m_currentGroupName;
    static constexpr int MAX_HISTORY_SIZE = 100;

    void pushUndo(Command* cmd);
    void clearStack(QStack<Command*>& stack);

    // 使用简单结构体而非指针
    SimpleCanvasMetadata m_metadata;

    void removeCommandsDirect(const QList<int> &cmdIds);
    void moveCommandsDirect(const QList<int> &cmdIds, const QPointF &offset);
};

#endif // DRAWINGCORE_H