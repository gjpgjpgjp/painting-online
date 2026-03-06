#include "RoomServer.h"

RoomServer::RoomServer(quint16 port, QObject *parent)
    : QObject(parent) {
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &RoomServer::onNewConnection);
    m_server->listen(QHostAddress::Any, port);
}

bool RoomServer::isListening() const {
    return m_server->isListening();
}

void RoomServer::addCommand(const QJsonObject &cmdData) {
    m_history.append(cmdData);
    QJsonObject obj;
    obj["type"] = "cmd";
    obj["data"] = cmdData;
    broadcast(obj, nullptr);  // 广播给所有客户端
}

void RoomServer::onNewConnection() {
    QTcpSocket *socket = m_server->nextPendingConnection();
    Client c;
    c.socket = socket;
    m_clients.append(c);

    connect(socket, &QTcpSocket::disconnected, this, &RoomServer::onClientDisconnected);
    connect(socket, &QTcpSocket::readyRead, this, &RoomServer::onReadyRead);

    // 发送完整历史给新客户端
    sendInit(socket);
}

void RoomServer::sendInit(QTcpSocket *socket) {
    QJsonObject init;
    init["type"] = "init";
    QJsonArray cmds;
    for (const auto &cmdData : m_history) {
        cmds.append(cmdData);
    }
    init["cmds"] = cmds;

    QByteArray data = QJsonDocument(init).toJson(QJsonDocument::Compact);
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream << quint32(data.size());
    packet.append(data);
    socket->write(packet);
}

void RoomServer::onClientDisconnected() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    m_clients.removeIf([socket](const Client &c) { return c.socket == socket; });
    socket->deleteLater();
}

void RoomServer::onReadyRead() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
                           [socket](const Client &c) { return c.socket == socket; });
    if (it == m_clients.end()) return;

    it->buffer.append(socket->readAll());
    while (it->buffer.size() >= 4) {
        QDataStream stream(it->buffer);
        quint32 blockSize;
        stream >> blockSize;
        if (it->buffer.size() < 4 + blockSize) break;

        QByteArray jsonData = it->buffer.mid(4, blockSize);
        it->buffer.remove(0, 4 + blockSize);

        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            QString msgType = obj["type"].toString();

            if (msgType == "cmd") {
                // 保存到历史并广播
                m_history.append(obj["data"].toObject());
                broadcast(obj, socket);
            }
            else if (msgType == "layerOp" || msgType == "layerSync" || msgType == "fullSync") {
                // 转发图层操作和完整同步
                broadcast(obj, socket);
            }
            else if (msgType == "syncRequest") {
                // 转发同步请求给房主
                broadcast(obj, socket);
            }
        }
    }
}

void RoomServer::broadcast(const QJsonObject &obj, QTcpSocket *exclude) {
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream << quint32(data.size());
    packet.append(data);

    for (const auto &c : m_clients) {
        if (c.socket != exclude) c.socket->write(packet);
    }
}