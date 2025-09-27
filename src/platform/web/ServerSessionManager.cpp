#include "ServerSessionManager.h"
#include "ClientSocketWorker.h"
#include "VideoSink.h"
#include "WebMultiplayerServer.h"
#include "ClientSessionHandler.h"
#include <QDebug>
#include <QThread>

using namespace QGBA;

ServerSessionManager::ServerSessionManager(WebMultiplayerServer* server, QObject* parent)
    : QObject(parent)
    , m_server(server) { }

ServerSessionManager::~ServerSessionManager() { }

void ServerSessionManager::onWorkerRegister(const QHostAddress& addr, quint16 udpPort) {
	if (!m_server) return;
	ClientSocketWorker* worker = qobject_cast<ClientSocketWorker*>(sender());
	if (!worker) return;
	if (!m_workerToSession.contains(worker)) return;
	QString sessionId = m_workerToSession.value(worker);
	auto it = m_server->m_clients.find(sessionId);
	if (it == m_server->m_clients.end()) return;
	WebMultiplayerServer::ClientSession* s = it.value();
	s->udpAddress = addr; s->udpPort = udpPort;
	QString peerIp = s->udpAddress.toString(); if (peerIp.startsWith("::ffff:")) peerIp = peerIp.mid(QString("::ffff:").length());
	QString udpUrl = QString("udp://%1:%2?pkt_size=1316").arg(peerIp).arg(s->udpPort);
	s->VideoSink = new VideoSink(); if (!s->VideoSink->open(udpUrl)) { qDebug() << "Failed to open VideoSink for" << udpUrl; delete s->VideoSink; s->VideoSink = nullptr; }
	qDebug() << "Control client registered:" << sessionId << "udpPort=" << udpPort;
	if (!s->coreController) m_server->initializeEmulator(s);
	if (s->handler) {
		s->handler->sendConnectionAck();
		s->handler->sendPlayerInfo(m_server->m_clients.size());
	} else {
		m_server->sendControlToSession(s, mgba::CM_CONNECTION, QByteArray());
		QByteArray pi; pi.append(static_cast<char>(s->playerId & 0xFF)); pi.append(static_cast<char>(m_server->m_clients.size() & 0xFF));
		m_server->sendControlToSession(s, mgba::CM_PLAYERINFO, pi);
	}
}

void ServerSessionManager::onWorkerInput(uint8_t action, uint8_t key) {
	if (!m_server) return;
	ClientSocketWorker* worker = qobject_cast<ClientSocketWorker*>(sender());
	if (!worker) return;
	if (!m_workerToSession.contains(worker)) return;
	QString sessionId = m_workerToSession.value(worker);
	auto it = m_server->m_clients.find(sessionId);
	if (it == m_server->m_clients.end()) return;
	if (it.value()->handler) it.value()->handler->handleInput(action, key);
}

void ServerSessionManager::onWorkerDisconnected() {
	if (!m_server) return;
	ClientSocketWorker* worker = qobject_cast<ClientSocketWorker*>(sender());
	if (!worker) return;
	if (!m_workerToSession.contains(worker)) return;
	QString sessionId = m_workerToSession.value(worker);
	auto it = m_server->m_clients.find(sessionId);
	if (it == m_server->m_clients.end()) return;
	WebMultiplayerServer::ClientSession* session = it.value();
	int pid = session->playerId;
	m_server->cleanupEmulator(session);
	m_server->m_clients.remove(sessionId);
	if (pid >= 0 && pid < m_server->m_playerSlots.size()) m_server->m_playerSlots[pid] = false;
	if (session->socket) session->socket->deleteLater();
	delete session;
	qDebug() << "Client disconnected:" << sessionId;
	emit m_server->clientDisconnected(sessionId);
	// cleanup stored mappings
	if (m_sessionToWorker.contains(sessionId)) m_sessionToWorker.remove(sessionId);
	if (m_sessionToThread.contains(sessionId)) m_sessionToThread.remove(sessionId);
	if (m_workerToSession.contains(worker)) m_workerToSession.remove(worker);
}

void ServerSessionManager::handleIncomingDescriptor(qintptr socketDescriptor) {
	if (!m_server)
		return;
	// Recreate the session handling logic previously in WebMultiplayerServer
	if (m_server->m_clients.size() >= m_server->m_maxClients) {
		QTcpSocket temp;
		if (temp.setSocketDescriptor(socketDescriptor)) {
			QByteArray err;
			err.append((char) 1);
			m_server->sendControlMessage(&temp, mgba::CM_CONNECTION, err);
			temp.close();
		}
		return;
	}

	QString sessionId = m_server->generateSessionId();
	auto session = new WebMultiplayerServer::ClientSession();
	session->socket = nullptr;
	session->sessionId = sessionId;
	int pid = -1;
	for (int i = 0; i < m_server->m_playerSlots.size(); ++i) {
		if (!m_server->m_playerSlots[i]) {
			pid = i;
			break;
		}
	}
	if (pid < 0)
		pid = m_server->m_nextPlayerId++;
	else
		m_server->m_playerSlots[pid] = true;
	session->playerId = pid;
	session->connected = true;
	session->primed = false;
	m_server->m_clients[sessionId] = session;

	ClientSocketWorker* worker = new ClientSocketWorker(socketDescriptor);
	QThread* thread = new QThread(m_server);
	session->worker = worker;
	worker->moveToThread(thread);
	connect(thread, &QThread::started, worker, &ClientSocketWorker::start);

	// Hook worker signals directly to manager slots. Qt will queue across threads.
	connect(worker, &ClientSocketWorker::registerReceived, this, &ServerSessionManager::onWorkerRegister);
	connect(worker, &ClientSocketWorker::inputReceived, this, &ServerSessionManager::onWorkerInput);
	connect(worker, &ClientSocketWorker::disconnected, this, &ServerSessionManager::onWorkerDisconnected);

	// store mapping
	m_threadToSession[thread] = sessionId;
	m_sessionToThread[sessionId] = thread;
	m_sessionToWorker[sessionId] = worker;
	m_workerToSession[worker] = sessionId;

	// Create a session-level handler for input
	session->handler = new ClientSessionHandler(m_server, session, m_server);

	thread->start();
	qDebug() << "Client connected:" << sessionId << "Player ID:" << session->playerId;
	emit m_server->clientConnected(sessionId);
}
