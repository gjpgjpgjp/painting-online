#include "NetworkManager.h"
#include <QJsonDocument>
#include <QDataStream>

NetworkManager::NetworkManager(QObject *parent) : QObject(parent), m_socket(nullptr) {}

bool NetworkManager::connectToServer(const QString &host, quint16 port) {
    if (m_socket) { m_socket->abort(); delete m_socket; }
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &NetworkManager::connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &NetworkManager::disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &NetworkManager::onSocketError);
    m_socket->connectToHost(host, port);
    return true;
}

void NetworkManager::disconnectFromServer() {
    if (m_socket) m_socket->disconnectFromHost();
}

bool NetworkManager::isConnected() const {
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void NetworkManager::sendCommand(const QJsonObject &obj) {
    if (!isConnected()) return;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream << quint32(data.size());
    packet.append(data);
    m_socket->write(packet);
}

void NetworkManager::onReadyRead() {
    m_buffer.append(m_socket->readAll());
    while (m_buffer.size() >= 4) {
        QDataStream stream(m_buffer);
        quint32 blockSize;
        stream >> blockSize;
        if (m_buffer.size() < 4 + blockSize) break;
        QByteArray jsonData = m_buffer.mid(4, blockSize);
        m_buffer.remove(0, 4 + blockSize);
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (doc.isObject()) emit commandReceived(doc.object());
    }
}

void NetworkManager::onSocketError(QAbstractSocket::SocketError) {
    emit errorOccurred(m_socket->errorString());
}