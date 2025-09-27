/* Copyright (c) 2013-2023 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "WebMultiplayerServer.h"
#include "../qt/ConfigController.h"
#include "../qt/LogController.h"
#include "WebServerGui.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QTimer>

#include <mgba-util/socket.h>

using namespace QGBA;

int main(int argc, char* argv[]) {
    // Use QApplication since we need GUI components for Qt platform
    QApplication app(argc, argv);
    app.setApplicationName("mGBA Web Multiplayer Server");
    app.setApplicationVersion("1.0.0");

    // Initialize mGBA subsystems (same as GBAApp)
    SocketSubsystemInit();
    qRegisterMetaType<const uint32_t*>("const uint32_t*");
    qRegisterMetaType<mCoreThread*>("mCoreThread*");

    // Initialize logging
    LogController::installMessageHandler();

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("mGBA Web Multiplayer Server - Hosts multiplayer GBA games via WebSocket");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption romOption(QStringList() << "r" << "rom",
                                 "Path to GBA ROM file",
                                 "rom", "kirby.gba");
    parser.addOption(romOption);

    QCommandLineOption maxClientsOption(QStringList() << "m" << "max-clients",
                                        "Maximum number of clients (1-4)",
                                        "max", "4");
    parser.addOption(maxClientsOption);

    parser.process(app);

    // Get command line values
    QString romPath = parser.value(romOption);
    int maxClients = parser.value(maxClientsOption).toInt();

    if (maxClients < 1 || maxClients > 4) {
        qDebug() << "Invalid max clients:" << maxClients << "(must be 1-4)";
        return 1;
    }

    // Check if ROM file exists
    QDir currentDir = QDir::current();
    QString fullRomPath = currentDir.absoluteFilePath(romPath);
    if (!QFile::exists(fullRomPath)) {
        qDebug() << "ROM file not found:" << fullRomPath;
        qDebug() << "Current directory:" << currentDir.absolutePath();
        return 1;
    }

    qDebug() << "Starting mGBA Web Multiplayer Server";
    qDebug() << "ROM:" << fullRomPath;
    qDebug() << "Max clients:" << maxClients;

    // Initialize configuration
    ConfigController config;
    config.setOption("logLevel", static_cast<int>(mLOG_INFO));

    // Load logging configuration (same as GBAApp)
    LogController::global()->load(&config);

    // Create and start server
    WebMultiplayerServer server(&config);
    server.setRomPath(fullRomPath);
    // The server now creates and shows an integrated GUI when started.
    
    QObject::connect(&server, &WebMultiplayerServer::serverStarted, []() {
        qDebug() << "Server started successfully";
    });

    QObject::connect(&server, &WebMultiplayerServer::serverStopped, []() {
        qDebug() << "Server stopped";
        QCoreApplication::quit();
    });

    QObject::connect(&server, &WebMultiplayerServer::errorOccurred, [](const QString& error) {
        qDebug() << "Server error:" << error;
        QCoreApplication::quit();
    });

    QObject::connect(&server, &WebMultiplayerServer::clientConnected, [](const QString& sessionId) {
        qDebug() << "Client connected:" << sessionId;
    });

    QObject::connect(&server, &WebMultiplayerServer::clientDisconnected, [](const QString& sessionId) {
        qDebug() << "Client disconnected:" << sessionId;
    });

    if (!server.startServer()) {
        qDebug() << "Failed to start server";
        return 1;
    }

    // Handle Ctrl+C gracefully
    QObject::connect(&app, &QApplication::aboutToQuit, [&server]() {
        qDebug() << "Shutting down server...";
        server.stopServer();
    });

    qDebug() << "Press Ctrl+C to stop the server";
    return app.exec();
}