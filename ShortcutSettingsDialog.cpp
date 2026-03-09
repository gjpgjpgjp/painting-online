#include "ShortcutSettingsDialog.h"
#include <QTableWidget>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QKeyEvent>

// 定义快捷键ID常量
const QString ShortcutSettingsDialog::ID_TOOL_PEN = "tool.pen";
const QString ShortcutSettingsDialog::ID_TOOL_RECT = "tool.rect";
const QString ShortcutSettingsDialog::ID_TOOL_ELLIPSE = "tool.ellipse";
const QString ShortcutSettingsDialog::ID_TOOL_ERASER_FULL = "tool.eraser_full";
const QString ShortcutSettingsDialog::ID_TOOL_ERASER_REALTIME = "tool.eraser_realtime";
const QString ShortcutSettingsDialog::ID_TOOL_LASSO = "tool.lasso";
const QString ShortcutSettingsDialog::ID_EDIT_UNDO = "edit.undo";
const QString ShortcutSettingsDialog::ID_EDIT_REDO = "edit.redo";
const QString ShortcutSettingsDialog::ID_EDIT_CLEAR = "edit.clear";
const QString ShortcutSettingsDialog::ID_FILE_NEW = "file.new";
const QString ShortcutSettingsDialog::ID_FILE_OPEN = "file.open";
const QString ShortcutSettingsDialog::ID_FILE_SAVE = "file.save";
const QString ShortcutSettingsDialog::ID_VIEW_SYNC = "view.sync";
const QString ShortcutSettingsDialog::ID_SELECTION_PAINT = "selection.paint";
const QString ShortcutSettingsDialog::ID_SELECTION_CLEAR = "selection.clear";
const QString ShortcutSettingsDialog::ID_SELECTION_DRAG = "selection.drag";

ShortcutSettingsDialog::ShortcutSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_currentRow(-1)
{
    setWindowTitle(tr("快捷键设置"));
    setMinimumSize(500, 400);
    setObjectName("ShortcutSettingsDialog");

    setupUI();
    setupConnections();
    initializeShortcuts();
    updateTable();
}

ShortcutSettingsDialog::~ShortcutSettingsDialog() = default;

void ShortcutSettingsDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // 说明标签
    QLabel *infoLabel = new QLabel(tr("点击表格中的快捷键，然后在下方输入新的快捷键组合。"));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #666;");
    mainLayout->addWidget(infoLabel);

    // 快捷键表格
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(3);
    m_tableWidget->setHorizontalHeaderLabels({tr("功能"), tr("描述"), tr("快捷键")});
    m_tableWidget->horizontalHeader()->setStretchLastSection(true);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableWidget->setAlternatingRowColors(true);
    m_tableWidget->verticalHeader()->setVisible(false);
    mainLayout->addWidget(m_tableWidget);

    // 快捷键编辑区域
    QHBoxLayout *editLayout = new QHBoxLayout();
    editLayout->addWidget(new QLabel(tr("新快捷键:")));

    m_keySequenceEdit = new QKeySequenceEdit(this);
    m_keySequenceEdit->setToolTip(tr("按下你想要的快捷键组合"));
    m_keySequenceEdit->setMaximumWidth(200);
    editLayout->addWidget(m_keySequenceEdit);

    m_clearButton = new QPushButton(tr("清除"), this);
    m_clearButton->setToolTip(tr("清除当前选中的快捷键"));
    editLayout->addWidget(m_clearButton);

    editLayout->addStretch();
    mainLayout->addLayout(editLayout);

    // 冲突提示
    m_conflictLabel = new QLabel(this);
    m_conflictLabel->setStyleSheet("color: #e74c3c; font-weight: bold;");
    m_conflictLabel->setVisible(false);
    mainLayout->addWidget(m_conflictLabel);

    // 按钮区域
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    m_resetButton = new QPushButton(tr("恢复默认"), this);
    m_resetButton->setToolTip(tr("将所有快捷键恢复为默认设置"));
    buttonLayout->addWidget(m_resetButton);

    buttonLayout->addStretch();

    m_cancelButton = new QPushButton(tr("取消"), this);
    buttonLayout->addWidget(m_cancelButton);

    m_okButton = new QPushButton(tr("确定"), this);
    m_okButton->setDefault(true);
    buttonLayout->addWidget(m_okButton);

    mainLayout->addLayout(buttonLayout);
}

void ShortcutSettingsDialog::setupConnections() {
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_resetButton, &QPushButton::clicked, this, &ShortcutSettingsDialog::onResetClicked);
    connect(m_clearButton, &QPushButton::clicked, this, &ShortcutSettingsDialog::onClearClicked);
    connect(m_keySequenceEdit, &QKeySequenceEdit::keySequenceChanged,
            this, &ShortcutSettingsDialog::onShortcutChanged);
    connect(m_tableWidget, &QTableWidget::itemSelectionChanged,
            this, &ShortcutSettingsDialog::onItemSelectionChanged);
}

void ShortcutSettingsDialog::initializeShortcuts() {
    m_shortcuts.clear();

    // 工具类
    m_shortcuts.append({ID_TOOL_PEN, tr("画笔"), tr("切换到画笔工具"), QKeySequence("P"), QKeySequence("P")});
    m_shortcuts.append({ID_TOOL_RECT, tr("矩形"), tr("切换到矩形工具"), QKeySequence("R"), QKeySequence("R")});
    m_shortcuts.append({ID_TOOL_ELLIPSE, tr("椭圆"), tr("切换到椭圆工具"), QKeySequence("E"), QKeySequence("E")});
    m_shortcuts.append({ID_TOOL_ERASER_FULL, tr("整笔删除"), tr("切换到整笔删除模式"), QKeySequence("F"), QKeySequence("F")});
    m_shortcuts.append({ID_TOOL_ERASER_REALTIME, tr("实时擦除"), tr("切换到实时擦除模式"), QKeySequence("X"), QKeySequence("X")});
    m_shortcuts.append({ID_TOOL_LASSO, tr("套索"), tr("切换到套索选择工具"), QKeySequence("L"), QKeySequence("L")});

    // 编辑类
    m_shortcuts.append({ID_EDIT_UNDO, tr("撤销"), tr("撤销上一步操作"), QKeySequence("Ctrl+Z"), QKeySequence("Ctrl+Z")});
    m_shortcuts.append({ID_EDIT_REDO, tr("重做"), tr("重做上一步操作"), QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Y")});
    m_shortcuts.append({ID_EDIT_CLEAR, tr("清空"), tr("清空当前图层"), QKeySequence("Ctrl+Shift+C"), QKeySequence("Ctrl+Shift+C")});

    // 文件类
    m_shortcuts.append({ID_FILE_NEW, tr("新建画布"), tr("创建新的画布"), QKeySequence("Ctrl+N"), QKeySequence("Ctrl+N")});
    m_shortcuts.append({ID_FILE_OPEN, tr("打开画布"), tr("打开已有的画布文件"), QKeySequence("Ctrl+O"), QKeySequence("Ctrl+O")});
    m_shortcuts.append({ID_FILE_SAVE, tr("保存画布"), tr("保存当前画布"), QKeySequence("Ctrl+S"), QKeySequence("Ctrl+S")});

    // 视图类
    m_shortcuts.append({ID_VIEW_SYNC, tr("同步"), tr("手动同步画布"), QKeySequence("F5"), QKeySequence("F5")});

    // 选区类
    m_shortcuts.append({ID_SELECTION_PAINT, tr("喷漆选区"), tr("在选区内喷漆"), QKeySequence(), QKeySequence()});
    m_shortcuts.append({ID_SELECTION_CLEAR, tr("清空选区"), tr("删除选区内所有内容"), QKeySequence(), QKeySequence()});
    m_shortcuts.append({ID_SELECTION_DRAG, tr("拖动选区"), tr("拖动选区内的图形"), QKeySequence(), QKeySequence()});
}

void ShortcutSettingsDialog::updateTable() {
    m_tableWidget->setRowCount(m_shortcuts.size());

    for (int i = 0; i < m_shortcuts.size(); ++i) {
        const ShortcutItem &item = m_shortcuts[i];

        // 功能名称
        QTableWidgetItem *nameItem = new QTableWidgetItem(item.name);
        nameItem->setData(Qt::UserRole, item.id);  // 存储ID
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_tableWidget->setItem(i, 0, nameItem);

        // 描述
        QTableWidgetItem *descItem = new QTableWidgetItem(item.description);
        descItem->setFlags(descItem->flags() & ~Qt::ItemIsEditable);
        m_tableWidget->setItem(i, 1, descItem);

        // 快捷键
        QString shortcutText = item.currentShortcut.toString(QKeySequence::NativeText);
        if (shortcutText.isEmpty()) {
            shortcutText = tr("(无)");
        }
        QTableWidgetItem *shortcutItem = new QTableWidgetItem(shortcutText);
        shortcutItem->setFlags(shortcutItem->flags() & ~Qt::ItemIsEditable);
        shortcutItem->setTextAlignment(Qt::AlignCenter);
        m_tableWidget->setItem(i, 2, shortcutItem);
    }
}

void ShortcutSettingsDialog::onItemSelectionChanged() {
    QList<QTableWidgetItem*> selected = m_tableWidget->selectedItems();
    if (selected.isEmpty()) {
        m_currentRow = -1;
        m_keySequenceEdit->clear();
        m_keySequenceEdit->setEnabled(false);
        return;
    }

    m_currentRow = selected.first()->row();
    if (m_currentRow >= 0 && m_currentRow < m_shortcuts.size()) {
        m_keySequenceEdit->setKeySequence(m_shortcuts[m_currentRow].currentShortcut);
        m_keySequenceEdit->setEnabled(true);
    }

    checkConflicts();
}

void ShortcutSettingsDialog::onShortcutChanged(const QKeySequence &keySequence) {
    if (m_currentRow < 0 || m_currentRow >= m_shortcuts.size()) {
        return;
    }

    // 检查是否与其他快捷键冲突
    if (hasConflict(keySequence, m_currentRow)) {
        m_conflictLabel->setText(tr("警告: 此快捷键已被其他功能使用！"));
        m_conflictLabel->setVisible(true);
    } else {
        m_conflictLabel->setVisible(false);
    }

    // 更新数据
    m_shortcuts[m_currentRow].currentShortcut = keySequence;

    // 更新表格显示
    QString shortcutText = keySequence.toString(QKeySequence::NativeText);
    if (shortcutText.isEmpty()) {
        shortcutText = tr("(无)");
    }
    m_tableWidget->item(m_currentRow, 2)->setText(shortcutText);
}

void ShortcutSettingsDialog::onClearClicked() {
    if (m_currentRow < 0 || m_currentRow >= m_shortcuts.size()) {
        return;
    }

    m_shortcuts[m_currentRow].currentShortcut = QKeySequence();
    m_keySequenceEdit->clear();
    m_tableWidget->item(m_currentRow, 2)->setText(tr("(无)"));
    m_conflictLabel->setVisible(false);
}

void ShortcutSettingsDialog::onResetClicked() {
    QMessageBox::StandardButton reply = QMessageBox::question(this,
                                                              tr("恢复默认"),
                                                              tr("确定要将所有快捷键恢复为默认设置吗？"),
                                                              QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        for (auto &item : m_shortcuts) {
            item.currentShortcut = item.defaultShortcut;
        }
        updateTable();

        if (m_currentRow >= 0) {
            m_keySequenceEdit->setKeySequence(m_shortcuts[m_currentRow].currentShortcut);
        }

        m_conflictLabel->setVisible(false);
    }
}

// 修复：添加缺失的 onConflictCheck 函数实现
void ShortcutSettingsDialog::onConflictCheck() {
    checkConflicts();
}

void ShortcutSettingsDialog::checkConflicts() {
    if (m_currentRow < 0) {
        m_conflictLabel->setVisible(false);
        return;
    }

    const QKeySequence &current = m_shortcuts[m_currentRow].currentShortcut;
    if (current.isEmpty()) {
        m_conflictLabel->setVisible(false);
        return;
    }

    if (hasConflict(current, m_currentRow)) {
        m_conflictLabel->setText(tr("警告: 此快捷键已被其他功能使用！"));
        m_conflictLabel->setVisible(true);
    } else {
        m_conflictLabel->setVisible(false);
    }
}

bool ShortcutSettingsDialog::hasConflict(const QKeySequence &shortcut, int excludeRow) const {
    if (shortcut.isEmpty()) {
        return false;
    }

    for (int i = 0; i < m_shortcuts.size(); ++i) {
        if (i == excludeRow) {
            continue;
        }
        if (m_shortcuts[i].currentShortcut == shortcut) {
            return true;
        }
    }
    return false;
}

QMap<QString, QKeySequence> ShortcutSettingsDialog::getShortcuts() const {
    QMap<QString, QKeySequence> result;
    for (const auto &item : m_shortcuts) {
        result[item.id] = item.currentShortcut;
    }
    return result;
}

void ShortcutSettingsDialog::setShortcut(const QString &id, const QKeySequence &shortcut) {
    for (auto &item : m_shortcuts) {
        if (item.id == id) {
            item.currentShortcut = shortcut;
            break;
        }
    }
    updateTable();
}

void ShortcutSettingsDialog::resetToDefaults() {
    for (auto &item : m_shortcuts) {
        item.currentShortcut = item.defaultShortcut;
    }
    updateTable();
}