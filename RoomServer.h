#ifndef ROOMSERVER_H
#define ROOMSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDataStream>
#include <QHash>
#include <QDateTime>
#include <QUuid>  // 添加 QUuid 的包含

class RoomServer : public QObject {
    Q_OBJECT
public:
    explicit RoomServer(quint16 port, QObject *parent = nullptr);
    bool isListening() const;

    // 设置当前完整状态（由房主调用）
    void setFullState(const QJsonObject &state);

    // 添加命令到历史
    void addCommand(const QJsonObject &cmdData);

    // 广播消息
    void broadcast(const QJsonObject &obj, QTcpSocket *exclude = nullptr);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onReadyRead();

private:
    struct Client {
        QTcpSocket *socket;
        QByteArray buffer;
        qint64 lastActivity;  // 用于超时检测
        bool stateSynced = false;  // 是否已完成初始同步
        QString clientId;
    };

    QTcpServer *m_server;
    QHash<QTcpSocket*, Client> m_clients;
    QList<QJsonObject> m_history;  // 命令历史（用于增量同步）
    QJsonObject m_currentFullState;  // 当前完整状态缓存
    qint64 m_lastStateUpdate = 0;
    static constexpr int MAX_HISTORY_SIZE = 1000;
    static constexpr int STATE_CACHE_INTERVAL = 30000;  // 30秒缓存一次状态

    void sendInit(QTcpSocket *socket);
    void sendFullState(QTcpSocket *socket);
    void sendPacket(QTcpSocket *socket, const QJsonObject &obj);  // 添加声明
    void pruneHistory();  // 清理旧历史
    void updateStateCache();  // 更新状态缓存
};

#endif // ROOMSERVER_H