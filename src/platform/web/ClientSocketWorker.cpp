#include "ClientSocketWorker.h"
#include <QDebug>

namespace QGBA {

ClientSocketWorker::ClientSocketWorker(qintptr socketDescriptor, QObject* parent)
    : QObject(parent), m_socket(nullptr), m_socketDescriptor(socketDescriptor) {
}

ClientSocketWorker::~ClientSocketWorker() {
    if (m_socket) {
        m_socket->close();
        m_socket = nullptr;
    }
}

void ClientSocketWorker::start() {
    if (!m_socket) {
        m_socket = new QTcpSocket(this);
        if (!m_socket->setSocketDescriptor(m_socketDescriptor)) {
            qDebug() << "ClientSocketWorker: failed to set socket descriptor" << m_socketDescriptor;
            delete m_socket;
            m_socket = nullptr;
            return;
        }
    }
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientSocketWorker::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientSocketWorker::onDisconnected);
}

void ClientSocketWorker::onReadyRead() {
    if (!m_socket) return;
    m_buffer.append(m_socket->readAll());
    while (m_buffer.size() >= (int)sizeof(mgba::ControlHeader)) {
        mgba::ControlHeader hdr;
        memcpy(&hdr, m_buffer.constData(), sizeof(hdr));
        uint8_t type = hdr.type;
        uint16_t len = ntohs(hdr.len);
        if (m_buffer.size() < (int)(sizeof(hdr) + len)) break;
        QByteArray payload = m_buffer.mid(sizeof(hdr), len);
        m_buffer.remove(0, sizeof(hdr) + len);

        if (type == mgba::CM_REGISTER) {
            if (payload.size() >= 2) {
                uint16_t p; memcpy(&p, payload.constData(), 2); p = ntohs(p);
                emit registerReceived(m_socket->peerAddress(), p);
            }
        } else if (type == mgba::CM_INPUT) {
            if (payload.size() >= 2) {
                uint8_t action = static_cast<uint8_t>(payload[0]);
                uint8_t key = static_cast<uint8_t>(payload[1]);
                emit inputReceived(action, key);
            }
        } else if (type == mgba::CM_CONNECTION) {
            emit connectionPing();
        }
    }
}

void ClientSocketWorker::onDisconnected() {
    emit disconnected();
}

void ClientSocketWorker::sendControlMessage(uint8_t type, const QByteArray& payload) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    mgba::ControlHeader hdr;
    hdr.type = type;
    hdr.len = htons((uint16_t)payload.size());
    QByteArray out;
    out.append(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (!payload.isEmpty()) out.append(payload);
    m_socket->write(out);
}

} // namespace QGBA
