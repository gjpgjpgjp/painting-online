#ifndef ROOMSERVER_H
#define ROOMSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDataStream>

class RoomServer : public QObject {
    Q_OBJECT
public:
    explicit RoomServer(quint16 port, QObject *parent = nullptr);
    bool isListening() const;
    void addCommand(const QJsonObject &cmdData);  // 新增：添加指令到历史

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onReadyRead();

private:
    struct Client {
        QTcpSocket *socket;
        QByteArray buffer;
    };

    QTcpServer *m_server;
    QList<Client> m_clients;
    QList<QJsonObject> m_history;  // 动态维护的历史记录

    void broadcast(const QJsonObject &obj, QTcpSocket *exclude);
    void sendInit(QTcpSocket *socket);
};

#endif // ROOMSERVER_H