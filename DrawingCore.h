#ifndef DRAWINGCORE_H
#define DRAWINGCORE_H

#include <QObject>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QMouseEvent>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include "NetworkManager.h"
#include "BrushPreset.h"
#include <QElapsedTimer>

class CanvasModel;
class CanvasView;
class DrawingController;

enum class CmdType { Pen, Rect, Ellipse, EraserStroke, PenPoint };

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
};

struct Layer {
    int id;
    QString name;
    qreal opacity = 1.0;
    bool visible = true;
    QList<DrawCmd> commands;

    Layer(int i, const QString &n) : id(i), name(n) {}
};

class CanvasModel : public QObject {
    Q_OBJECT
public:
    explicit CanvasModel(QObject *parent = nullptr);

    int addLayer(const QString &name);
    void removeLayer(int layerId);
    void moveLayer(int fromIndex, int toIndex);  // 根据索引移动图层
    void setLayerOpacity(int layerId, qreal opacity);
    void setLayerVisible(int layerId, bool visible);
    void setCurrentLayer(int layerId) { if (getLayer(layerId)) m_currentLayerId = layerId; }
    int currentLayerId() const { return m_currentLayerId; }
    QList<Layer> layers() const { return m_layers; }
    Layer* getLayer(int layerId);
    int getLayerIndex(int layerId) const;
    QJsonArray layersToJson() const;
    void layersFromJson(const QJsonArray &array);
    void syncLayersToNetwork();

    int add(const DrawCmd &cmd, bool emitChanged = true);
    void undo();
    void redo() {}
    void clear();
    void clearLayer(int layerId);
    QList<DrawCmd> allCommands() const;
    QList<QJsonObject> getCommandData() const;
    bool canUndo() const;
    bool canRedo() const { return false; }
    void setNetworkManager(NetworkManager *net) { m_network = net; }
    void setRoomOwner(bool isOwner) { m_isRoomOwner = isOwner; }

    int addBrushPreset(const BrushPreset &preset);
    void removeBrushPreset(int id);
    BrushPreset* getBrushPreset(int id);
    QList<BrushPreset> brushPresets() const { return m_brushPresets; }
    void setBrushPresets(const QList<BrushPreset> &presets);
    QJsonArray brushPresetsToJson() const;
    void brushPresetsFromJson(const QJsonArray &array);
    int currentBrushPresetId() const { return m_currentBrushPresetId; }
    void setCurrentBrushPresetId(int id);

signals:
    void changed();
    void layerChanged();
    void commandAdded(const QJsonObject &cmdData);
    void layersSynced(const QJsonObject &layersData);
    void layerOperation(const QJsonObject &op);
    void brushPresetsChanged();

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
    QList<DrawCmd>& currentLayerCommands();

    void sendLayerOp(const QString &opType, int layerId, const QJsonObject &extra = QJsonObject());
    static void cmdToJson(QJsonObject &data, const DrawCmd &cmd);
    static void jsonToCmd(DrawCmd &cmd, const QJsonObject &data);
};

class CanvasView : public QGraphicsView {
    Q_OBJECT
public:
    explicit CanvasView(QWidget *parent = nullptr);
    void renderCommands(const QList<DrawCmd> &cmds);
    void renderCommand(const DrawCmd &cmd);
    void eraseAt(const QPointF &pos, int width);
    void applyEraser(const QPainterPath &path, int width);
    void setModel(CanvasModel *model) { m_model = model; }

signals:
    void mousePressed(QMouseEvent *event, QPointF scenePos);
    void mouseMoved(QMouseEvent *event, QPointF scenePos);
    void mouseReleased(QMouseEvent *event, QPointF scenePos);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    void doEraseAtPoint(const QPointF &pos, int width);
    CanvasModel *m_model = nullptr;
};

class DrawingController : public QObject {
    Q_OBJECT
public:
    explicit DrawingController(CanvasModel *model, CanvasView *view, QObject *parent = nullptr);
    enum Tool { Pen, Rect, Ellipse, EraserFull, EraserRealTime };

    QColor currentColor() const { return m_currentColor; }
    int currentOpacity() const { return m_currentColor.alpha(); }
    Tool currentTool() const { return m_currentTool; }
    void setCurrentBrushPreset(int presetId) { m_currentPresetId = presetId; }
    int currentBrushPresetId() const { return m_currentPresetId; }

public slots:
    void setTool(Tool tool);
    void setColor(const QColor &c) { m_currentColor = QColor(c.red(), c.green(), c.blue(), m_currentColor.alpha()); }
    void setOpacity(int o) { m_currentColor.setAlpha(o); }
    void setWidth(int w) { m_penWidth = w; }
    void undo() { m_model->undo(); }
    void redo() { m_model->redo(); }
    void clear() { m_model->clear(); }

private slots:
    void onViewPressed(QMouseEvent *e, QPointF pos);
    void onViewMoved(QMouseEvent *e, QPointF pos);
    void onViewReleased(QMouseEvent *e, QPointF pos);
    void onModelChanged();

private:
    CanvasModel *m_model;
    CanvasView *m_view;
    Tool m_currentTool = Pen;
    QColor m_currentColor = Qt::black;
    int m_penWidth = 2;
    int m_currentPresetId = 0;
    bool m_isDrawing = false;
    QPointF m_startPos;
    QPointF m_lastPos;
    QPainterPath m_currentPath;
    QGraphicsItem *m_previewItem = nullptr;
    QElapsedTimer m_lastTime;
    qreal m_lastPressure = 0.5;

    void removePreview();
    void createPreview(const QPointF &pos);
    void updatePreview(const QPointF &pos);
    void commitDrawing(const QPointF &endPos);
};

#endif