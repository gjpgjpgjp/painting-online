#include "mainwindow.h"
#include "ui_MainWindow.h"
#include "RoomServer.h"
#include "CanvasExporter.h"
#include "CanvasDatabase.h"
#include "ShortcutSettingsDialog.h"
#include <QColorDialog>
#include <QActionGroup>
#include <QStatusBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QSettings>
#include <QShortcut>
#include <QKeySequence>
#include <QDateTime>
#include <QUuid>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow), m_exporter(new CanvasExporter(this)), m_processingMove(false) {
    // 透明无边框窗口
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, true);

    ui->setupUi(this);
    setupUI();
    setupConnections();
    setupToolGroup();
    setupLayerListDragDrop();

    // 标题栏按钮连接
    connect(ui->minimizeBtn, &QPushButton::clicked, this, &QMainWindow::showMinimized);
    connect(ui->maximizeBtn, &QPushButton::clicked, this, [this](){
        if (this->isMaximized())
            this->showNormal();
        else
            this->showMaximized();
    });
    connect(ui->closeBtn, &QPushButton::clicked, this, &QMainWindow::close);

    // 允许拖动标题栏移动窗口
    ui->titleBar->installEventFilter(this);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::setupUI() {
    // 不再需要透明属性
    // this->setAttribute(Qt::WA_TranslucentBackground, true);

    m_network = new NetworkManager(this);
    m_model = new CanvasModel(this);
    m_model->setNetworkManager(m_network);
    m_view = new CanvasView(this);
    m_view->setModel(m_model);
    m_controller = new DrawingController(m_model, m_view, this);
    QString clientId = QUuid::createUuid().toString();
    m_model->setClientId(clientId);

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
    m_view->setController(m_controller);

    // 加载快捷键设置
    loadShortcuts();
    applyShortcuts();
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
    connect(m_model, &CanvasModel::commandAdded, this, [this](const QJsonObject &d) {
        if (m_roomServer) m_roomServer->addCommand(d);
    });

    connect(ui->brushPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onBrushPresetSelected);
    connect(ui->pressureCheckBox, &QCheckBox::toggled, this, &MainWindow::onPressureToggled);
    connect(ui->minWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onMinWidthChanged);
    connect(ui->maxWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onMaxWidthChanged);
    connect(ui->importBrushButton, &QPushButton::clicked, this, &MainWindow::onImportBrush);

    connect(ui->lassoButton, &QAction::triggered, this, &MainWindow::on_lassoButton_triggered);
    connect(ui->paintSelectionAction, &QAction::triggered, this, &MainWindow::on_paintSelectionAction_triggered);
    connect(ui->clearSelectionAction, &QAction::triggered, this, &MainWindow::on_clearSelectionAction_triggered);
    connect(ui->dragSelectionAction, &QAction::toggled, this, &MainWindow::on_dragSelectionAction_toggled);

    connect(m_controller, &DrawingController::selectionChanged,
            m_view, QOverload<>::of(&QGraphicsView::update));
    connect(ui->dragSelectionAction, &QAction::toggled,
            m_controller, &DrawingController::setDragModeEnabled);

    connect(m_model, &CanvasModel::undoRedoStateChanged,
            this, &MainWindow::onUndoRedoStateChanged);

    // 快捷键设置
    connect(ui->actionShortcutSettings, &QAction::triggered,
            this, &MainWindow::on_actionShortcutSettings_triggered);

    // 新增：画布重置和状态接收信号
    connect(m_model, &CanvasModel::canvasReset, this, &MainWindow::onCanvasReset);
    connect(m_model, &CanvasModel::fullStateReceived, this, &MainWindow::onFullStateReceived);
}

void MainWindow::onUndoRedoStateChanged(bool canUndo, bool canRedo) {
    ui->undoButton->setEnabled(canUndo);
    ui->redoButton->setEnabled(canRedo);
}

void MainWindow::setupLayerListDragDrop() {
    ui->layerListWidget->setDragDropMode(QAbstractItemView::InternalMove);
    ui->layerListWidget->setDefaultDropAction(Qt::MoveAction);
    ui->layerListWidget->setDragEnabled(true);
    ui->layerListWidget->setAcceptDrops(true);
    ui->layerListWidget->setDropIndicatorShown(true);

    QAbstractItemModel *model = ui->layerListWidget->model();
    connect(model, &QAbstractItemModel::rowsMoved, this, &MainWindow::onLayerListRowsMoved);
}

void MainWindow::onLayerListRowsMoved(const QModelIndex &sourceParent, int sourceStart, int sourceEnd,
                                      const QModelIndex &destinationParent, int destinationRow) {
    if (m_processingMove) return;
    m_processingMove = true;

    int fromIndex = sourceStart;
    int toIndex = destinationRow;

    if (toIndex > fromIndex) {
        toIndex = toIndex - 1;
    }

    auto layers = m_model->layers();
    if (fromIndex < 0 || fromIndex >= layers.size() ||
        toIndex < 0 || toIndex >= layers.size() ||
        fromIndex == toIndex) {
        m_processingMove = false;
        return;
    }

    // 使用命令移动图层
    m_model->moveLayer(fromIndex, toIndex);  // 内部已改为命令方式
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
    g->addAction(ui->lassoButton);
    ui->penButton->setChecked(true);
}

void MainWindow::updateLayerList() {
    if (m_processingMove) return;

    ui->layerListWidget->clear();
    for (const auto &l : m_model->layers()) {
        QString t = l.name + (l.visible ? "" : " (隐藏)");
        ui->layerListWidget->addItem(t);
    }

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

    // ========== 修改：切换到画布模式 ==========
    // 隐藏登录面板（透明背景区域）
    ui->connectWidget->setVisible(false);

    // 显示画布容器（纯白背景，不透明，适合绘画）
    ui->canvasContainer->setVisible(true);
    ui->mainToolBar->setVisible(true);

    // 可选：最大化窗口以获得更多绘画空间
    this->showMaximized();

    // 或者使用全屏模式（无边框）：
    // this->showFullScreen();
    // ======================================

    statusBar()->showMessage("已连接到服务器");
    if (!ui->createRoomRadio->isChecked()) {
        QJsonObject req;
        req["type"] = "syncRequest";
        m_network->sendCommand(req);
    }
}

void MainWindow::onNetworkDisconnected() {
    statusBar()->showMessage("已断开连接");

    // ========== 修改：恢复到登录界面 ==========
    // 显示登录面板（玻璃效果，透明背景）
    ui->connectWidget->setVisible(true);

    // 隐藏画布容器和工具栏
    ui->canvasContainer->setVisible(false);
    ui->mainToolBar->setVisible(false);

    // 如果窗口是最大化状态，恢复为普通大小
    if (this->isMaximized()) {
        this->showNormal();
    }
    // ======================================

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

// 修改：网络命令处理 - 增强同步处理（合并所有处理逻辑到这里）
void MainWindow::onNetworkCommand(const QJsonObject &obj) {
    QString t = obj["type"].toString();

    if (t == "undoCmd" || t == "redoCmd") {
        updateLayerList();
        m_view->clearSceneItems();
        m_view->renderCommands(m_model->allCommands());
        onUndoRedoStateChanged(m_model->canUndo(), m_model->canRedo());

        QString cmdTypeStr;
        int cmdType = obj["undoneCmdType"].toInt();
        if (t == "redoCmd") {
            cmdType = obj["redoneCmdType"].toInt();
        }

        switch (static_cast<CommandType>(cmdType)) {
        case CommandType::DrawCmd: cmdTypeStr = "绘制"; break;
        case CommandType::DeleteDrawCmds: cmdTypeStr = "删除"; break;
        case CommandType::MoveDrawCmds: cmdTypeStr = "移动"; break;
        case CommandType::AddLayer: cmdTypeStr = "添加图层"; break;
        case CommandType::RemoveLayer: cmdTypeStr = "删除图层"; break;
        case CommandType::MoveLayer: cmdTypeStr = "移动图层"; break;
        case CommandType::ClearLayer: cmdTypeStr = "清空图层"; break;
        case CommandType::Composite: cmdTypeStr = "复合操作"; break;
        default: cmdTypeStr = "未知操作"; break;
        }

        QString action = (t == "undoCmd") ? "撤销" : "重做";
        statusBar()->showMessage(QString("远端%1了%2").arg(action, cmdTypeStr), 2000);
        return;
    }

    if (t == "fullState") {
        QJsonObject state = obj;
        m_model->applyFullState(state);
        m_canvasSize = m_model->canvasSize();
        m_view->scene()->setSceneRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
        updateLayerList();
        updateBrushPresetUI();
        statusBar()->showMessage("画布同步完成");
    }
    else if (t == "canvasReset") {
        if (obj.contains("state")) {
            m_model->applyFullState(obj["state"].toObject());
            m_canvasSize = m_model->canvasSize();
            m_view->scene()->setSceneRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
            updateLayerList();
            updateBrushPresetUI();
            statusBar()->showMessage("画布已重置");
        }
    }
    else if (t == "init") {
        QJsonArray cmds = obj["cmds"].toArray();
        for (const auto &v : cmds) {
            DrawCmd cmd = parseDrawCmd(v.toObject());
            m_model->add(cmd, false);
        }
        m_view->renderCommands(m_model->allCommands());
        updateLayerList();
        statusBar()->showMessage(QString("已加载历史记录 (%1 条命令)").arg(cmds.size()));
    }
    else if (t == "syncRequest" && m_roomServer) {
        QJsonObject state = m_model->getFullState();
        state["type"] = "fullState";
        m_network->sendCommand(state);
    }
    else if (t == "layerOp" || t == "layerSync") {
        m_model->onNetworkLayerOp(obj);
        updateLayerList();
    }
    else if (t == "uploadAck") {
        bool success = obj["success"].toBool();
        if (success) {
            statusBar()->showMessage("画布上传成功: " + obj["fileName"].toString(), 3000);
        } else {
            QMessageBox::warning(this, "上传失败",
                                 "上传画布失败: " + obj["error"].toString());
        }
    }
    else if (t == "downloadData") {
        QJsonObject data = obj["data"].toObject();
        QString fileName = obj["fileName"].toString();

        auto reply = QMessageBox::question(this, "下载画布",
                                           "是否用下载的画布替换当前画布？",
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            m_model->applyFullState(data);
            m_canvasSize = m_model->canvasSize();
            m_view->scene()->setSceneRect(0, 0, m_canvasSize.width(), m_canvasSize.height());
            updateLayerList();
            updateBrushPresetUI();
            statusBar()->showMessage("画布下载完成: " + fileName, 3000);
        }
    }
    else if (t == "downloadError") {
        QMessageBox::warning(this, "下载失败",
                             "下载画布失败: " + obj["error"].toString());
    }
    else if (t == "canvasFileList") {
        QJsonArray fileList = obj["files"].toArray();
        if (fileList.isEmpty()) {
            QMessageBox::information(this, "文件列表", "服务器上没有找到画布文件");
            return;
        }

        QStringList files;
        for (const auto &v : fileList) {
            files.append(v.toString());
        }

        bool ok;
        QString selected = QInputDialog::getItem(this, "选择画布文件", "请选择要下载的画布:", files, 0, false, &ok);
        if (!ok || selected.isEmpty()) return;

        QJsonObject download;
        download["type"] = "downloadCanvas";
        download["fileName"] = selected;
        m_network->sendCommand(download);
        statusBar()->showMessage("正在下载画布: " + selected);
    }
}
// 修改：同步功能 - 现在支持双向同步
void MainWindow::on_syncAction_triggered() {
    if (!m_network || !m_network->isConnected()) {
        QMessageBox::warning(this, "未连接", "请先连接到服务器");
        return;
    }

    // 房主：上传当前完整状态到服务器
    if (ui->createRoomRadio->isChecked() || m_model->isRoomOwner()) {
        syncToServer();
    } else {
        // 客人：从服务器拉取最新状态
        requestSyncFromServer();
    }
}

void MainWindow::syncToServer() {
    QJsonObject state = m_model->getFullState();
    state["type"] = "fullState";

    // 如果是房主，直接设置服务器状态
    if (m_roomServer) {
        m_roomServer->setFullState(state);
        statusBar()->showMessage("画布状态已同步到服务器");
    } else {
        // 否则发送给服务器
        m_network->sendCommand(state);
        statusBar()->showMessage("正在上传画布状态...");
    }
}

void MainWindow::requestSyncFromServer() {
    QJsonObject req;
    req["type"] = "syncRequest";
    req["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    m_network->sendCommand(req);
    statusBar()->showMessage("正在从服务器同步画布...");
}

void MainWindow::on_penButton_triggered() { m_controller->setTool(DrawingController::Pen); }
void MainWindow::on_rectButton_triggered() { m_controller->setTool(DrawingController::Rect); }
void MainWindow::on_ellipseButton_triggered() { m_controller->setTool(DrawingController::Ellipse); }
void MainWindow::on_eraserFullButton_triggered() { m_controller->setTool(DrawingController::EraserFull); }
void MainWindow::on_eraserRealTimeButton_triggered() { m_controller->setTool(DrawingController::EraserRealTime); }
void MainWindow::on_lassoButton_triggered() { m_controller->setTool(DrawingController::LassoTool); }

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
void MainWindow::on_clearButton_triggered() { m_controller->clear(); } // 已改为命令方式

void MainWindow::on_addLayerBtn_clicked() {
    bool ok;
    QString n = QInputDialog::getText(this, "新建图层", "图层名称:", QLineEdit::Normal,
                                      QString("图层%1").arg(m_model->layers().size() + 1), &ok);
    if (ok && !n.isEmpty()) {
        // 通过命令添加图层
        int id = m_model->addLayer(n);  // 内部已改为命令
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
    // 通过命令删除图层
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

// 修改：新建画布 - 现在会广播到所有客户端
void MainWindow::on_actionNewCanvas_triggered() {
    bool ok;
    int w = QInputDialog::getInt(this, "新建画布", "宽度 (100-4000):", 800, 100, 4000, 1, &ok);
    if (!ok) return;
    int h = QInputDialog::getInt(this, "新建画布", "高度 (100-4000):", 600, 100, 4000, 1, &ok);
    if (!ok) return;

    QString name = QInputDialog::getText(this, "新建画布", "画布名称:",
                                         QLineEdit::Normal, "未命名画布", &ok);
    if (!ok) return;

    // 重置画布（会自动广播）
    m_model->resetCanvas(QSize(w, h), name);

    m_canvasSize = QSize(w, h);
    m_view->scene()->setSceneRect(0, 0, w, h);
    m_currentCanvasFile.clear();

    updateLayerList();
    updateBrushPresetUI();
    statusBar()->showMessage("新建画布: " + name);
}

// 修改：保存画布 - 现在保存完整状态
void MainWindow::on_actionSaveCanvas_triggered() {
    QString f = QFileDialog::getSaveFileName(this, "保存画布", m_currentCanvasFile,
                                             "画布文件 (*.canvas);;所有文件 (*)");
    if (f.isEmpty()) return;

    // 更新元数据
    SimpleCanvasMetadata meta = m_model->canvasMetadata();
    meta.modifiedAt = QDateTime::currentMSecsSinceEpoch();
    meta.size = m_canvasSize;
    m_model->setCanvasMetadata(meta);

    if (!CanvasDatabase::save(f, m_model->allCommands(), meta,
                              m_model->layers(), m_model->brushPresets())) {
        QMessageBox::critical(this, "错误", "无法保存画布");
        return;
    }

    m_currentCanvasFile = f;
    statusBar()->showMessage("画布保存成功: " + f);
}

// 修改：打开画布 - 现在加载完整状态并可选同步到服务器
void MainWindow::on_actionOpenCanvas_triggered() {
    QString f = QFileDialog::getOpenFileName(this, "选择画布文件", "",
                                             "画布文件 (*.canvas);;所有文件 (*)");
    if (f.isEmpty()) return;

    QList<DrawCmd> cmds;
    SimpleCanvasMetadata meta;
    QList<Layer> layers;
    QList<BrushPreset> presets;

    if (!CanvasDatabase::load(f, cmds, meta, layers, presets)) {
        QMessageBox::critical(this, "错误", "无法加载画布文件");
        return;
    }

    // 应用加载的状态
    m_model->setSuppressEmit(true);
    m_model->setCanvasMetadata(meta);
    m_model->setBrushPresets(presets);

    // 使用applyFullState更高效
    QJsonObject state = CanvasDatabase::exportToJson(cmds, meta, layers, presets);
    state["currentLayerId"] = 0;
    state["nextCmdId"] = cmds.isEmpty() ? 1 : cmds.last().id + 1;
    state["nextLayerId"] = layers.isEmpty() ? 1 :
                               (*std::max_element(layers.begin(), layers.end(),
                                                  [](const Layer& a, const Layer& b) { return a.id < b.id; })).id + 1;

    m_model->applyFullState(state);
    m_model->setSuppressEmit(false);

    m_canvasSize = meta.size;
    m_view->scene()->setSceneRect(0, 0, meta.size.width(), meta.size.height());
    m_currentCanvasFile = f;

    updateLayerList();
    updateBrushPresetUI();
    statusBar()->showMessage("画布加载成功: " + meta.canvasName);

    // 如果已连接，询问是否同步到服务器
    if (m_network && m_network->isConnected()) {
        auto reply = QMessageBox::question(this, "同步到服务器",
                                           "是否将打开的画布同步到服务器？\n这将覆盖服务器上的当前画布。",
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            syncToServer();
        }
    }
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
    if (!ui->brushPresetCombo) {
        qWarning() << "brushPresetCombo is null!";
        return;
    }

    // 临时阻断信号，避免在清除/添加时触发槽函数
    ui->brushPresetCombo->blockSignals(true);
    ui->brushPresetCombo->clear();

    for (const auto &p : m_model->brushPresets())
        ui->brushPresetCombo->addItem(p.name(), p.id());

    int index = ui->brushPresetCombo->findData(m_model->currentBrushPresetId());
    if (index >= 0)
        ui->brushPresetCombo->setCurrentIndex(index);

    ui->brushPresetCombo->blockSignals(false);

    // 更新压力感应等控件
    if (auto *p = m_model->getBrushPreset(m_model->currentBrushPresetId())) {
        ui->pressureCheckBox->blockSignals(true);
        ui->minWidthSpinBox->blockSignals(true);
        ui->maxWidthSpinBox->blockSignals(true);

        ui->pressureCheckBox->setChecked(p->usePressure());
        ui->minWidthSpinBox->setValue(p->minWidth());
        ui->maxWidthSpinBox->setValue(p->maxWidth());

        ui->pressureCheckBox->blockSignals(false);
        ui->minWidthSpinBox->blockSignals(false);
        ui->maxWidthSpinBox->blockSignals(false);
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

    // 确保图片格式为 ARGB32（支持透明）
    if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);

    int newId = m_model->brushPresets().isEmpty() ? 1 : m_model->brushPresets().last().id() + 1;
    BrushPreset p(newId, QFileInfo(f).baseName());
    p.setTexture(img);
    p.setMinWidth(1);
    p.setMaxWidth(50);
    p.setUsePressure(false);  // 先关闭压力感应，方便测试

    // 禁止模型在添加时发射信号，避免界面刷新冲突
    bool oldSuppress = m_model->suppressEmit();
    m_model->setSuppressEmit(true);
    m_model->addBrushPreset(p);
    m_model->setSuppressEmit(oldSuppress);

    // 关键：切换到新笔刷
    m_model->setCurrentBrushPresetId(newId);
    m_controller->setCurrentBrushPreset(newId);

    // 刷新笔刷下拉框并选中新笔刷
    updateBrushPresetUI();

    statusBar()->showMessage("笔刷导入成功: " + p.name(), 2000);
}

void MainWindow::on_paintSelectionAction_triggered() {
    m_controller->onPaintSelection();
}

void MainWindow::on_clearSelectionAction_triggered() {
    m_controller->onClearSelection();
}

void MainWindow::on_dragSelectionAction_toggled(bool checked) {
    if (checked) {
        m_view->setCursor(Qt::SizeAllCursor);
    } else {
        m_view->setCursor(Qt::ArrowCursor);
    }
}

// 新增：画布重置处理
void MainWindow::onCanvasReset() {
    m_view->clearSceneItems();
    m_view->renderCommands(m_model->allCommands());
    updateLayerList();
    updateBrushPresetUI();
}

// 新增：完整状态接收处理
void MainWindow::onFullStateReceived(const QJsonObject &state) {
    Q_UNUSED(state)
    // 状态已应用，更新UI
    m_view->clearSceneItems();
    m_view->renderCommands(m_model->allCommands());
    updateLayerList();
    updateBrushPresetUI();
    statusBar()->showMessage("画布同步完成");
}

// ========== 快捷键设置相关函数 ==========

void MainWindow::on_actionShortcutSettings_triggered()
{
    if (!m_shortcutDialog) {
        m_shortcutDialog = new ShortcutSettingsDialog(this);
        connect(m_shortcutDialog, &ShortcutSettingsDialog::shortcutsChanged,
                this, &MainWindow::onShortcutsChanged);
    }

    // 设置当前快捷键
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        m_shortcutDialog->setShortcut(it.key(), it.value());
    }

    if (m_shortcutDialog->exec() == QDialog::Accepted) {
        m_shortcuts = m_shortcutDialog->getShortcuts();
        applyShortcuts();
        saveShortcuts();
    }
}

void MainWindow::onShortcutsChanged(const QMap<QString, QKeySequence> &shortcuts)
{
    m_shortcuts = shortcuts;
    applyShortcuts();
    saveShortcuts();
}

void MainWindow::applyShortcuts()
{
    // 工具类快捷键
    updateActionShortcut(ShortcutSettingsDialog::ID_TOOL_PEN, ui->penButton);
    updateActionShortcut(ShortcutSettingsDialog::ID_TOOL_RECT, ui->rectButton);
    updateActionShortcut(ShortcutSettingsDialog::ID_TOOL_ELLIPSE, ui->ellipseButton);
    updateActionShortcut(ShortcutSettingsDialog::ID_TOOL_ERASER_FULL, ui->eraserFullButton);
    updateActionShortcut(ShortcutSettingsDialog::ID_TOOL_ERASER_REALTIME, ui->eraserRealTimeButton);
    updateActionShortcut(ShortcutSettingsDialog::ID_TOOL_LASSO, ui->lassoButton);

    // 编辑类快捷键
    updateActionShortcut(ShortcutSettingsDialog::ID_EDIT_UNDO, ui->undoButton);
    updateActionShortcut(ShortcutSettingsDialog::ID_EDIT_REDO, ui->redoButton);
    updateActionShortcut(ShortcutSettingsDialog::ID_EDIT_CLEAR, ui->clearButton);

    // 文件类快捷键
    updateActionShortcut(ShortcutSettingsDialog::ID_FILE_NEW, ui->actionNewCanvas);
    updateActionShortcut(ShortcutSettingsDialog::ID_FILE_OPEN, ui->actionOpenCanvas);
    updateActionShortcut(ShortcutSettingsDialog::ID_FILE_SAVE, ui->actionSaveCanvas);

    // 视图类快捷键
    updateActionShortcut(ShortcutSettingsDialog::ID_VIEW_SYNC, ui->syncAction);

    // 选区类快捷键
    updateActionShortcut(ShortcutSettingsDialog::ID_SELECTION_PAINT, ui->paintSelectionAction);
    updateActionShortcut(ShortcutSettingsDialog::ID_SELECTION_CLEAR, ui->clearSelectionAction);
    updateActionShortcut(ShortcutSettingsDialog::ID_SELECTION_DRAG, ui->dragSelectionAction);
}

void MainWindow::updateActionShortcut(const QString &id, QAction *action)
{
    if (!action || !m_shortcuts.contains(id)) {
        return;
    }

    QKeySequence shortcut = m_shortcuts[id];
    action->setShortcut(shortcut);

    // 更新工具提示显示快捷键
    QString tooltip = action->toolTip();
    // 移除旧的快捷键显示（如果有）
    int parenIndex = tooltip.indexOf(" (");
    if (parenIndex > 0) {
        tooltip = tooltip.left(parenIndex);
    }
    // 添加新的快捷键显示
    if (!shortcut.isEmpty()) {
        tooltip += " (" + shortcut.toString(QKeySequence::NativeText) + ")";
    }
    action->setToolTip(tooltip);
}

void MainWindow::loadShortcuts()
{
    QSettings settings("YourCompany", "PaintingOnline");
    settings.beginGroup("Shortcuts");

    // 默认快捷键
    m_shortcuts[ShortcutSettingsDialog::ID_TOOL_PEN] = QKeySequence("P");
    m_shortcuts[ShortcutSettingsDialog::ID_TOOL_RECT] = QKeySequence("R");
    m_shortcuts[ShortcutSettingsDialog::ID_TOOL_ELLIPSE] = QKeySequence("E");
    m_shortcuts[ShortcutSettingsDialog::ID_TOOL_ERASER_FULL] = QKeySequence("F");
    m_shortcuts[ShortcutSettingsDialog::ID_TOOL_ERASER_REALTIME] = QKeySequence("X");
    m_shortcuts[ShortcutSettingsDialog::ID_TOOL_LASSO] = QKeySequence("L");
    m_shortcuts[ShortcutSettingsDialog::ID_EDIT_UNDO] = QKeySequence("Ctrl+Z");
    m_shortcuts[ShortcutSettingsDialog::ID_EDIT_REDO] = QKeySequence("Ctrl+Y");
    m_shortcuts[ShortcutSettingsDialog::ID_EDIT_CLEAR] = QKeySequence("Ctrl+Shift+C");
    m_shortcuts[ShortcutSettingsDialog::ID_FILE_NEW] = QKeySequence("Ctrl+N");
    m_shortcuts[ShortcutSettingsDialog::ID_FILE_OPEN] = QKeySequence("Ctrl+O");
    m_shortcuts[ShortcutSettingsDialog::ID_FILE_SAVE] = QKeySequence("Ctrl+S");
    m_shortcuts[ShortcutSettingsDialog::ID_VIEW_SYNC] = QKeySequence("F5");

    // 从设置加载（覆盖默认值）
    QStringList keys = settings.allKeys();
    for (const QString &key : keys) {
        QString value = settings.value(key).toString();
        if (!value.isEmpty()) {
            m_shortcuts[key] = QKeySequence(value);
        }
    }

    settings.endGroup();
}

void MainWindow::saveShortcuts()
{
    QSettings settings("YourCompany", "PaintingOnline");
    settings.beginGroup("Shortcuts");

    // 清除旧的设置
    settings.remove("");

    // 保存新的设置
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        if (!it.value().isEmpty()) {
            settings.setValue(it.key(), it.value().toString());
        }
    }

    settings.endGroup();
    settings.sync();
}

// ========== 上传画布到服务器 ==========
void MainWindow::on_actionUploadCanvas_triggered()
{
    if (!m_network || !m_network->isConnected()) {
        QMessageBox::warning(this, "未连接", "请先连接到服务器");
        return;
    }

    // 弹出保存文件对话框，选择服务器上要保存的文件名
    QString fileName = QFileDialog::getSaveFileName(this, "上传画布到服务器", "",
                                                    "画布文件 (*.canvas)");
    if (fileName.isEmpty()) return;

    // 只保留文件名，不含路径（可根据需求调整）
    QString baseName = QFileInfo(fileName).fileName();

    // 构造上传命令
    QJsonObject upload;
    upload["type"] = "uploadCanvas";
    upload["fileName"] = baseName;  // 服务器将文件保存在其工作目录下
    upload["data"] = m_model->getFullState();

    m_network->sendCommand(upload);
    statusBar()->showMessage("正在上传画布到服务器...");
}

// ========== 从服务器下载画布 ==========
void MainWindow::on_actionDownloadCanvas_triggered()
{
    if (!m_network || !m_network->isConnected()) {
        QMessageBox::warning(this, "未连接", "请先连接到服务器");
        return;
    }

    // 请求服务器发送画布文件列表
    QJsonObject request;
    request["type"] = "listCanvasFiles";
    m_network->sendCommand(request);
    statusBar()->showMessage("正在获取服务器画布文件列表...");
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == ui->titleBar && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            m_dragPosition = me->globalPos() - frameGeometry().topLeft();
            me->accept();
            return true;
        }
    }
    else if (obj == ui->titleBar && event->type() == QEvent::MouseMove) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        if (me->buttons() & Qt::LeftButton) {
            move(me->globalPos() - m_dragPosition);
            me->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}