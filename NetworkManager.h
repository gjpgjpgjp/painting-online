#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <QObject>
#include <QTcpSocket>
#include <QJsonObject>

class NetworkManager : public QObject
{
    Q_OBJECT
public:
    explicit NetworkManager(QObject *parent = nullptr);
    bool connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    void sendCommand(const QJsonObject &obj);
    bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);
    void commandReceived(const QJsonObject &obj);
    void requestSync();  // 客人发送同步请求

private slots:
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    QTcpSocket *m_socket;
    QByteArray m_buffer;
};

#endif // NETWORKMANAGER_H