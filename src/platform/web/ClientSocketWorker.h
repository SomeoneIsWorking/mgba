#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QThread>
#include <QHostAddress>
#include "StreamingCommon.h"

namespace QGBA {

class ClientSocketWorker : public QObject {
    Q_OBJECT
public:
    explicit ClientSocketWorker(qintptr socketDescriptor, QObject* parent = nullptr);
    ~ClientSocketWorker();

signals:
    void registerReceived(const QHostAddress& addr, quint16 udpPort);
    void inputReceived(uint8_t action, uint8_t key);
    void connectionPing();
    void disconnected();

public slots:
    void start();
    void onReadyRead();
    void onDisconnected();
    void sendControlMessage(uint8_t type, const QByteArray& payload);

private:
    QTcpSocket* m_socket;
    QByteArray m_buffer;
    qintptr m_socketDescriptor;
};

}
