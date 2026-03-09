#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QKeySequence>
#include "DrawingCore.h"
#include "UIWindow.h"
#include "NetworkManager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class RoomServer;
class CanvasExporter;
class ShortcutSettingsDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_penButton_triggered();
    void on_rectButton_triggered();
    void on_ellipseButton_triggered();
    void on_eraserFullButton_triggered();
    void on_eraserRealTimeButton_triggered();
    void on_lassoButton_triggered();
    void on_colorButton_triggered();
    void on_undoButton_triggered();
    void on_redoButton_triggered();
    void on_clearButton_triggered();
    void on_widthSpinBox_valueChanged(int v);
    void on_opacitySpinBox_valueChanged(int v);
    void onConnectButtonClicked();
    void onNetworkConnected();
    void onNetworkDisconnected();
    void onNetworkError(const QString &err);
    void on_actionNewCanvas_triggered();
    void on_actionOpenCanvas_triggered();
    void on_actionSaveCanvas_triggered();
    void on_actionExportPNG_triggered();
    void on_actionExportJPEG_triggered();
    void on_addLayerBtn_clicked();
    void on_deleteLayerBtn_clicked();
    void on_layerListWidget_currentRowChanged(int r);
    void on_layerVisibleCheck_stateChanged(int s);
    void on_layerOpacitySlider_valueChanged(int v);
    void onNetworkCommand(const QJsonObject &obj);
    void on_syncAction_triggered();
    void onBrushPresetSelected(int i);
    void onPressureToggled(bool c);
    void onMinWidthChanged(int v);
    void onMaxWidthChanged(int v);
    void onImportBrush();

    // 新增选区操作槽函数
    void on_paintSelectionAction_triggered();
    void on_clearSelectionAction_triggered();
    void on_dragSelectionAction_toggled(bool checked);
    void onLayerListRowsMoved(const QModelIndex &sourceParent, int sourceStart, int sourceEnd,
                              const QModelIndex &destinationParent, int destinationRow);
    void onUndoRedoStateChanged(bool canUndo, bool canRedo);

    // 新增：快捷键设置
    void on_actionShortcutSettings_triggered();
    void onShortcutsChanged(const QMap<QString, QKeySequence> &shortcuts);

private:
    void setupUI();
    void setupConnections();
    void setupToolGroup();
    void setupLayerListDragDrop();
    void updateColorIcon();
    void syncWidthControls(int v);
    void updateLayerList();
    void updateLayerControls();
    bool validateConnectionInput(bool creating);
    void cleanupRoomServer();
    void setWidth(int v);
    DrawCmd parseDrawCmd(const QJsonObject &d);
    void updateBrushPresetUI();
    void exportImage(const QString &title, const QString &filter, const QString &fmt);

    // 新增：快捷键管理
    void loadShortcuts();
    void saveShortcuts();
    void applyShortcuts();
    void updateActionShortcut(const QString &id, QAction *action);

    Ui::MainWindow *ui;
    CanvasModel *m_model;
    CanvasView *m_view;
    DrawingController *m_controller;
    NetworkManager *m_network;
    RoomServer *m_roomServer = nullptr;
    CanvasExporter *m_exporter;
    ShortcutSettingsDialog *m_shortcutDialog = nullptr;

    QString m_roomName;
    QString m_currentCanvasFile;
    QSize m_canvasSize;

    bool m_processingMove = false;

    // 快捷键配置
    QMap<QString, QKeySequence> m_shortcuts;
};

#endif // MAINWINDOW_H