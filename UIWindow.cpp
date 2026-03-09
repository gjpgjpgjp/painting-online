#include "UIWindow.h"
#include "Command.h"
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QScrollBar>
#include <QtMath>
#include <QRandomGenerator>
#include <QPainterPathStroker>
#include <QDebug>

// ==================== CanvasView ====================

CanvasView::CanvasView(QWidget *parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setBackgroundBrush(Qt::white);
    setDragMode(QGraphicsView::NoDrag);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    scene()->setSceneRect(-5000, -5000, 10000, 10000);
    setCursor(Qt::ArrowCursor);
}

void CanvasView::renderCommands(const QList<DrawCmd> &cmds) {
    clearSceneItems();
    QTransform oldTransform = transform();
    for (const auto &cmd : cmds) {
        renderCommandToScene(cmd);
    }
    setTransform(oldTransform);
}

void CanvasView::renderCommand(const DrawCmd &cmd) {
    renderCommandToScene(cmd);
}

void CanvasView::renderCommandToScene(const DrawCmd &cmd) {
    if (m_cmdItemMap.contains(cmd.id)) {
        auto oldItem = m_cmdItemMap.take(cmd.id);
        scene()->removeItem(oldItem);
        delete oldItem;
    }

    QGraphicsItem *item = nullptr;
    QPen pen(cmd.color, cmd.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    switch (cmd.type) {
    case CmdType::Pen:
        if (!cmd.path.isEmpty()) {
            item = scene()->addPath(cmd.path, pen);
        }
        break;
    case CmdType::Rect: {
        QRectF r = QRectF(cmd.start, cmd.end).normalized();
        if (r.width() > 0 && r.height() > 0)
            item = scene()->addRect(r, pen, QBrush(cmd.color));
        break;
    }
    case CmdType::Ellipse: {
        QRectF r = QRectF(cmd.start, cmd.end).normalized();
        if (r.width() > 0 && r.height() > 0)
            item = scene()->addEllipse(r, pen, QBrush(cmd.color));
        break;
    }
    case CmdType::EraserStroke:
        // 擦除命令不再渲染
        break;
    case CmdType::PenPoint: {
        BrushPreset *preset = m_model ? m_model->getBrushPreset(cmd.presetId) : nullptr;
        if (preset && !preset->texture().isNull()) {
            int width = cmd.width;
            int margin = 2;
            int size = width + margin * 2;
            QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            preset->drawPoint(&p, QPointF(size/2.0, size/2.0), cmd.color, width);
            p.end();
            auto *pixItem = scene()->addPixmap(QPixmap::fromImage(img));
            pixItem->setPos(cmd.point - QPointF(size/2.0, size/2.0));
            pixItem->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
            item = pixItem;
        } else {
            qreal r = cmd.width / 2.0;
            item = scene()->addEllipse(cmd.point.x() - r, cmd.point.y() - r,
                                       cmd.width, cmd.width, Qt::NoPen, QBrush(cmd.color));
        }
        break;
    }
    case CmdType::Polygon: {
        QPen polyPen(cmd.color, 1);
        polyPen.setStyle(Qt::NoPen);
        QBrush brush(cmd.color);
        item = scene()->addPolygon(cmd.polygon, polyPen, brush);
        break;
    }
    }

    if (item) {
        item->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
        m_cmdItemMap[cmd.id] = item;
    }
}

void CanvasView::removeCommandItem(int cmdId) {
    if (m_cmdItemMap.contains(cmdId)) {
        auto item = m_cmdItemMap.take(cmdId);
        scene()->removeItem(item);
        delete item;
    }
}

void CanvasView::updateCommandItemPosition(int cmdId, const QPointF &offset) {
    if (m_cmdItemMap.contains(cmdId)) {
        m_cmdItemMap[cmdId]->setPos(offset);
    }
}

void CanvasView::clearSceneItems() {
    for (auto item : m_cmdItemMap) {
        scene()->removeItem(item);
        delete item;
    }
    m_cmdItemMap.clear();
}

void CanvasView::eraseAt(const QPointF &pos, int width) {
    Q_UNUSED(pos)
    Q_UNUSED(width)
}

void CanvasView::mousePressEvent(QMouseEvent *e) {
    if (m_spacePressed && e->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastPanPos = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }

    QPointF scenePos = mapToScene(e->pos());
    if (m_controller && m_controller->isDragModeEnabled() &&
        m_controller->hasSelection() && m_controller->selectionPath().contains(scenePos)) {
        m_controller->onStartDragSelection(scenePos);
        e->accept();
        return;
    }

    emit mousePressed(e, scenePos);
    QGraphicsView::mousePressEvent(e);
}

void CanvasView::mouseMoveEvent(QMouseEvent *e) {
    if (m_panning) {
        QPoint delta = m_lastPanPos - e->pos();
        m_lastPanPos = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + delta.y());
        e->accept();
        return;
    }

    QPointF scenePos = mapToScene(e->pos());
    if (m_controller && m_controller->isDraggingSelection()) {
        m_controller->onDragSelection(scenePos);
        e->accept();
        return;
    }

    emit mouseMoved(e, scenePos);
    QGraphicsView::mouseMoveEvent(e);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *e) {
    if (m_panning && (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton)) {
        m_panning = false;
        setCursor(m_spacePressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
        e->accept();
        return;
    }

    if (m_controller && m_controller->isDraggingSelection()) {
        m_controller->onEndDragSelection();
        e->accept();
        return;
    }

    emit mouseReleased(e, mapToScene(e->pos()));
    QGraphicsView::mouseReleaseEvent(e);
}

void CanvasView::wheelEvent(QWheelEvent *e) {
    QPointF scenePosBefore = mapToScene(e->position().toPoint());
    qreal factor = (e->angleDelta().y() > 0) ? m_zoomFactor : (1.0 / m_zoomFactor);
    qreal newScale = m_currentScale * factor;
    if (newScale < m_minScale) {
        factor = m_minScale / m_currentScale;
        newScale = m_minScale;
    } else if (newScale > m_maxScale) {
        factor = m_maxScale / m_currentScale;
        newScale = m_maxScale;
    }
    scale(factor, factor);
    m_currentScale = newScale;

    QPointF scenePosAfter = mapToScene(e->position().toPoint());
    QPointF offset = scenePosAfter - scenePosBefore;
    translate(offset.x(), offset.y());

    emit zoomChanged(m_currentScale);
    e->accept();
}

void CanvasView::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spacePressed = true;
        if (!m_panning) setCursor(Qt::OpenHandCursor);
        e->accept();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spacePressed = false;
        if (!m_panning) setCursor(Qt::ArrowCursor);
        e->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(e);
}

void CanvasView::resetZoom() {
    resetTransform();
    m_currentScale = 1.0;
    emit zoomChanged(m_currentScale);
}

void CanvasView::fitToWindow() {
    if (scene()->items().isEmpty()) return;
    QRectF bounds = scene()->itemsBoundingRect();
    if (bounds.isEmpty()) return;
    bounds.adjust(-50, -50, 50, 50);
    fitInView(bounds, Qt::KeepAspectRatio);
    QTransform t = transform();
    m_currentScale = t.m11();
    emit zoomChanged(m_currentScale);
}

void CanvasView::addPreviewPoint(const QPointF &pos, const QColor &color, int width) {
    m_previewPoints.append(qMakePair(pos, qMakePair(color, width)));
    viewport()->update();
}

void CanvasView::clearPreviewPoints() {
    m_previewPoints.clear();
    viewport()->update();
}

void CanvasView::drawForeground(QPainter *painter, const QRectF &rect) {
    QGraphicsView::drawForeground(painter, rect);
    Q_UNUSED(rect)
    painter->setPen(Qt::NoPen);
    for (const auto &p : m_previewPoints) {
        QPointF pos = p.first;
        QColor color = p.second.first;
        int width = p.second.second;
        qreal r = width / 2.0;
        painter->setBrush(color);
        painter->drawEllipse(pos, r, r);
    }
}

// ==================== DrawingController ====================

DrawingController::DrawingController(CanvasModel *model, CanvasView *view, QObject *parent)
    : QObject(parent), m_model(model), m_view(view)
{
    connect(m_view, &CanvasView::mousePressed, this, &DrawingController::onViewPressed);
    connect(m_view, &CanvasView::mouseMoved, this, &DrawingController::onViewMoved);
    connect(m_view, &CanvasView::mouseReleased, this, &DrawingController::onViewReleased);
    connect(m_model, &CanvasModel::changed, this, &DrawingController::onModelChanged);

    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(FLUSH_INTERVAL);
    m_flushTimer->setSingleShot(false);
    connect(m_flushTimer, &QTimer::timeout, this, &DrawingController::flushPendingPoints);
}

void DrawingController::setTool(Tool tool) {
    if (m_isDrawing) {
        removePreview();
        m_isDrawing = false;
        m_currentPath = QPainterPath();
        if (!m_pendingPoints.isEmpty()) flushPendingPoints();
        if (m_inGroup) {
            m_model->endCommandGroup();
            m_inGroup = false;
        }
    }

    if (m_lassoPreviewItem) {
        m_view->scene()->removeItem(m_lassoPreviewItem);
        delete m_lassoPreviewItem;
        m_lassoPreviewItem = nullptr;
    }

    if (m_selectionHighlightItem) {
        m_view->scene()->removeItem(m_selectionHighlightItem);
        delete m_selectionHighlightItem;
        m_selectionHighlightItem = nullptr;
    }

    clearSelection();
    m_currentTool = tool;
}

void DrawingController::clearSelection() {
    m_hasSelection = false;
    m_selectionPath = QPainterPath();
    m_selectedCmdIds.clear();

    if (m_selectionHighlightItem) {
        m_view->scene()->removeItem(m_selectionHighlightItem);
        delete m_selectionHighlightItem;
        m_selectionHighlightItem = nullptr;
    }

    clearDragPreview();
    emit selectionChanged();
}

void DrawingController::flushPendingPoints() {
    if (m_pendingPoints.isEmpty()) return;

    for (int i = 0; i < m_pendingPoints.size(); ++i) {
        const auto &cmd = m_pendingPoints[i];
        bool isStart = (i == 0 && m_isStrokeStart);
        m_model->executeDrawCommand(cmd, isStart);
    }

    m_pendingPoints.clear();
    m_view->clearPreviewPoints();
}

void DrawingController::addPointImmediate(const QPointF &pos, qreal pressure, bool batch) {
    BrushPreset *preset = m_model->getBrushPreset(m_currentPresetId);
    if (!preset) return;

    DrawCmd cmd;
    cmd.type = CmdType::PenPoint;
    cmd.point = pos;
    cmd.color = m_currentColor;
    cmd.width = preset->widthForPressure(pressure);
    cmd.presetId = m_currentPresetId;

    m_view->addPreviewPoint(pos, cmd.color, cmd.width);

    if (batch) {
        m_pendingPoints.append(cmd);
        if (m_pendingPoints.size() >= BATCH_SIZE) flushPendingPoints();
    } else {
        if (!m_pendingPoints.isEmpty()) flushPendingPoints();
        m_model->executeDrawCommand(cmd, m_isStrokeStart);
        m_isStrokeStart = false;
    }
}

qreal DrawingController::calculateSpacing(qreal pressure) const {
    return 3.0 + (1.0 - pressure) * 8.0;
}

void DrawingController::interpolatePoints(const QPointF &from, const QPointF &to,
                                          qreal pressureFrom, qreal pressureTo) {
    qreal totalDist = QLineF(from, to).length();
    qreal avgPressure = (pressureFrom + pressureTo) / 2.0;
    qreal spacing = calculateSpacing(avgPressure);
    int maxSteps = 20;
    int steps = qMin(maxSteps, qMax(1, qRound(totalDist / spacing)));
    QPointF lastDrawnPos = from;

    for (int i = 1; i <= steps; ++i) {
        qreal t = i / qreal(steps);
        t = t * t * (3.0 - 2.0 * t);
        QPointF pos = from + (to - from) * t;
        qreal pressure = pressureFrom + (pressureTo - pressureFrom) * t;
        pressure = qBound(0.2, pressure, 1.0);
        if (QLineF(pos, lastDrawnPos).length() >= spacing * 0.7 || i == steps) {
            addPointImmediate(pos, pressure, true);
            lastDrawnPos = pos;
        }
    }
}

void DrawingController::onViewPressed(QMouseEvent *e, QPointF pos) {
    if (e->button() != Qt::LeftButton) return;

    Layer* layer = m_model->getLayer(m_model->currentLayerId());
    if (!layer || !layer->visible) return;

    m_lastPos = pos;
    m_lastTime.start();
    m_lastPressure = 0.5;
    m_pendingPoints.clear();
    m_isStrokeStart = true;

    if (m_currentTool == LassoTool) {
        m_lassoPath = QPainterPath();
        m_lassoPath.moveTo(pos);
        m_isDrawing = true;
        if (m_hasSelection) clearSelection();

        if (m_lassoPreviewItem) {
            m_view->scene()->removeItem(m_lassoPreviewItem);
            delete m_lassoPreviewItem;
        }
        QPen lassoPen(Qt::red, 2, Qt::DashLine);
        m_lassoPreviewItem = m_view->scene()->addPath(m_lassoPath, lassoPen);
        return;
    }

    if (m_currentTool == Pen) {
        BrushPreset *preset = m_model->getBrushPreset(m_currentPresetId);
        if (preset && preset->usePressure()) {
            m_isDrawing = true;
            m_model->beginCommandGroup("PenStroke");
            m_inGroup = true;
            m_flushTimer->start();
            addPointImmediate(pos, 0.5, false);
            return;
        }
    }

    if (m_currentTool == EraserRealTime) {
        m_isDrawing = true;
        m_startPos = m_lastPos = pos;
        m_currentPath = QPainterPath();
        m_currentPath.moveTo(pos);

        // 修复：增加检测半径
        QList<int> ids = findCmdsAtPoint(pos, m_penWidth * 2);
        if (!ids.isEmpty()) {
            m_model->beginCommandGroup("EraserStroke");
            m_inGroup = true;
            DeleteDrawCommand *delCmd = new DeleteDrawCommand(ids, QList<DrawCmd>());
            m_model->executeCommand(delCmd);
        }
        return;
    }

    if (m_isDrawing) return;
    m_isDrawing = true;
    m_startPos = m_lastPos = pos;
    m_currentPath = QPainterPath();
    m_currentPath.moveTo(pos);

    if (m_currentTool != EraserFull) createPreview(pos);
}

void DrawingController::onViewMoved(QMouseEvent *e, QPointF pos) {
    if (!m_isDrawing) return;

    Layer* layer = m_model->getLayer(m_model->currentLayerId());
    if (!layer || !layer->visible) return;

    if (m_currentTool == LassoTool && (e->buttons() & Qt::LeftButton)) {
        m_lassoPath.lineTo(pos);
        if (m_lassoPreviewItem) {
            static_cast<QGraphicsPathItem*>(m_lassoPreviewItem)->setPath(m_lassoPath);
        }
        return;
    }

    if (m_currentTool == Pen) {
        BrushPreset *preset = m_model->getBrushPreset(m_currentPresetId);
        if (preset && preset->usePressure()) {
            qreal distance = QLineF(pos, m_lastPos).length();
            qreal elapsed = m_lastTime.elapsed();
            qreal speed = (elapsed > 5) ? distance / elapsed : 0;
            qreal currentPressure = qBound(0.2, 1.0 - speed / 10.0, 1.0);
            qreal spacing = calculateSpacing(currentPressure);
            if (distance >= spacing * 0.5) {
                interpolatePoints(m_lastPos, pos, m_lastPressure, currentPressure);
                m_lastPos = pos;
                m_lastPressure = currentPressure;
                m_lastTime.restart();
            }
            return;
        }
    }

    if (m_currentTool == EraserRealTime && (e->buttons() & Qt::LeftButton)) {
        // 修复：使用路径检测实现连续擦除
        QPainterPath erasePath;
        erasePath.moveTo(m_lastPos);
        erasePath.lineTo(pos);
        QList<int> ids = findCmdsAlongPath(erasePath, m_penWidth * 2);

        if (!ids.isEmpty()) {
            if (!m_inGroup) {
                m_model->beginCommandGroup("EraserStroke");
                m_inGroup = true;
            }
            DeleteDrawCommand *delCmd = new DeleteDrawCommand(ids, QList<DrawCmd>());
            m_model->executeCommand(delCmd);
        }
        m_lastPos = pos;
        return;
    }

    if (!(e->buttons() & Qt::LeftButton)) return;

    if (m_currentTool == EraserFull) {
        m_currentPath.lineTo(pos);
        m_lastPos = pos;
    } else {
        updatePreview(pos);
    }
}

void DrawingController::onViewReleased(QMouseEvent *e, QPointF pos) {
    if (e->button() != Qt::LeftButton) return;

    if (m_currentTool == LassoTool && m_isDrawing) {
        m_lassoPath.closeSubpath();
        m_selectionPath = m_lassoPath;
        m_hasSelection = true;
        m_isDrawing = false;

        if (m_lassoPreviewItem) {
            m_view->scene()->removeItem(m_lassoPreviewItem);
            delete m_lassoPreviewItem;
            m_lassoPreviewItem = nullptr;
        }

        updateSelectionHighlight();
        emit selectionChanged();
        return;
    }

    if (m_currentTool == Pen && m_isDrawing) {
        BrushPreset *preset = m_model->getBrushPreset(m_currentPresetId);
        if (preset && preset->usePressure()) {
            m_flushTimer->stop();
            addPointImmediate(pos, m_lastPressure, true);
            flushPendingPoints();
            m_isDrawing = false;
            if (m_inGroup) {
                m_model->endCommandGroup();
                m_inGroup = false;
            }
            return;
        }
    }

    if (m_currentTool == EraserRealTime) {
        if (m_isDrawing) {
            if (m_inGroup) {
                m_model->endCommandGroup();
                m_inGroup = false;
            }
            m_isDrawing = false;
            m_currentPath = QPainterPath();
        }
        return;
    }

    if (!m_isDrawing) return;

    if (m_currentTool != EraserFull) removePreview();
    commitDrawing(pos);
    m_isDrawing = false;
    m_currentPath = QPainterPath();
}

void DrawingController::onModelChanged() {
    m_view->renderCommands(m_model->allCommands());
}

void DrawingController::refreshAfterUndo() {
    m_view->renderCommands(m_model->allCommands());
}

void DrawingController::updateSelectionHighlight() {
    if (!m_hasSelection) return;

    if (m_selectionHighlightItem) {
        m_view->scene()->removeItem(m_selectionHighlightItem);
        delete m_selectionHighlightItem;
    }

    QPen pen(Qt::blue, 2, Qt::SolidLine);
    QBrush brush(QColor(0, 0, 255, 50));
    m_selectionHighlightItem = m_view->scene()->addPath(m_selectionPath, pen, brush);
}

void DrawingController::removePreview() {
    if (m_previewItem) {
        m_view->scene()->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }
}

void DrawingController::createPreview(const QPointF &pos) {
    QPen pen(m_currentColor, m_penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (m_currentTool == Pen) {
        auto *item = new QGraphicsPathItem();
        item->setPen(pen);
        m_view->scene()->addItem(item);
        m_previewItem = item;
        QPainterPath dot;
        dot.addEllipse(pos, m_penWidth/2.0, m_penWidth/2.0);
        item->setPath(dot);
    } else if (m_currentTool == Rect) {
        m_previewItem = m_view->scene()->addRect(QRectF(pos, pos), pen, QBrush(Qt::NoBrush));
    } else if (m_currentTool == Ellipse) {
        m_previewItem = m_view->scene()->addEllipse(QRectF(pos, pos), pen, QBrush(Qt::NoBrush));
    }
}

void DrawingController::updatePreview(const QPointF &pos) {
    if (!m_previewItem) return;
    if (m_currentTool == Pen) {
        m_currentPath.lineTo(pos);
        static_cast<QGraphicsPathItem*>(m_previewItem)->setPath(m_currentPath);
        m_lastPos = pos;
    } else {
        QRectF r(m_startPos, pos);
        if (m_currentTool == Rect)
            static_cast<QGraphicsRectItem*>(m_previewItem)->setRect(r.normalized());
        else if (m_currentTool == Ellipse)
            static_cast<QGraphicsEllipseItem*>(m_previewItem)->setRect(r.normalized());
    }
}

void DrawingController::commitDrawing(const QPointF &endPos) {
    DrawCmd cmd;
    cmd.color = m_currentColor;
    cmd.width = m_penWidth;

    switch (m_currentTool) {
    case Pen:
        if (!m_currentPath.isEmpty()) {
            cmd.type = CmdType::Pen;
            cmd.path = m_currentPath;
            m_model->executeDrawCommand(cmd, true);
            m_view->renderCommandToScene(cmd);
        }
        break;
    case Rect: {
        QRectF r = QRectF(m_startPos, endPos).normalized();
        if (r.width() > 0 || r.height() > 0) {
            cmd.type = CmdType::Rect;
            cmd.start = r.topLeft();
            cmd.end = r.bottomRight();
            m_model->executeDrawCommand(cmd, true);
            m_view->renderCommandToScene(cmd);
        }
        break;
    }
    case Ellipse: {
        QRectF r = QRectF(m_startPos, endPos).normalized();
        if (r.width() > 0 || r.height() > 0) {
            cmd.type = CmdType::Ellipse;
            cmd.start = r.topLeft();
            cmd.end = r.bottomRight();
            m_model->executeDrawCommand(cmd, true);
            m_view->renderCommandToScene(cmd);
        }
        break;
    }
    case EraserFull:
        if (!m_currentPath.isEmpty()) {
            // 修复：增加检测宽度
            QList<int> ids = findCmdsAlongPath(m_currentPath, m_penWidth * 2);
            if (!ids.isEmpty()) {
                DeleteDrawCommand *delCmd = new DeleteDrawCommand(ids, QList<DrawCmd>());
                m_model->executeCommand(delCmd);
            }
        }
        break;
    default:
        break;
    }
}

// ========== 修复的相交检测函数 ==========

QPainterPath DrawingController::getCmdHitPath(const DrawCmd &cmd) const {
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(cmd.width, 4));  // 最小检测宽度为4
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    QPainterPath hitPath;
    switch (cmd.type) {
    case CmdType::Pen:
        if (!cmd.path.isEmpty()) {
            hitPath = stroker.createStroke(cmd.path);
        }
        break;
    case CmdType::Rect: {
        QRectF r = QRectF(cmd.start, cmd.end).normalized();
        if (r.width() > 0 && r.height() > 0) {
            hitPath.addRect(r);
            hitPath = stroker.createStroke(hitPath);
        }
        break;
    }
    case CmdType::Ellipse: {
        QRectF r = QRectF(cmd.start, cmd.end).normalized();
        if (r.width() > 0 && r.height() > 0) {
            hitPath.addEllipse(r);
            hitPath = stroker.createStroke(hitPath);
        }
        break;
    }
    case CmdType::PenPoint: {
        // 修复：创建带实际绘制宽度的检测区域
        qreal radius = cmd.width / 2.0;
        hitPath.addEllipse(cmd.point, radius, radius);
        // 添加边距确保检测可靠
        QPainterPathStroker marginStroker;
        marginStroker.setWidth(4);
        hitPath = marginStroker.createStroke(hitPath);
        hitPath.addEllipse(cmd.point, radius, radius);
        break;
    }
    case CmdType::Polygon: {
        hitPath.addPolygon(cmd.polygon);
        break;
    }
    default:
        break;
    }

    return hitPath;
}

bool DrawingController::cmdIntersectsPath(const DrawCmd &cmd, const QPainterPath &path) const {
    if (cmd.type == CmdType::EraserStroke)
        return false;

    QPainterPath cmdPath = getCmdHitPath(cmd);
    if (cmdPath.isEmpty()) return false;

    // 对于 PenPoint，使用更宽松的相交检测
    if (cmd.type == CmdType::PenPoint) {
        if (path.contains(cmd.point)) return true;
    }

    return path.intersects(cmdPath) || cmdPath.intersects(path);
}

bool DrawingController::cmdIntersectsSelection(const DrawCmd &cmd, const QPainterPath &selection) const {
    if (cmd.type == CmdType::EraserStroke)
        return false;

    QPainterPath cmdPath = getCmdHitPath(cmd);
    if (cmdPath.isEmpty()) return false;

    // 修复：对于 PenPoint，使用填充区域检测
    if (cmd.type == CmdType::PenPoint) {
        // 检查点是否在选区内
        if (selection.contains(cmd.point)) return true;
        // 检查选区边界是否与点区域相交
        QPainterPath pointPath;
        qreal r = cmd.width / 2.0 + 2;  // 添加边距
        pointPath.addEllipse(cmd.point, r, r);
        return selection.intersects(pointPath);
    }

    // 对于其他类型，检查是否与选区路径相交
    return selection.intersects(cmdPath);
}

bool DrawingController::cmdIsFullyInsideSelection(const DrawCmd &cmd, const QPainterPath &selection) const {
    if (cmd.type == CmdType::EraserStroke)
        return false;

    QPainterPath cmdPath = getCmdHitPath(cmd);
    if (cmdPath.isEmpty()) return false;

    QPointF center;
    switch (cmd.type) {
    case CmdType::PenPoint:
        center = cmd.point;
        break;
    case CmdType::Pen:
        center = cmd.path.boundingRect().center();
        break;
    case CmdType::Rect:
    case CmdType::Ellipse:
        center = QRectF(cmd.start, cmd.end).center();
        break;
    case CmdType::Polygon:
        center = cmd.polygon.boundingRect().center();
        break;
    default:
        return false;
    }

    return selection.contains(center);
}

// ========== 修复的擦除辅助函数 ==========

QList<int> DrawingController::findCmdsAtPoint(const QPointF &pos, int width) const {
    QList<int> ids;
    qreal radius = width / 2.0;

    // 创建圆形检测区域
    QPainterPath hitPath;
    hitPath.addEllipse(pos, radius, radius);

    // 添加边距确保检测可靠
    QPainterPathStroker stroker;
    stroker.setWidth(4);
    hitPath = stroker.createStroke(hitPath);
    hitPath.addEllipse(pos, radius, radius);

    // 修复：使用 auto& 避免拷贝
    for (const auto &layer : m_model->layers()) {
        for (const auto &cmd : layer.commands) {
            if (cmdIntersectsPath(cmd, hitPath))
                ids.append(cmd.id);
        }
    }
    return ids;
}

QList<int> DrawingController::findCmdsAlongPath(const QPainterPath &path, int width) const {
    QList<int> ids;

    // 创建带宽度的检测路径
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath hitPath = stroker.createStroke(path);

    // 修复：使用 auto& 避免拷贝
    for (const auto &layer : m_model->layers()) {
        for (const auto &cmd : layer.commands) {
            if (cmdIntersectsPath(cmd, hitPath))
                ids.append(cmd.id);
        }
    }
    return ids;
}

// ========== 选区操作 ==========

void DrawingController::onPaintSelection() {
    if (!m_hasSelection) return;

    DrawCmd cmd;
    cmd.type = CmdType::Polygon;
    cmd.color = m_currentColor;
    cmd.width = 1;
    cmd.polygon = m_selectionPath.toFillPolygon();

    m_model->executeDrawCommand(cmd, true);
    m_view->renderCommandToScene(cmd);
}

void DrawingController::onClearSelection() {
    if (!m_hasSelection) return;

    QList<int> idsToDelete;
    // 修复：使用 auto& 避免拷贝
    for (const auto &layer : m_model->layers()) {
        for (const auto &cmd : layer.commands) {
            // 使用改进的选区相交检测
            if (cmdIntersectsSelection(cmd, m_selectionPath))
                idsToDelete.append(cmd.id);
        }
    }

    if (idsToDelete.isEmpty()) {
        clearSelection();
        return;
    }

    DeleteDrawCommand* delCmd = new DeleteDrawCommand(idsToDelete, QList<DrawCmd>());
    m_model->executeCommand(delCmd);

    clearSelection();
}

void DrawingController::onStartDragSelection(const QPointF &pos) {
    if (!m_hasSelection) return;
    m_dragStartPos = pos;
    m_draggingSelection = true;
    m_selectedCmdIds.clear();

    // 收集所有与选区相交的命令
    // 修复：使用 auto& 避免拷贝
    for (const auto &layer : m_model->layers()) {
        for (const auto &cmd : layer.commands) {
            if (cmdIntersectsSelection(cmd, m_selectionPath))
                m_selectedCmdIds.append(cmd.id);
        }
    }

    if (m_selectedCmdIds.isEmpty()) {
        m_draggingSelection = false;
        return;
    }

    m_dragPreviewOffset = QPointF();
    createDragPreview();
}

void DrawingController::createDragPreview() {
    for (int cmdId : m_selectedCmdIds) {
        auto it = m_view->m_cmdItemMap.find(cmdId);
        if (it == m_view->m_cmdItemMap.end()) continue;

        auto *orig = it.value();
        QGraphicsItem *preview = nullptr;

        // 修复：使用 qgraphicsitem_cast 替代 dynamic_cast，支持所有类型
        if (auto *p = qgraphicsitem_cast<QGraphicsPathItem*>(orig))
            preview = m_view->scene()->addPath(p->path(), p->pen(), p->brush());
        else if (auto *r = qgraphicsitem_cast<QGraphicsRectItem*>(orig))
            preview = m_view->scene()->addRect(r->rect(), r->pen(), r->brush());
        else if (auto *e = qgraphicsitem_cast<QGraphicsEllipseItem*>(orig))
            preview = m_view->scene()->addEllipse(e->rect(), e->pen(), e->brush());
        else if (auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem*>(orig)) {
            // 支持 PenPoint 的 pixmap item
            preview = m_view->scene()->addPixmap(pix->pixmap());
            preview->setPos(orig->pos());
        }

        if (preview) {
            preview->setOpacity(0.5);
            m_dragPreviewItems.append(preview);
        }
    }
}

void DrawingController::clearDragPreview() {
    for (auto *item : m_dragPreviewItems) {
        m_view->scene()->removeItem(item);
        delete item;
    }
    m_dragPreviewItems.clear();
}

void DrawingController::onDragSelection(const QPointF &pos) {
    if (!m_draggingSelection) return;
    m_dragPreviewOffset = pos - m_dragStartPos;

    for (auto *item : m_dragPreviewItems)
        item->setPos(m_dragPreviewOffset);

    if (m_selectionHighlightItem)
        m_selectionHighlightItem->setPos(m_dragPreviewOffset);
}

void DrawingController::onEndDragSelection() {
    if (!m_draggingSelection) return;

    auto finalOffset = m_dragPreviewOffset;
    clearDragPreview();

    if (!finalOffset.isNull() && !m_selectedCmdIds.isEmpty()) {
        MoveDrawCommand* moveCmd = new MoveDrawCommand(m_selectedCmdIds, finalOffset);
        m_model->executeCommand(moveCmd);

        m_selectionPath.translate(finalOffset);
        if (m_selectionHighlightItem) {
            m_selectionHighlightItem->setPos(0, 0);
            static_cast<QGraphicsPathItem*>(m_selectionHighlightItem)->setPath(m_selectionPath);
        }
    }
    m_draggingSelection = false;
    m_selectedCmdIds.clear();
    m_dragPreviewOffset = QPointF();
}