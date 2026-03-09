#ifndef UIWINDOW_H
#define UIWINDOW_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QTimer>
#include <QGraphicsPixmapItem>
#include "DrawingCore.h"

class DrawingController;

class CanvasView : public QGraphicsView {
    Q_OBJECT
public:
    explicit CanvasView(QWidget *parent = nullptr);
    void renderCommands(const QList<DrawCmd> &cmds);
    void renderCommand(const DrawCmd &cmd);
    void renderCommandToScene(const DrawCmd &cmd);
    void eraseAt(const QPointF &pos, int width);
    void setModel(CanvasModel *model) { m_model = model; }
    void setController(DrawingController *controller) { m_controller = controller; }

    void removeCommandItem(int cmdId);
    void updateCommandItemPosition(int cmdId, const QPointF &offset);
    void clearSceneItems();

    void resetZoom();
    void fitToWindow();
    qreal currentScale() const { return m_currentScale; }

    void addPreviewPoint(const QPointF &pos, const QColor &color, int width);
    void clearPreviewPoints();

signals:
    void mousePressed(QMouseEvent *event, QPointF scenePos);
    void mouseMoved(QMouseEvent *event, QPointF scenePos);
    void mouseReleased(QMouseEvent *event, QPointF scenePos);
    void zoomChanged(qreal scale);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    CanvasModel *m_model = nullptr;
    DrawingController *m_controller = nullptr;

    qreal m_currentScale = 1.0;
    const qreal m_minScale = 0.1;
    const qreal m_maxScale = 5.0;
    const qreal m_zoomFactor = 1.15;

    bool m_spacePressed = false;
    bool m_panning = false;
    QPoint m_lastPanPos;

    QList<QPair<QPointF, QPair<QColor, int>>> m_previewPoints;

public:
    QHash<int, QGraphicsItem*> m_cmdItemMap;
};

class DrawingController : public QObject {
    Q_OBJECT
public:
    explicit DrawingController(CanvasModel *model, CanvasView *view, QObject *parent = nullptr);

    enum Tool { Pen, Rect, Ellipse, EraserFull, EraserRealTime, LassoTool };

    QColor currentColor() const { return m_currentColor; }
    int currentOpacity() const { return m_currentColor.alpha(); }
    Tool currentTool() const { return m_currentTool; }
    void setCurrentBrushPreset(int presetId) { m_currentPresetId = presetId; }
    int currentBrushPresetId() const { return m_currentPresetId; }

    bool hasSelection() const { return m_hasSelection; }
    QPainterPath selectionPath() const { return m_selectionPath; }
    QPainterPath lassoPath() const { return m_lassoPath; }
    bool isDrawingLasso() const { return m_isDrawing && m_currentTool == LassoTool; }
    bool isDragModeEnabled() const { return m_dragModeEnabled; }
    bool isDraggingSelection() const { return m_draggingSelection; }
    void setDragModeEnabled(bool enabled) { m_dragModeEnabled = enabled; }

public slots:
    void setTool(Tool tool);
    void setColor(const QColor &c) { m_currentColor = QColor(c.red(), c.green(), c.blue(), m_currentColor.alpha()); }
    void setOpacity(int o) { m_currentColor.setAlpha(o); }
    void setWidth(int w) { m_penWidth = w; }

    void undo() { m_model->undo(); }
    void redo() { m_model->redo(); }

    void clear() {
        m_model->clearLayer(m_model->currentLayerId());
        refreshAfterUndo();
    }

    void onPaintSelection();
    void onClearSelection();
    void onStartDragSelection(const QPointF &pos);
    void onDragSelection(const QPointF &pos);
    void onEndDragSelection();
    void clearSelection();

signals:
    void selectionChanged();

private slots:
    void onViewPressed(QMouseEvent *e, QPointF pos);
    void onViewMoved(QMouseEvent *e, QPointF pos);
    void onViewReleased(QMouseEvent *e, QPointF pos);
    void onModelChanged();
    void flushPendingPoints();

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

    bool m_isStrokeStart = false;
    bool m_inGroup = false;

    QList<DrawCmd> m_pendingPoints;
    QTimer *m_flushTimer = nullptr;
    static constexpr int FLUSH_INTERVAL = 16;
    static constexpr int BATCH_SIZE = 5;

    QPainterPath m_lassoPath;
    QPainterPath m_selectionPath;
    bool m_hasSelection = false;
    QList<int> m_selectedCmdIds;
    QPointF m_dragStartPos;
    bool m_draggingSelection = false;
    QPointF m_dragPreviewOffset;
    bool m_dragModeEnabled = false;

    QGraphicsPathItem *m_lassoPreviewItem = nullptr;
    QGraphicsPathItem *m_selectionHighlightItem = nullptr;
    QList<QGraphicsItem*> m_dragPreviewItems;

    // 新增辅助函数声明
    QPainterPath getCmdHitPath(const DrawCmd &cmd) const;
    QList<int> findCmdsAtPoint(const QPointF &pos, int width) const;
    QList<int> findCmdsAlongPath(const QPainterPath &path, int width) const;
    bool cmdIntersectsPath(const DrawCmd &cmd, const QPainterPath &path) const;
    bool cmdIntersectsSelection(const DrawCmd &cmd, const QPainterPath &selection) const;
    bool cmdIsFullyInsideSelection(const DrawCmd &cmd, const QPainterPath &selection) const;

    void removePreview();
    void createPreview(const QPointF &pos);
    void updatePreview(const QPointF &pos);
    void commitDrawing(const QPointF &endPos);
    void interpolatePoints(const QPointF &from, const QPointF &to, qreal pressureFrom, qreal pressureTo);
    qreal calculateSpacing(qreal pressure) const;
    void addPointImmediate(const QPointF &pos, qreal pressure, bool batch = true);

    void refreshAfterUndo();
    void updateSelectionHighlight();
    void clearDragPreview();
    void createDragPreview();
};

#endif // UIWINDOW_H