/* Copyright (c) 2013-2023 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "WebMultiplayerServer.h"

#include "ClientSocketWorker.h"
#include "StreamingCommon.h"
#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QMessageLogContext>
#include <QMimeDatabase>
#include <QTcpSocket>
#include <QTextStream>
#include <QThread>
#include <QUuid>
#include <QtGlobal>
#include <cstdio>
#include <memory>

#include "VideoSink.h"
#include "WebBroadcaster.h"
#include <QHostInfo>
#include <QTcpServer>
#include <QThread>

#include "NativeTcpServer.h"
#include "ServerSessionManager.h"
#include "ClientSessionHandler.h"
#include "WebServerGui.h"
#include <arpa/inet.h>
#include <mgba-util/audio-buffer.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/input.h>

#include "../qt/CoreManager.h"
#include "../qt/InputController.h"

using namespace QGBA;

static void web_qt_message_handler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    Q_UNUSED(context);
	const char* t = "DEBUG";
	switch (type) {
	case QtDebugMsg:
		t = "DEBUG";
		break;
	case QtInfoMsg:
		t = "INFO";
		break;
	case QtWarningMsg:
		t = "WARN";
		break;
	case QtCriticalMsg:
		t = "CRIT";
		break;
	case QtFatalMsg:
		t = "FATAL";
		break;
	}
	std::fprintf(stderr, "qt[%s] %s\n", t, msg.toUtf8().constData());
	std::fflush(stderr);
}

WebMultiplayerServer::WebMultiplayerServer(ConfigController* config)
    : m_controlServer(nullptr)
    , m_streamSocket(nullptr)
    , m_streamPort(49000)
    , m_controlPort(49001)
    , m_multiplayerController(nullptr)
    , m_coreManager(nullptr)
    , m_broadcaster(nullptr)
    , m_romPath("kirby.gba") // Default to kirby.gba in project root
    , m_webRoot("web") // Default web root directory
    , m_maxClients(4)
    , m_serverRunning(false)
    , m_nextPlayerId(0) {
	m_configController = config;
	// Ensure Qt debug/info/warning messages appear on stderr
	qInstallMessageHandler(web_qt_message_handler);
	m_playerSlots.resize(m_maxClients);
	for (int i = 0; i < m_maxClients; ++i)
		m_playerSlots[i] = false;
}

bool WebMultiplayerServer::startServer() {
	if (m_serverRunning)
		return false;

	// Use NativeTcpServer which emits raw socket descriptors so we can create
	// QTcpSocket instances in dedicated worker threads instead of in the main thread.
	NativeTcpServer* nts = new NativeTcpServer(this);
	m_controlServer = nts;
	// Session manager forwards descriptors into the server's handler
	ServerSessionManager* mgr = new ServerSessionManager(this, this);
	m_sessionManager = mgr;
	connect(nts, &NativeTcpServer::haveDescriptor, mgr, &ServerSessionManager::handleIncomingDescriptor);
	if (!nts->listen(QHostAddress::Any, m_controlPort)) {
		qDebug() << "Failed to start control TCP server on port" << m_controlPort << nts->errorString();
		emit errorOccurred(
		    QString("Failed to start control TCP server on port %1: %2").arg(m_controlPort).arg(nts->errorString()));
		delete nts;
		m_controlServer = nullptr;
		return false;
	}

	// Initialize multiplayer controller
	if (!m_multiplayerController) {
		m_multiplayerController = new MultiplayerController();
	}

	// Initialize core manager
	if (!m_coreManager) {
		m_coreManager = new CoreManager();
		m_coreManager->setConfig(m_configController->config());
		m_coreManager->setMultiplayerController(m_multiplayerController);
	}

	m_serverRunning = true;

	// Start broadcaster
	if (!m_broadcaster)
		m_broadcaster = new WebBroadcaster(this);
	m_broadcaster->setServerInfo(QHostInfo::localHostName(), m_streamPort, m_controlPort);
	m_broadcaster->start();

	// Create a server GUI that shares the server's core manager and multiplayer controller
	if (!m_coreManager) {
		m_coreManager = new CoreManager();
		m_coreManager->setConfig(m_configController->config());
		m_coreManager->setMultiplayerController(m_multiplayerController);
	}
	// Create GUI and show it
	WebServerGui* gui = new WebServerGui(nullptr, m_configController, m_coreManager, m_multiplayerController);
	gui->show();
	// store until destructor to delete
	m_serverGui = gui;

	// Create UDP stream socket
	m_streamSocket = new QUdpSocket(this);
	if (!m_streamSocket->bind(QHostAddress::AnyIPv4, m_streamPort,
	                          QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
		qDebug() << "Warning: failed to bind stream UDP port" << m_streamPort << m_streamSocket->errorString();
		if (!m_streamSocket->bind(QHostAddress::AnyIPv4, 0)) {
			qDebug() << "Failed to bind UDP stream socket:" << m_streamSocket->errorString();
			delete m_streamSocket;
			m_streamSocket = nullptr;
		}
	}

	emit serverStarted();
	return true;
}

void WebMultiplayerServer::stopServer() {
	// Forward pending descriptor(s) to the session manager which centralizes accept handling
	while (m_controlServer && m_controlServer->hasPendingConnections()) {
		QTcpSocket* socket = m_controlServer->nextPendingConnection();
		qintptr sd = socket->socketDescriptor();
		// Close the temporary socket so it's not owned by the main thread
		socket->close();
		socket->deleteLater();
		if (m_sessionManager) {
			m_sessionManager->handleIncomingDescriptor(sd);
		} else {
			// Fallback: invoke existing descriptor handler
			onIncomingConnectionDescriptor(sd);
		}
	}
}

void WebMultiplayerServer::setRomPath(const QString& romPath) {
	m_romPath = romPath;
}

void WebMultiplayerServer::onIncomingConnectionDescriptor(qintptr socketDescriptor) {
	// Delegate descriptor handling to the central session manager which
	// owns worker/thread creation and per-session adapters.
	if (m_sessionManager) {
		m_sessionManager->handleIncomingDescriptor(socketDescriptor);
	} else {
		// Fallback to the previous inline logic that replies an error
		if (m_clients.size() >= m_maxClients) {
			QTcpSocket temp;
			if (temp.setSocketDescriptor(socketDescriptor)) {
				QByteArray err;
				err.append((char)1);
				sendControlMessage(&temp, mgba::CM_CONNECTION, err);
				temp.close();
			}
		} else {
			// If no session manager exists, convert descriptor to a temporary socket and close it.
			QTcpSocket temp;
			if (temp.setSocketDescriptor(socketDescriptor)) {
				temp.close();
			}
		}
	}
}

WebMultiplayerServer::~WebMultiplayerServer() {
	stopServer();
	if (m_multiplayerController) {
		delete m_multiplayerController;
		m_multiplayerController = nullptr;
	}
	if (m_coreManager) {
		delete m_coreManager;
		m_coreManager = nullptr;
	}
	if (m_broadcaster) {
		m_broadcaster->stop();
		delete m_broadcaster;
		m_broadcaster = nullptr;
	}
	if (m_controlServer) {
		m_controlServer->close();
		delete m_controlServer;
		m_controlServer = nullptr;
	}
	if (m_streamSocket) {
		m_streamSocket->close();
		delete m_streamSocket;
		m_streamSocket = nullptr;
	}

	if (m_serverGui) {
		m_serverGui->close();
		delete m_serverGui;
		m_serverGui = nullptr;
	}
}

bool WebMultiplayerServer::isRunning() const {
	return m_serverRunning;
}

void WebMultiplayerServer::onNewConnection() {
	while (m_controlServer && m_controlServer->hasPendingConnections()) {
		QTcpSocket* socket = m_controlServer->nextPendingConnection();
		qintptr sd = socket->socketDescriptor();
		socket->close();
		socket->deleteLater();
		if (m_sessionManager) m_sessionManager->handleIncomingDescriptor(sd);
		else onIncomingConnectionDescriptor(sd);
	}
}

void WebMultiplayerServer::handleSocketReadyRead() {
	QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
	if (!socket)
		return;

	// Find the session associated with this socket
	ClientSession* session = nullptr;
	QString sessionId;
	for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
		if (it.value()->socket == socket) {
			session = it.value();
			sessionId = it.key();
			break;
		}
	}
	if (!session)
		return;

	QByteArray data = socket->readAll();
	QByteArray buf = socket->property("recvBuffer").toByteArray();
	buf.append(data);
	// Process as [ControlHeader][payload] frames
	while (buf.size() >= (int) sizeof(mgba::ControlHeader)) {
		mgba::ControlHeader hdr;
		memcpy(&hdr, buf.constData(), sizeof(hdr));
		uint8_t type = hdr.type;
		uint16_t len = ntohs(hdr.len);
		if (buf.size() < (int) (sizeof(hdr) + len))
			break; // wait for full payload
		QByteArray payload = buf.mid(sizeof(hdr), len);
		// consume frame
		buf.remove(0, sizeof(hdr) + len);

		if (type == mgba::CM_REGISTER) {
			if (payload.size() >= 2) {
				uint16_t clientUdpPort; memcpy(&clientUdpPort, payload.constData(), 2); clientUdpPort = ntohs(clientUdpPort);
				session->udpAddress = socket->peerAddress(); session->udpPort = clientUdpPort;
				QString peerIp = session->udpAddress.toString(); if (peerIp.startsWith("::ffff:")) peerIp = peerIp.mid(QString("::ffff:").length());
				QString udpUrl = QString("udp://%1:%2?pkt_size=1316").arg(peerIp).arg(session->udpPort);
				session->VideoSink = new VideoSink(); if (!session->VideoSink->open(udpUrl)) { qDebug() << "Failed to open VideoSink for" << udpUrl; delete session->VideoSink; session->VideoSink = nullptr; }
				qDebug() << "Control client registered:" << sessionId << "udpPort=" << clientUdpPort;
				if (!session->coreController) initializeEmulator(session);
				if (session->handler) {
					session->handler->sendConnectionAck();
					session->handler->sendPlayerInfo(m_clients.size());
				} else {
					sendControlToSession(session, mgba::CM_CONNECTION, QByteArray());
					QByteArray pi; pi.append(static_cast<char>(session->playerId & 0xFF)); pi.append(static_cast<char>(m_clients.size() & 0xFF));
					sendControlToSession(session, mgba::CM_PLAYERINFO, pi);
				}
			}
		} else if (type == mgba::CM_INPUT) {
			// CM_INPUT payload: [action:1][key:1]
			if (payload.size() >= 2) {
				uint8_t action = static_cast<uint8_t>(payload[0]);
				uint8_t key = static_cast<uint8_t>(payload[1]);
				if (session->handler) session->handler->handleInput(action, key);
			}
		} else if (type == mgba::CM_CONNECTION) {
			// ping/pong - ignore or reply
			sendControlToSession(session, mgba::CM_CONNECTION, QByteArray());
		}
	}
	socket->setProperty("recvBuffer", buf);
}

void WebMultiplayerServer::sendControlMessage(QTcpSocket* socket, uint8_t type, const QByteArray& payload) {
	if (!socket || socket->state() != QAbstractSocket::ConnectedState)
		return;
	mgba::ControlHeader hdr;
	hdr.type = type;
	hdr.len = htons((uint16_t) payload.size());
	QByteArray out;
	out.append(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
	if (!payload.isEmpty())
		out.append(payload);
	socket->write(out);
}

void WebMultiplayerServer::sendControlToSession(ClientSession* session, uint8_t type, const QByteArray& payload) {
	if (!session)
		return;
	if (session->worker) {
		// invoke worker's sendControlMessage in worker thread
		QMetaObject::invokeMethod(session->worker, "sendControlMessage", Qt::QueuedConnection, Q_ARG(uint8_t, type),
		                          Q_ARG(QByteArray, payload));
	} else if (session->socket) {
		sendControlMessage(session->socket, type, payload);
	}
}

void WebMultiplayerServer::handleSocketDisconnected() {
	QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
	if (!socket)
		return;
	ClientSession* session = nullptr;
	QString sessionId;
	for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
		if (it.value()->socket == socket) {
			session = it.value();
			sessionId = it.key();
			break;
		}
	}
	if (sessionId.isEmpty())
		return;
	int pid = session->playerId;
	cleanupEmulator(session);
	m_clients.remove(sessionId);
	if (pid >= 0 && pid < m_playerSlots.size()) {
		m_playerSlots[pid] = false;
	}
	if (session->socket)
		session->socket->deleteLater();
	delete session;
	qDebug() << "Client disconnected:" << sessionId;
	emit clientDisconnected(sessionId);
}

void WebMultiplayerServer::initializeEmulator(ClientSession* session) {
	qDebug() << "Initializing emulator for session:" << session->sessionId;
	qDebug() << "ROM path:" << m_romPath;

	// Use CoreManager to load the game
	session->coreController = m_coreManager->loadGame(m_romPath);
	if (!session->coreController) {
		qDebug() << "Failed to load game";
		return;
	}

	qDebug() << "CoreController created:" << (void*) session->coreController;

	// Create input controller and attach it to the session so we can delete it
	// when the session is cleaned up. InputController claims a player slot on
	// construction and must be freed when the session ends.
	InputController* input = new InputController(nullptr, this);
	session->inputController = input;
	if (m_configController)
		input->setConfiguration(m_configController);
	session->coreController->setInputController(input);
	session->coreController->loadConfig(m_configController);
	// CoreManager::loadGame already set the controller's path and base directory.
	// Do not call setPath here with only the ROM path because that overwrites
	// the base directory used for locating save files and causes new save
	// files to be created every run.
	if (session->coreController->thread() && session->coreController->thread()->core) {
		mCoreConfigSetIntValue(&session->coreController->thread()->core->config, "hwaccelVideo", 0);
		session->coreController->thread()->core->reloadConfigOption(session->coreController->thread()->core,
		                                                            "hwaccelVideo", NULL);
	}

	if (!session->handler)
		session->handler = new ClientSessionHandler(this, session, this);
	connect(session->coreController, &CoreController::started, session->handler, &ClientSessionHandler::onCoreStarted);
	connect(session->coreController, &CoreController::stopping, session->handler,
	        &ClientSessionHandler::onCoreStopping);
	connect(session->coreController, &CoreController::crashed, session->handler, &ClientSessionHandler::onCoreCrashed);
	connect(session->coreController, &CoreController::failed, session->handler, &ClientSessionHandler::onCoreFailed);
	connect(session->coreController, &CoreController::frameAvailable, session->handler,
	        &ClientSessionHandler::onFrameAvailable);

	qDebug() << "Emulator started";
	session->coreController->start();
	session->coreController->setSync(false);

	// Master/lockstep: only the master (player 0) advances frames; others stay paused
	if (session->playerId == 0) {
		if (!session->frameTimer) {
			session->frameTimer = new QTimer(this);
			session->frameTimer->setInterval(1000 / 60);
			if (!session->handler)
				session->handler = new ClientSessionHandler(this, session, this);
			connect(session->frameTimer, &QTimer::timeout, session->handler,
			        &ClientSessionHandler::onFrameTimerTimeout);
			session->frameTimer->start();
		}
		session->coreController->setPaused(true);
	} else {
		session->coreController->setPaused(false);
	}
}

// asyncInitializeEmulator and finalizeEmulator removed - initialization is now
// handled synchronously in initializeEmulator to keep the flow simple.

void WebMultiplayerServer::cleanupEmulator(ClientSession* session) {
	if (!session || !session->coreController) {
		return;
	}

	// Detach from multiplayer controller
	if (m_multiplayerController) {
		m_multiplayerController->detachGame(session->coreController);
	}

	// Stop and cleanup emulator
	session->coreController->stop();
	// Stop timers and delete session handler if present
	if (session->frameTimer) {
		session->frameTimer->stop();
	}
	if (session->handler) {
		delete session->handler;
		session->handler = nullptr;
	}
	if (session->frameTimer) {
		session->frameTimer->stop();
		delete session->frameTimer;
		session->frameTimer = nullptr;
	}
	delete session->coreController;
	session->coreController = nullptr;
	// Delete per-session InputController after the core is torn down so it can
	// properly free its claimed player slot. Some InputController cleanup may
	// reference the CoreController, so ensure the core is gone first.
	if (session->inputController) {
		delete session->inputController;
		session->inputController = nullptr;
	}
	// Cleanup RTP sink if present
	if (session->VideoSink) {
		delete session->VideoSink;
		session->VideoSink = nullptr;
	}
}

// Input handling moved to ClientSessionHandler::handleInput

void WebMultiplayerServer::handleConnectionMessage(ClientSession* session, const QByteArray& payload) {
	// Simple connection message processing; respond with CM_CONNECTION
	Q_UNUSED(payload);
	sendControlMessage(session->socket, mgba::CM_CONNECTION, QByteArray());
}

void WebMultiplayerServer::broadcastToClients(uint8_t type, const QByteArray& payload) {
	for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
		sendControlMessage(it.value()->socket, type, payload);
	}
}

void WebMultiplayerServer::sendVideoFrame(ClientSession* session) {
	if (!session->coreController || !session->socket) {
		return;
	}
	QByteArray frameData;
	if (session->handler) frameData = session->handler->encodeVideoFrame();
	if (frameData.isEmpty()) {
		return;
	}
	// Video is sent via RTP sink; do not send frame over TCP control channel.
	// Per-client streaming is handled by the RTP sink created at registration.
	(void) 0;
}

// Player info sending moved to ClientSessionHandler::sendPlayerInfo

QString WebMultiplayerServer::generateSessionId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Video encoding moved to ClientSessionHandler::encodeVideoFrame

void WebMultiplayerServer::sendVideoFrame(const QString& sessionId) {
	auto it = m_clients.find(sessionId);
	if (it != m_clients.end()) {
		sendVideoFrame(it.value());
	}
}

void WebMultiplayerServer::processInput(ClientSession* session, uint8_t action, uint8_t keyCode) {
	if (session && session->handler) session->handler->handleInput(action, keyCode);
}
