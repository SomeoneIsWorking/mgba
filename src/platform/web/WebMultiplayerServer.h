/* Copyright (c) 2013-2023 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

#include "../qt/ConfigController.h"
#include "../qt/CoreController.h"
#include "../qt/CoreManager.h"
#include "../qt/MultiplayerController.h"
#include "WebServerGui.h"

#include <mgba/core/core.h>
#include <mgba/gba/interface.h>

namespace QGBA {

class WebServerGui; // Move forward declaration to be global
} // Closing the QGBA namespace
namespace QGBA {
class WebMultiplayerServer : public QObject {
	Q_OBJECT

public:
	struct ClientSession {
		QTcpSocket* socket; // TCP control socket for this client (JSON messages)
		class ClientSocketWorker* worker;
		CoreController* coreController;
		// Owned InputController for this session (if any). Must be deleted when
		// the session's emulator is cleaned up so that InputController::~InputController
		// can free its claimed player slot.
		class InputController* inputController;
		// VideoSink (RTP) is used for streaming encoded video
		int playerId;
		bool connected;
		QString sessionId;
		bool primed;
		QTimer* frameTimer;
		bool attachedToMultiplayer;
		qint64 lastFrameSentMs;
		qint64 lastDropLogMs;
		qint64 lastAudioLogMs;
		bool sendInProgress;
		QHostAddress udpAddress;
		quint16 udpPort;
		quint32 nextFrameId;
		class VideoSink* VideoSink;
	QTimer* audioTimer;
		QThread* workerThread;
		class ClientSessionHandler* handler;
		ClientSession()
		    : socket(nullptr)
		    , coreController(nullptr)
			, inputController(nullptr)
		    , playerId(-1)
		    , connected(false)
		    , frameTimer(nullptr)
		    , attachedToMultiplayer(false)
		    , lastFrameSentMs(0)
		    , lastDropLogMs(0)
		    , lastAudioLogMs(0)
		    , sendInProgress(false)
		    , udpPort(0)
		    , nextFrameId(1)
			, VideoSink(nullptr)
			, audioTimer(nullptr)
			, workerThread(nullptr)
			, handler(nullptr) {}
	};

	explicit WebMultiplayerServer(ConfigController* config);
	~WebMultiplayerServer();

	bool startServer();
	void stopServer();
	bool isRunning() const;
	void setWebRoot(const QString& webRoot);
	void setRomPath(const QString& romPath);

signals:
	void clientConnected(const QString& sessionId);
	void clientDisconnected(const QString& sessionId);
	void serverStarted();
	void serverStopped();
	void errorOccurred(const QString& error);

public slots:
	void onNewConnection();
	void onIncomingConnectionDescriptor(qintptr socketDescriptor);
	// Per-socket handlers (connected to each client's QTcpSocket)
	void handleSocketReadyRead();
	void handleSocketDisconnected();

	// Public wrapper to process input from session handlers
	void processInput(ClientSession* session, uint8_t action, uint8_t keyCode);

private slots:
	void sendVideoFrame(const QString& sessionId);
	// audio sending moved to ClientSessionHandler::onAudioTimerTimeout

private:
    	friend class ServerSessionManager;
	void initializeEmulator(ClientSession* session);
	void cleanupEmulator(ClientSession* session);
	// (implementation provided in .cpp)
	void handleConnectionMessage(ClientSession* session, const QByteArray& payload);
	void sendControlMessage(QTcpSocket* socket, uint8_t type, const QByteArray& payload);
	void broadcastToClients(uint8_t type, const QByteArray& payload);
	void sendControlToSession(ClientSession* session, uint8_t type, const QByteArray& payload);
	void sendVideoFrame(ClientSession* session);
	// Input handling moved to ClientSessionHandler
	QString generateSessionId();

	QTcpServer* m_controlServer; // TCP server for control connections (JSON messages)
	QUdpSocket* m_streamSocket; // UDP socket used to send video/audio packets to clients
	int m_streamPort;
	int m_controlPort;
	MultiplayerController* m_multiplayerController;
	CoreManager* m_coreManager;
	ConfigController* m_configController;
	class WebBroadcaster* m_broadcaster;
	QMap<QString, ClientSession*> m_clients;
	// initialization is done inline in initializeEmulator
	QString m_romPath;
	QString m_webRoot;
	int m_maxClients;
	bool m_serverRunning;
	int m_nextPlayerId;
	QVector<bool> m_playerSlots;

	WebServerGui* m_serverGui{nullptr};
	class ServerSessionManager* m_sessionManager{nullptr};
};

}