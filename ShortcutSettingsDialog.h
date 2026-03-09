#ifndef SHORTCUTSETTINGSDIALOG_H
#define SHORTCUTSETTINGSDIALOG_H

#include <QDialog>
#include <QKeySequence>
#include <QMap>
#include <QString>

QT_BEGIN_NAMESPACE
class QTableWidget;
class QKeySequenceEdit;
class QPushButton;
class QLabel;
QT_END_NAMESPACE

struct ShortcutItem {
    QString id;
    QString name;
    QString description;
    QKeySequence defaultShortcut;
    QKeySequence currentShortcut;
};

class ShortcutSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutSettingsDialog(QWidget *parent = nullptr);
    ~ShortcutSettingsDialog();

    QMap<QString, QKeySequence> getShortcuts() const;
    void setShortcut(const QString &id, const QKeySequence &shortcut);
    void resetToDefaults();

    static const QString ID_TOOL_PEN;
    static const QString ID_TOOL_RECT;
    static const QString ID_TOOL_ELLIPSE;
    static const QString ID_TOOL_ERASER_FULL;
    static const QString ID_TOOL_ERASER_REALTIME;
    static const QString ID_TOOL_LASSO;
    static const QString ID_EDIT_UNDO;
    static const QString ID_EDIT_REDO;
    static const QString ID_EDIT_CLEAR;
    static const QString ID_FILE_NEW;
    static const QString ID_FILE_OPEN;
    static const QString ID_FILE_SAVE;
    static const QString ID_VIEW_SYNC;
    static const QString ID_SELECTION_PAINT;
    static const QString ID_SELECTION_CLEAR;
    static const QString ID_SELECTION_DRAG;

signals:
    void shortcutsChanged(const QMap<QString, QKeySequence> &shortcuts);

private slots:
    void onShortcutChanged(const QKeySequence &keySequence);
    void onResetClicked();
    void onClearClicked();
    void onItemSelectionChanged();
    void onConflictCheck();

private:
    void setupUI();
    void setupConnections();
    void initializeShortcuts();
    void updateTable();
    void checkConflicts();
    bool hasConflict(const QKeySequence &shortcut, int excludeRow) const;

    QTableWidget *m_tableWidget;
    QKeySequenceEdit *m_keySequenceEdit;
    QPushButton *m_resetButton;
    QPushButton *m_clearButton;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    QLabel *m_conflictLabel;

    QList<ShortcutItem> m_shortcuts;
    int m_currentRow;
};

#endif // SHORTCUTSETTINGSDIALOG_H