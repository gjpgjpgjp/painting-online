#include "RoomServer.h"
#include <QHostAddress>
#include <QDebug>
#include <QFile>
#include <QDir>
RoomServer::RoomServer(quint16 port, QObject *parent)
    : QObject(parent)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &RoomServer::onNewConnection);

    if (!m_server->listen(QHostAddress::Any, port)) {
        qCritical() << "Failed to start server:" << m_server->errorString();
    }
}

bool RoomServer::isListening() const {
    return m_server->isListening();
}

void RoomServer::setFullState(const QJsonObject &state) {
    m_currentFullState = state;
    m_lastStateUpdate = QDateTime::currentMSecsSinceEpoch();
}

void RoomServer::addCommand(const QJsonObject &cmdData) {
    m_history.append(cmdData);
    pruneHistory();

    QJsonObject obj;
    obj["type"] = "cmd";
    obj["data"] = cmdData;
    obj["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    broadcast(obj, nullptr);

    // 定期更新状态缓存
    if (QDateTime::currentMSecsSinceEpoch() - m_lastStateUpdate > STATE_CACHE_INTERVAL) {
        // 通知房主更新状态缓存
        QJsonObject cacheRequest;
        cacheRequest["type"] = "requestStateCache";
        broadcast(cacheRequest, nullptr);
    }
}

void RoomServer::broadcast(const QJsonObject &obj, QTcpSocket *exclude) {
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream << quint32(data.size());
    packet.append(data);

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        // 关键：排除发送者自己
        if (exclude && it.key() == exclude) {
            continue;
        }

        if (it->socket->state() == QAbstractSocket::ConnectedState) {
            it->socket->write(packet);
        }
    }
}

void RoomServer::sendPacket(QTcpSocket *socket, const QJsonObject &obj) {
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream << quint32(data.size());
    packet.append(data);
    socket->write(packet);
}

void RoomServer::onNewConnection() {
    QTcpSocket *socket = m_server->nextPendingConnection();

    Client client;
    client.socket = socket;
    client.buffer.clear();
    client.lastActivity = QDateTime::currentMSecsSinceEpoch();
    client.stateSynced = false;
    client.clientId = QUuid::createUuid().toString();

    m_clients.insert(socket, client);

    connect(socket, &QTcpSocket::disconnected, this, &RoomServer::onClientDisconnected);
    connect(socket, &QTcpSocket::readyRead, this, &RoomServer::onReadyRead);

    qDebug() << "New client connected:" << client.clientId;

    // 发送客户端ID
    QJsonObject welcome;
    welcome["type"] = "welcome";
    welcome["clientId"] = client.clientId;
    sendPacket(socket, welcome);

    // 如果有缓存的完整状态，立即发送
    if (!m_currentFullState.isEmpty()) {
        sendFullState(socket);
    } else {
        // 否则发送历史记录
        sendInit(socket);
    }
}

void RoomServer::sendInit(QTcpSocket *socket) {
    QJsonObject init;
    init["type"] = "init";
    init["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    QJsonArray cmds;
    for (const auto &cmdData : m_history) {
        cmds.append(cmdData);
    }
    init["cmds"] = cmds;
    init["historySize"] = m_history.size();

    sendPacket(socket, init);
    m_clients[socket].stateSynced = true;
}

void RoomServer::sendFullState(QTcpSocket *socket) {
    QJsonObject state = m_currentFullState;
    state["type"] = "fullState";
    state["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    sendPacket(socket, state);
    m_clients[socket].stateSynced = true;

    qDebug() << "Sent full state to client";
}

void RoomServer::onClientDisconnected() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (socket && m_clients.contains(socket)) {
        qDebug() << "Client disconnected:" << m_clients[socket].clientId;
        m_clients.remove(socket);
        socket->deleteLater();
    }
}

void RoomServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || !m_clients.contains(socket)) return;

    Client &client = m_clients[socket];
    client.lastActivity = QDateTime::currentMSecsSinceEpoch();
    client.buffer.append(socket->readAll());

    while (client.buffer.size() >= 4) {
        QDataStream stream(client.buffer);
        quint32 blockSize;
        stream >> blockSize;

        if (client.buffer.size() < 4 + blockSize) break;

        QByteArray jsonData = client.buffer.mid(4, blockSize);
        client.buffer.remove(0, 4 + blockSize);

        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (!doc.isObject()) continue;

        QJsonObject obj = doc.object();
        QString msgType = obj["type"].toString();

        QString senderId = m_clients[socket].clientId;
        obj["clientId"] = senderId;
        obj["_fromSelf"] = false;

        if (msgType == "cmd") {
            m_history.append(obj["data"].toObject());
            pruneHistory();
            broadcast(obj, socket);
        }
        else if (msgType == "layerOp" || msgType == "layerSync" ||
                 msgType == "deleteCmds" || msgType == "moveCmds" ||
                 msgType == "restoreCmds" || msgType == "restoreLayer") {
            broadcast(obj, socket);
        }
        else if (msgType == "undoCmd" || msgType == "redoCmd") {
            broadcast(obj, socket);
            qDebug() << "Broadcasted" << msgType << "from client" << senderId;
        }
        else if (msgType == "syncRequest") {
            if (!m_currentFullState.isEmpty()) {
                sendFullState(socket);
            } else {
                sendInit(socket);
            }
        }
        else if (msgType == "fullState") {
            if (obj.contains("state")) {
                setFullState(obj["state"].toObject());
                for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
                    if (it.key() != socket && !it->stateSynced) {
                        sendFullState(it.key());
                    }
                }
            }
        }
        else if (msgType == "canvasReset") {
            if (obj.contains("state")) {
                setFullState(obj["state"].toObject());
                broadcast(obj, socket);
                m_history.clear();
            }
        }
        else if (msgType == "requestStateCache") {
            broadcast(obj, socket);
        }
        else if (msgType == "uploadCanvas") {
            QString fileName = obj["fileName"].toString();
            QJsonObject modelData = obj["data"].toObject();

            QFile file(fileName);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(modelData).toJson());
                file.close();

                QJsonObject ack;
                ack["type"] = "uploadAck";
                ack["success"] = true;
                ack["fileName"] = fileName;
                sendPacket(socket, ack);
            } else {
                QJsonObject ack;
                ack["type"] = "uploadAck";
                ack["success"] = false;
                ack["error"] = "无法创建文件";
                sendPacket(socket, ack);
            }
        }
        else if (msgType == "downloadCanvas") {
            QString fileName = obj["fileName"].toString();
            QFile file(fileName);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray data = file.readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (doc.isObject()) {
                    QJsonObject reply;
                    reply["type"] = "downloadData";
                    reply["fileName"] = fileName;
                    reply["data"] = doc.object();
                    sendPacket(socket, reply);
                } else {
                    QJsonObject err;
                    err["type"] = "downloadError";
                    err["error"] = "文件格式错误";
                    sendPacket(socket, err);
                }
                file.close();
            } else {
                QJsonObject err;
                err["type"] = "downloadError";
                err["error"] = "文件不存在或无法读取";
                sendPacket(socket, err);
            }
        }
        else if (msgType == "listCanvasFiles") {
            QDir dir(".");  // 服务器当前工作目录
            QStringList filters;
            filters << "*.canvas";
            QStringList files = dir.entryList(filters, QDir::Files);
            QJsonObject reply;
            reply["type"] = "canvasFileList";
            QJsonArray fileArray;
            for (const QString &f : files) {
                fileArray.append(f);
            }
            reply["files"] = fileArray;
            sendPacket(socket, reply);
        }
    }
}

void RoomServer::pruneHistory() {
    // 限制历史大小，防止内存无限增长
    while (m_history.size() > MAX_HISTORY_SIZE) {
        m_history.removeFirst();
    }
}

void RoomServer::updateStateCache() {
    // 实现状态缓存更新逻辑
    m_lastStateUpdate = QDateTime::currentMSecsSinceEpoch();
}