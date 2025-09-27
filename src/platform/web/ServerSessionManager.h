#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QHostAddress>
#include <QThread>

namespace QGBA {

class WebMultiplayerServer;
class ClientSocketWorker;

class ServerSessionManager : public QObject {
    Q_OBJECT
public:
    explicit ServerSessionManager(WebMultiplayerServer* server, QObject* parent = nullptr);
    ~ServerSessionManager();

    void handleIncomingDescriptor(qintptr socketDescriptor);

public slots:
    // slots match ClientSocketWorker signals; map worker -> session internally
    void onWorkerRegister(const QHostAddress& addr, quint16 udpPort);
    void onWorkerInput(uint8_t action, uint8_t key);
    void onWorkerDisconnected();

private:
    // track worker -> session mapping
    QMap<QThread*, QString> m_threadToSession;
    QMap<QString, QThread*> m_sessionToThread;
    QMap<QString, ClientSocketWorker*> m_sessionToWorker;
    QMap<ClientSocketWorker*, QString> m_workerToSession;

private:
    WebMultiplayerServer* m_server;
};

}
