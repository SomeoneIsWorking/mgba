#pragma once

#include <QObject>
#include <cstdint>
#include <vector>
#include "WebMultiplayerServer.h"

struct mCore;
struct mAudioBuffer;

namespace QGBA {

class ClientSessionHandler : public QObject {
    Q_OBJECT
public:
    explicit ClientSessionHandler(WebMultiplayerServer* server, WebMultiplayerServer::ClientSession* session, QObject* parent = nullptr);
    ~ClientSessionHandler();

    void handleInput(uint8_t action, uint8_t keyCode);
    void sendConnectionAck();
    void sendPlayerInfo(int totalPlayers);
    QByteArray encodeVideoFrame();

public slots:
    void onCoreStarted();
    void onCoreStopping();
    void onCoreCrashed(const QString& msg);
    void onCoreFailed();
    void onFrameAvailable();
    void onFrameTimerTimeout();

private:
    void processVideoFrame();
    void processAudioFrame();
    unsigned int getCoreSampleRate(struct mCore* core);
    void sendAudioSamples(struct mAudioBuffer* buffer, unsigned int sampleRate);
    std::vector<int16_t> readAudioSamples(struct mAudioBuffer* buffer, size_t samplesNeeded, size_t toRead);

    WebMultiplayerServer* m_server;
    WebMultiplayerServer::ClientSession* m_session;
};

}
