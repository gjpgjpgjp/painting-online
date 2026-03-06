#include "mainwindow.h"
#include "ui_MainWindow.h"
#include "RoomServer.h"
#include "CanvasExporter.h"
#include "CanvasDatabase.h"
#include <QColorDialog>
#include <QActionGroup>
#include <QStatusBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow), m_exporter(new CanvasExporter(this)), m_processingMove(false) {
    ui->setupUi(this);
    setupUI();
    setupConnections();
    setupToolGroup();
    setupLayerListDragDrop();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::setupUI() {
    m_network = new NetworkManager(this);
    m_model = new CanvasModel(this);
    m_model->setNetworkManager(m_network);
    m_view = new CanvasView(this);
    m_view->setModel(m_model);
    m_controller = new DrawingController(m_model, m_view, this);

    ui->canvasLayout->addWidget(m_view);
    ui->widthSlider->setValue(ui->widthSpinBox->value());
    ui->opacitySpinBox->setValue(100);
    ui->mainToolBar->setVisible(false);

    updateColorIcon();
    updateLayerList();

    BrushPreset defaultPreset(1, "默认");
    defaultPreset.setMinWidth(2);
    defaultPreset.setMaxWidth(10);
    m_model->addBrushPreset(defaultPreset);
    m_model->setCurrentBrushPresetId(1);
    m_controller->setCurrentBrushPreset(1);
    updateBrushPresetUI();
}

void MainWindow::setupConnections() {
    connect(m_network, &NetworkManager::connected, this, &MainWindow::onNetworkConnected);
    connect(m_network, &NetworkManager::disconnected, this, &MainWindow::onNetworkDisconnected);
    connect(m_network, &NetworkManager::errorOccurred, this, &MainWindow::onNetworkError);
    connect(m_network, &NetworkManager::commandReceived, m_model, &CanvasModel::onNetworkCommand);
    connect(ui->widthSlider, &QSlider::valueChanged, this, &MainWindow::setWidth);
    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::onConnectButtonClicked);
    connect(ui->exitButton, &QPushButton::clicked, this, &QMainWindow::close);
    connect(m_network, &NetworkManager::commandReceived, this, &MainWindow::onNetworkCommand);
    connect(m_model, &CanvasModel::layerChanged, this, &MainWindow::updateLayerList);
    connect(m_model, &CanvasModel::changed, this, &MainWindow::updateLayerControls);
    connect(m_model, &CanvasModel::commandAdded, this, [this](const QJsonObject &d) { if (m_roomServer) m_roomServer->addCommand(d); });

    connect(ui->brushPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onBrushPresetSelected);
    connect(ui->pressureCheckBox, &QCheckBox::toggled, this, &MainWindow::onPressureToggled);
    connect(ui->minWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onMinWidthChanged);
    connect(ui->maxWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onMaxWidthChanged);
    connect(ui->importBrushButton, &QPushButton::clicked, this, &MainWindow::onImportBrush);
}

void MainWindow::setupLayerListDragDrop() {
    // 启用内部拖放
    ui->layerListWidget->setDragDropMode(QAbstractItemView::InternalMove);
    ui->layerListWidget->setDefaultDropAction(Qt::MoveAction);
    ui->layerListWidget->setDragEnabled(true);
    ui->layerListWidget->setAcceptDrops(true);
    ui->layerListWidget->setDropIndicatorShown(true);

    // 连接 rowsMoved 信号（正确的签名）
    QAbstractItemModel *model = ui->layerListWidget->model();
    connect(model, &QAbstractItemModel::rowsMoved, this, &MainWindow::onLayerListRowsMoved);
}

void MainWindow::onLayerListRowsMoved(const QModelIndex &sourceParent, int sourceStart, int sourceEnd,
                                      const QModelIndex &destinationParent, int destinationRow) {
    if (m_processingMove) return;
    m_processingMove = true;

    // 计算目标索引（destinationRow 是插入位置，需要调整）
    int fromIndex = sourceStart;
    int toIndex = destinationRow;

    // 如果向下移动，destinationRow 是目标位置+1，需要调整
    if (toIndex > fromIndex) {
        toIndex = toIndex - 1;
    }

    // 边界检查
    auto layers = m_model->layers();
    if (fromIndex < 0 || fromIndex >= layers.size() ||
        toIndex < 0 || toIndex >= layers.size() ||
        fromIndex == toIndex) {
        m_processingMove = false;
        return;
    }

    // 执行图层移动
    m_model->moveLayer(fromIndex, toIndex);

    // 更新列表显示（保持选中状态）
    updateLayerList();
    ui->layerListWidget->setCurrentRow(toIndex);

    m_processingMove = false;
}

void MainWindow::setupToolGroup() {
    QActionGroup *g = new QActionGroup(this);
    g->addAction(ui->penButton);
    g->addAction(ui->rectButton);
    g->addAction(ui->ellipseButton);
    g->addAction(ui->eraserFullButton);
    g->addAction(ui->eraserRealTimeButton);
    ui->penButton->setChecked(true);
}

void MainWindow::updateLayerList() {
    if (m_processingMove) return;

    ui->layerListWidget->clear();
    for (const auto &l : m_model->layers()) {
        QString t = l.name + (l.visible ? "" : " (隐藏)");
        ui->layerListWidget->addItem(t);
    }

    // 恢复当前选中
    auto layers = m_model->layers();
    for (int i = 0; i < layers.size(); ++i) {
        if (layers[i].id == m_model->currentLayerId()) {
            ui->layerListWidget->setCurrentRow(i);
            break;
        }
    }
    updateLayerControls();
}

void MainWindow::updateLayerControls() {
    if (auto *l = m_model->getLayer(m_model->currentLayerId())) {
        ui->layerVisibleCheck->setChecked(l->visible);
        ui->layerOpacitySlider->setValue(qRound(l->opacity * 100));
    }
}

void MainWindow::updateColorIcon() {
    QPixmap p(16, 16);
    p.fill(m_controller->currentColor());
    ui->colorButton->setIcon(QIcon(p));
}

void MainWindow::syncWidthControls(int v) {
    m_controller->setWidth(v);
    ui->widthSpinBox->blockSignals(true);
    ui->widthSlider->blockSignals(true);
    ui->widthSpinBox->setValue(v);
    ui->widthSlider->setValue(v);
    ui->widthSpinBox->blockSignals(false);
    ui->widthSlider->blockSignals(false);
}

bool MainWindow::validateConnectionInput(bool creating) {
    m_roomName = ui->roomEdit->text();
    if (m_roomName.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请填写房间名");
        return false;
    }
    if (creating) {
        if (ui->portEdit->text().toInt() <= 0) {
            QMessageBox::warning(this, "输入错误", "请填写有效的端口");
            return false;
        }
    } else {
        if (ui->hostEdit->text().isEmpty() || ui->portEdit->text().toInt() <= 0) {
            QMessageBox::warning(this, "输入错误", "请填写完整的服务器信息");
            return false;
        }
    }
    return true;
}

void MainWindow::cleanupRoomServer() {
    delete m_roomServer;
    m_roomServer = nullptr;
    ui->connectButton->setEnabled(true);
    ui->connectButton->setText("连接");
}

void MainWindow::onConnectButtonClicked() {
    bool creating = ui->createRoomRadio->isChecked();
    if (!validateConnectionInput(creating)) return;
    int port = ui->portEdit->text().toInt();
    ui->connectButton->setEnabled(false);
    ui->connectButton->setText(creating ? "启动中..." : "连接中...");

    if (creating) {
        m_view->scene()->setSceneRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
        m_roomServer = new RoomServer(port, this);
        if (!m_roomServer->isListening()) {
            QMessageBox::critical(this, "错误", "无法启动本地服务器");
            cleanupRoomServer();
            return;
        }
        m_model->setRoomOwner(true);
        m_network->connectToServer("127.0.0.1", port);
    } else {
        m_network->connectToServer(ui->hostEdit->text(), port);
    }
}

void MainWindow::onNetworkConnected() {
    ui->connectButton->setEnabled(true);
    ui->connectButton->setText("连接");
    QJsonObject j;
    j["type"] = "join";
    j["room"] = m_roomName;
    j["action"] = ui->createRoomRadio->isChecked() ? "create" : "join";
    m_network->sendCommand(j);
    ui->connectWidget->setVisible(false);
    ui->canvasContainer->setVisible(true);
    ui->mainToolBar->setVisible(true);
    statusBar()->showMessage("已连接到服务器");
    if (!ui->createRoomRadio->isChecked()) {
        QJsonObject req;
        req["type"] = "syncRequest";
        m_network->sendCommand(req);
    }
}

void MainWindow::onNetworkDisconnected() {
    statusBar()->showMessage("已断开连接");
    ui->connectWidget->setVisible(true);
    ui->canvasContainer->setVisible(false);
    ui->mainToolBar->setVisible(false);
    m_model->setRoomOwner(false);
    cleanupRoomServer();
}

void MainWindow::onNetworkError(const QString &err) {
    QMessageBox::critical(this, "网络错误", "连接服务器失败: " + err);
    cleanupRoomServer();
}

DrawCmd MainWindow::parseDrawCmd(const QJsonObject &d) {
    DrawCmd c;
    c.id = d["id"].toInt();
    c.layerId = d["layerId"].toInt();
    c.presetId = d["presetId"].toInt(0);
    c.type = static_cast<CmdType>(d["cmdType"].toInt());
    c.color = QColor(d["color"].toString());
    c.width = d["width"].toInt();
    if (c.type == CmdType::Pen || c.type == CmdType::EraserStroke) {
        QByteArray ba = QByteArray::fromBase64(d["path"].toString().toLatin1());
        QDataStream ds(ba);
        ds >> c.path;
    } else if (c.type == CmdType::PenPoint) {
        c.point = QPointF(d["pointX"].toDouble(), d["pointY"].toDouble());
    } else {
        c.start = QPointF(d["startX"].toDouble(), d["startY"].toDouble());
        c.end = QPointF(d["endX"].toDouble(), d["endY"].toDouble());
    }
    return c;
}

void MainWindow::onNetworkCommand(const QJsonObject &obj) {
    QString t = obj["type"].toString();
    if (t == "syncRequest" && m_roomServer) {
        QJsonObject s;
        s["type"] = "fullSync";
        s["layers"] = m_model->layersToJson();
        s["currentLayerId"] = m_model->currentLayerId();
        QJsonArray cmds;
        for (const auto &c : m_model->getCommandData()) cmds.append(c);
        s["cmds"] = cmds;
        m_network->sendCommand(s);
    } else if (t == "fullSync") {
        if (obj.contains("layers")) {
            m_model->layersFromJson(obj["layers"].toArray());
            if (m_model->getLayer(obj["currentLayerId"].toInt()))
                m_model->setCurrentLayer(obj["currentLayerId"].toInt());
        }
        if (m_roomServer) return;
        m_model->clear();
        for (const auto &v : obj["cmds"].toArray()) {
            auto c = parseDrawCmd(v.toObject());
            if (!m_model->getLayer(c.layerId))
                m_model->addLayer(QString("图层%1").arg(c.layerId));
            m_model->add(c);
        }
        updateLayerList();
    } else if (t == "layerOp" || t == "layerSync") {
        m_model->onNetworkLayerOp(obj);
        updateLayerList();
    }
}

void MainWindow::on_syncAction_triggered() {
    if (m_network && m_network->isConnected() && !ui->createRoomRadio->isChecked()) {
        QJsonObject req;
        req["type"] = "syncRequest";
        m_network->sendCommand(req);
        statusBar()->showMessage("请求同步中...");
    }
}

void MainWindow::on_penButton_triggered() { m_controller->setTool(DrawingController::Pen); }
void MainWindow::on_rectButton_triggered() { m_controller->setTool(DrawingController::Rect); }
void MainWindow::on_ellipseButton_triggered() { m_controller->setTool(DrawingController::Ellipse); }
void MainWindow::on_eraserFullButton_triggered() { m_controller->setTool(DrawingController::EraserFull); }
void MainWindow::on_eraserRealTimeButton_triggered() { m_controller->setTool(DrawingController::EraserRealTime); }

void MainWindow::on_colorButton_triggered() {
    QColor c = QColorDialog::getColor(m_controller->currentColor(), this, tr("选择颜色"));
    if (c.isValid()) {
        m_controller->setColor(c);
        updateColorIcon();
    }
}

void MainWindow::on_opacitySpinBox_valueChanged(int v) {
    m_controller->setOpacity(v * 255 / 100);
    updateColorIcon();
}

void MainWindow::on_widthSpinBox_valueChanged(int v) { syncWidthControls(v); }
void MainWindow::on_undoButton_triggered() { m_controller->undo(); }
void MainWindow::on_redoButton_triggered() { m_controller->redo(); }
void MainWindow::on_clearButton_triggered() { m_controller->clear(); }

void MainWindow::on_addLayerBtn_clicked() {
    bool ok;
    QString n = QInputDialog::getText(this, "新建图层", "图层名称:", QLineEdit::Normal,
                                      QString("图层%1").arg(m_model->layers().size() + 1), &ok);
    if (ok && !n.isEmpty()) {
        int id = m_model->addLayer(n);
        m_model->setCurrentLayer(id);
        updateLayerList();
    }
}

void MainWindow::on_deleteLayerBtn_clicked() {
    int r = ui->layerListWidget->currentRow();
    if (r < 0 || r >= m_model->layers().size()) return;
    if (m_model->layers().size() <= 1) {
        QMessageBox::warning(this, "警告", "至少保留一个图层");
        return;
    }
    m_model->removeLayer(m_model->layers()[r].id);
}

void MainWindow::on_layerListWidget_currentRowChanged(int r) {
    if (r >= 0 && r < m_model->layers().size()) {
        m_model->setCurrentLayer(m_model->layers()[r].id);
        updateLayerControls();
    }
}

void MainWindow::on_layerVisibleCheck_stateChanged(int s) {
    m_model->setLayerVisible(m_model->currentLayerId(), s == Qt::Checked);
    updateLayerList();
}

void MainWindow::on_layerOpacitySlider_valueChanged(int v) {
    m_model->setLayerOpacity(m_model->currentLayerId(), v / 100.0);
}

void MainWindow::on_actionNewCanvas_triggered() {
    bool ok;
    int w = QInputDialog::getInt(this, "新建画布", "宽度 (100-2000):", 800, 100, 2000, 1, &ok);
    if (!ok) return;
    int h = QInputDialog::getInt(this, "新建画布", "高度 (100-2000):", 600, 100, 2000, 1, &ok);
    if (!ok) return;
    m_model->clear();
    m_canvasSize = QSize(w, h);
    m_view->scene()->setSceneRect(0, 0, w, h);
    m_currentCanvasFile.clear();
    statusBar()->showMessage("新建画布");
}

void MainWindow::on_actionOpenCanvas_triggered() {
    QString f = QFileDialog::getOpenFileName(this, "选择画布文件", "", "画布文件 (*.canvas);;所有文件 (*)");
    if (f.isEmpty()) return;
    QList<DrawCmd> cmds;
    QSize sz;
    QList<BrushPreset> presets;
    if (!CanvasDatabase::load(f, cmds, sz, presets)) {
        QMessageBox::critical(this, "错误", "无法加载画布文件");
        return;
    }
    m_model->clear();
    m_model->setBrushPresets(presets);
    for (const auto &c : cmds) m_model->add(c);
    m_canvasSize = sz;
    m_view->scene()->setSceneRect(0, 0, sz.width(), sz.height());
    m_currentCanvasFile = f;
    statusBar()->showMessage("画布加载成功");
    updateBrushPresetUI();
}

void MainWindow::on_actionSaveCanvas_triggered() {
    QString f = QFileDialog::getSaveFileName(this, "保存画布", m_currentCanvasFile, "画布文件 (*.canvas);;所有文件 (*)");
    if (f.isEmpty()) return;
    QSize sz = m_view->scene()->sceneRect().size().toSize();
    if (sz.isEmpty()) sz = m_canvasSize;
    if (!CanvasDatabase::save(f, m_model->allCommands(), sz, m_model->brushPresets())) {
        QMessageBox::critical(this, "错误", "无法保存画布");
        return;
    }
    m_currentCanvasFile = f;
    statusBar()->showMessage("画布保存成功");
}

void MainWindow::exportImage(const QString &title, const QString &filter, const QString &fmt) {
    QString f = QFileDialog::getSaveFileName(this, title, "", filter);
    if (f.isEmpty()) return;
    if (m_exporter->exportToImage(m_view->scene(), m_canvasSize, f, fmt))
        statusBar()->showMessage(fmt + "导出成功");
    else
        QMessageBox::critical(this, "错误", "导出" + fmt + "失败");
}

void MainWindow::on_actionExportPNG_triggered() { exportImage("导出PNG", "PNG图片 (*.png)", "PNG"); }
void MainWindow::on_actionExportJPEG_triggered() { exportImage("导出JPEG", "JPEG图片 (*.jpg *.jpeg)", "JPEG"); }

void MainWindow::setWidth(int v) { syncWidthControls(v); }

void MainWindow::updateBrushPresetUI() {
    ui->brushPresetCombo->clear();
    for (const auto &p : m_model->brushPresets())
        ui->brushPresetCombo->addItem(p.name(), p.id());
    ui->brushPresetCombo->setCurrentIndex(ui->brushPresetCombo->findData(m_model->currentBrushPresetId()));
    if (auto *p = m_model->getBrushPreset(m_model->currentBrushPresetId())) {
        ui->pressureCheckBox->setChecked(p->usePressure());
        ui->minWidthSpinBox->setValue(p->minWidth());
        ui->maxWidthSpinBox->setValue(p->maxWidth());
    }
}

void MainWindow::onBrushPresetSelected(int i) {
    if (i < 0) return;
    int id = ui->brushPresetCombo->itemData(i).toInt();
    m_model->setCurrentBrushPresetId(id);
    m_controller->setCurrentBrushPreset(id);
    updateBrushPresetUI();
}

void MainWindow::onPressureToggled(bool c) {
    if (auto *p = m_model->getBrushPreset(m_model->currentBrushPresetId()))
        p->setUsePressure(c);
}

void MainWindow::onMinWidthChanged(int v) {
    if (auto *p = m_model->getBrushPreset(m_model->currentBrushPresetId()))
        p->setMinWidth(v);
}

void MainWindow::onMaxWidthChanged(int v) {
    if (auto *p = m_model->getBrushPreset(m_model->currentBrushPresetId()))
        p->setMaxWidth(v);
}

void MainWindow::onImportBrush() {
    QString f = QFileDialog::getOpenFileName(this, "选择笔刷图片", "", "图片 (*.png *.jpg *.bmp)");
    if (f.isEmpty()) return;
    QImage img(f);
    if (img.isNull()) {
        QMessageBox::warning(this, "错误", "无法加载图片文件");
        return;
    }
    const int maxSize = 128;
    if (img.width() > maxSize || img.height() > maxSize)
        img = img.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int id = m_model->brushPresets().isEmpty() ? 1 : m_model->brushPresets().last().id() + 1;
    BrushPreset p(id, QFileInfo(f).baseName());
    p.setTexture(img);
    p.setMinWidth(10);
    p.setMaxWidth(50);
    m_model->addBrushPreset(p);
    updateBrushPresetUI();
    statusBar()->showMessage("笔刷导入成功", 2000);
}