#include "DrawingCore.h"
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QJsonDocument>
#include <QDataStream>

// ==================== CanvasModel ====================

CanvasModel::CanvasModel(QObject *parent) : QObject(parent) {
    addLayer("背景层");
    setCurrentLayer(0);
}

int CanvasModel::addLayer(const QString &name) {
    Layer layer(m_nextLayerId++, name);
    m_layers.append(layer);
    emit layerChanged();
    if (!m_suppressEmit && m_network) {
        QJsonObject op;
        op["type"] = "layerOp";
        op["opType"] = "add";
        op["newId"] = layer.id;
        op["name"] = name;
        op["opacity"] = 1.0;
        op["visible"] = true;
        m_network->sendCommand(op);
    }
    return layer.id;
}

void CanvasModel::removeLayer(int layerId) {
    if (m_layers.size() <= 1) return;
    m_layers.removeIf([layerId](const Layer &l) { return l.id == layerId; });
    if (m_currentLayerId == layerId) m_currentLayerId = m_layers.first().id;
    emit layerChanged();
    sendLayerOp("remove", layerId);
}

int CanvasModel::getLayerIndex(int layerId) const {
    for (int i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].id == layerId) return i;
    }
    return -1;
}

void CanvasModel::moveLayer(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= m_layers.size()) return;
    if (toIndex < 0 || toIndex >= m_layers.size()) return;
    if (fromIndex == toIndex) return;

    // 移动图层
    Layer layer = m_layers.takeAt(fromIndex);
    m_layers.insert(toIndex, layer);

    // 更新当前图层索引跟踪
    m_currentLayerId = layer.id;

    emit layerChanged();
    emit changed();

    // 网络同步
    QJsonObject extra;
    QJsonArray order;
    for (const auto &l : m_layers) order.append(l.id);
    extra["order"] = order;
    extra["fromIndex"] = fromIndex;
    extra["toIndex"] = toIndex;
    sendLayerOp("move", layer.id, extra);
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

Layer* CanvasModel::getLayer(int layerId) {
    for (auto &l : m_layers) if (l.id == layerId) return &l;
    return nullptr;
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

int CanvasModel::add(const DrawCmd &cmd, bool emitChanged) {
    DrawCmd newCmd = cmd;
    newCmd.id = m_nextCmdId++;
    newCmd.layerId = m_currentLayerId;
    newCmd.presetId = m_currentBrushPresetId;
    if (Layer *layer = getLayer(m_currentLayerId)) layer->commands.append(newCmd);

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
    if (emitChanged) emit changed();
    return newCmd.id;
}

void CanvasModel::undo() {
    if (Layer *layer = getLayer(m_currentLayerId)) {
        if (!layer->commands.isEmpty()) {
            layer->commands.removeLast();
            emit changed();
        }
    }
}

void CanvasModel::clear() {
    for (auto &l : m_layers) l.commands.clear();
    emit changed();
    sendLayerOp("clear", 0);
}

void CanvasModel::clearLayer(int layerId) {
    if (Layer *layer = getLayer(layerId)) {
        layer->commands.clear();
        emit changed();
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

bool CanvasModel::canUndo() const {
    if (const Layer *layer = const_cast<CanvasModel*>(this)->getLayer(m_currentLayerId))
        return !layer->commands.isEmpty();
    return false;
}

void CanvasModel::onNetworkCommand(const QJsonObject &obj) {
    if (obj["type"].toString() != "cmd" || obj["_fromSelf"].toBool()) return;
    DrawCmd cmd;
    jsonToCmd(cmd, obj["data"].toObject());
    if (Layer *layer = getLayer(cmd.layerId)) {
        layer->commands.append(cmd);
        emit changed();
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
        removeLayer(layerId);
    } else if (opType == "visible") {
        setLayerVisible(layerId, op["visible"].toBool());
    } else if (opType == "opacity") {
        setLayerOpacity(layerId, op["opacity"].toDouble());
    } else if (opType == "clear") {
        clear();
    } else if (opType == "fullSync") {
        layersFromJson(op["layers"].toArray());
        m_currentLayerId = op["currentLayerId"].toInt(m_layers.first().id);
    } else if (opType == "move") {
        // 根据服务器传来的顺序重建图层列表
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
        // 添加可能遗漏的图层
        for (const auto &l : m_layers) {
            bool found = false;
            for (const auto &v : order) {
                if (v.toInt() == l.id) { found = true; break; }
            }
            if (!found) newLayers.append(l);
        }
        m_layers = newLayers;
        m_currentLayerId = layerId; // 保持被移动图层为当前选中
        emit layerChanged();
        emit changed();
    }

    m_suppressEmit = false;
}

// 笔刷预设管理
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

// ==================== CanvasView ====================

CanvasView::CanvasView(QWidget *parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setBackgroundBrush(Qt::white);
}

void CanvasView::renderCommands(const QList<DrawCmd> &cmds) {
    scene()->clear();
    for (const auto &cmd : cmds) renderCommand(cmd);
}

void CanvasView::renderCommand(const DrawCmd &cmd) {
    QPen pen(cmd.color, cmd.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (cmd.type) {
    case CmdType::Pen:
        if (!cmd.path.isEmpty()) scene()->addPath(cmd.path, pen);
        break;
    case CmdType::Rect: {
        QRectF r = QRectF(cmd.start, cmd.end).normalized();
        if (r.width() > 0 && r.height() > 0)
            scene()->addRect(r, pen, QBrush(cmd.color));
        break;
    }
    case CmdType::Ellipse: {
        QRectF r = QRectF(cmd.start, cmd.end).normalized();
        if (r.width() > 0 && r.height() > 0)
            scene()->addEllipse(r, pen, QBrush(cmd.color));
        break;
    }
    case CmdType::EraserStroke:
        applyEraser(cmd.path, cmd.width);
        break;
    case CmdType::PenPoint: {
        BrushPreset *preset = m_model ? m_model->getBrushPreset(cmd.presetId) : nullptr;
        if (preset) {
            int margin = 2;
            int size = cmd.width + margin * 2;
            QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            preset->drawPoint(&p, QPointF(size/2.0, size/2.0), cmd.color, cmd.width);
            p.end();
            auto *item = scene()->addPixmap(QPixmap::fromImage(img));
            item->setPos(cmd.point - QPointF(size/2.0, size/2.0));
        } else {
            scene()->addEllipse(cmd.point.x() - cmd.width/2.0, cmd.point.y() - cmd.width/2.0,
                                cmd.width, cmd.width, Qt::NoPen, QBrush(cmd.color));
        }
        break;
    }
    }
}

void CanvasView::eraseAt(const QPointF &pos, int width) {
    QPainterPath p;
    p.moveTo(pos);
    applyEraser(p, width);
}

void CanvasView::applyEraser(const QPainterPath &path, int width) {
    if (path.isEmpty()) return;
    qreal len = path.length();
    qreal step = qMax(1.0, width / 2.0);
    doEraseAtPoint(path.pointAtPercent(0), width);
    for (qreal d = step; d <= len; d += step)
        doEraseAtPoint(path.pointAtPercent(path.percentAtLength(d)), width);
}

void CanvasView::mousePressEvent(QMouseEvent *e) {
    emit mousePressed(e, mapToScene(e->pos()));
    QGraphicsView::mousePressEvent(e);
}

void CanvasView::mouseMoveEvent(QMouseEvent *e) {
    emit mouseMoved(e, mapToScene(e->pos()));
    QGraphicsView::mouseMoveEvent(e);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *e) {
    emit mouseReleased(e, mapToScene(e->pos()));
    QGraphicsView::mouseReleaseEvent(e);
}

void CanvasView::doEraseAtPoint(const QPointF &pos, int width) {
    QRectF hitRect(pos.x() - width/2.0, pos.y() - width/2.0, width, width);
    for (auto *item : scene()->items(hitRect)) {
        if (item->sceneBoundingRect().contains(pos)) {
            scene()->removeItem(item);
            delete item;
        }
    }
}

// ==================== DrawingController ====================

DrawingController::DrawingController(CanvasModel *model, CanvasView *view, QObject *parent)
    : QObject(parent), m_model(model), m_view(view) {
    connect(m_view, &CanvasView::mousePressed, this, &DrawingController::onViewPressed);
    connect(m_view, &CanvasView::mouseMoved, this, &DrawingController::onViewMoved);
    connect(m_view, &CanvasView::mouseReleased, this, &DrawingController::onViewReleased);
    connect(m_model, &CanvasModel::changed, this, &DrawingController::onModelChanged);
}

void DrawingController::setTool(Tool tool) {
    if (m_isDrawing) {
        removePreview();
        m_isDrawing = false;
        m_currentPath = QPainterPath();
    }
    m_currentTool = tool;
}

void DrawingController::onViewPressed(QMouseEvent *e, QPointF pos) {
    if (e->button() != Qt::LeftButton) return;

    m_lastPos = pos;
    m_lastTime.start();
    m_lastPressure = 0.5;

    if (m_currentTool == Pen) {
        BrushPreset *preset = m_model->getBrushPreset(m_currentPresetId);
        if (preset && preset->usePressure()) {
            m_isDrawing = true;
            DrawCmd cmd;
            cmd.type = CmdType::PenPoint;
            cmd.point = pos;
            cmd.color = m_currentColor;
            cmd.width = preset->widthForPressure(0.5);
            cmd.presetId = m_currentPresetId;
            m_model->add(cmd, false);
            m_view->renderCommand(cmd);
            return;
        }
    }

    if (m_currentTool == EraserRealTime) {
        m_isDrawing = true;
        m_startPos = m_lastPos = pos;
        m_currentPath = QPainterPath();
        m_currentPath.moveTo(pos);
        m_view->eraseAt(pos, m_penWidth);
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
    if (m_currentTool == Pen && m_isDrawing) {
        BrushPreset *preset = m_model->getBrushPreset(m_currentPresetId);
        if (preset && preset->usePressure()) {
            if (QLineF(pos, m_lastPos).length() < 2.0) return;

            qreal distance = QLineF(pos, m_lastPos).length();
            qreal elapsed = m_lastTime.restart();
            qreal speed = (elapsed > 0) ? distance / elapsed : 0;
            qreal pressure = qBound(0.0, 1.0 - speed / 5.0, 1.0);
            m_lastPressure = pressure;

            DrawCmd cmd;
            cmd.type = CmdType::PenPoint;
            cmd.point = pos;
            cmd.color = m_currentColor;
            cmd.width = preset->widthForPressure(pressure);
            cmd.presetId = m_currentPresetId;
            m_model->add(cmd, false);
            m_view->renderCommand(cmd);
            m_lastPos = pos;
            return;
        }
    }

    if (m_currentTool == EraserRealTime && (e->buttons() & Qt::LeftButton)) {
        if (m_isDrawing) {
            m_view->eraseAt(pos, m_penWidth);
            m_currentPath.lineTo(pos);
            m_lastPos = pos;
        }
        return;
    }

    if (!m_isDrawing || !(e->buttons() & Qt::LeftButton)) return;

    if (m_currentTool == EraserFull) {
        m_currentPath.lineTo(pos);
        m_lastPos = pos;
    } else {
        updatePreview(pos);
    }
}

void DrawingController::onViewReleased(QMouseEvent *e, QPointF pos) {
    if (e->button() != Qt::LeftButton) return;

    if (m_currentTool == Pen && m_isDrawing) {
        m_isDrawing = false;
        return;
    }

    if (m_currentTool == EraserRealTime) {
        if (m_isDrawing) {
            DrawCmd cmd;
            cmd.type = CmdType::EraserStroke;
            cmd.path = m_currentPath;
            cmd.width = m_penWidth;
            cmd.color = m_currentColor;
            m_model->add(cmd);
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
            m_model->add(cmd);
        }
        break;
    case Rect: {
        QRectF r = QRectF(m_startPos, endPos).normalized();
        if (r.width() > 0 || r.height() > 0) {
            cmd.type = CmdType::Rect;
            cmd.start = r.topLeft();
            cmd.end = r.bottomRight();
            m_model->add(cmd);
        }
        break;
    }
    case Ellipse: {
        QRectF r = QRectF(m_startPos, endPos).normalized();
        if (r.width() > 0 || r.height() > 0) {
            cmd.type = CmdType::Ellipse;
            cmd.start = r.topLeft();
            cmd.end = r.bottomRight();
            m_model->add(cmd);
        }
        break;
    }
    case EraserFull:
        if (!m_currentPath.isEmpty()) {
            cmd.type = CmdType::EraserStroke;
            cmd.path = m_currentPath;
            m_model->add(cmd);
        }
        break;
    default:
        break;
    }
}