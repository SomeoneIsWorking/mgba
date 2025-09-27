#include "ClientSessionHandler.h"
#include "WebMultiplayerServer.h"
#include "VideoSink.h"
#include "StreamingCommon.h"
#include "ClientSocketWorker.h"
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/audio-buffer.h>
#include <mgba/internal/gba/gba.h>
#include <QDebug>
#include <vector>

using namespace QGBA;

ClientSessionHandler::ClientSessionHandler(WebMultiplayerServer* server, WebMultiplayerServer::ClientSession* session, QObject* parent)
    : QObject(parent), m_server(server), m_session(session) {
}

ClientSessionHandler::~ClientSessionHandler() {
}

void ClientSessionHandler::handleInput(uint8_t action, uint8_t keyCode) {
    if (!m_session || !m_session->coreController) return;

    int mgbaKey = -1;
#ifdef M_CORE_GBA
    switch (keyCode) {
    case mgba::KEY_UP:
        mgbaKey = GBA_KEY_UP;
        break;
    case mgba::KEY_DOWN:
        mgbaKey = GBA_KEY_DOWN;
        break;
    case mgba::KEY_LEFT:
        mgbaKey = GBA_KEY_LEFT;
        break;
    case mgba::KEY_RIGHT:
        mgbaKey = GBA_KEY_RIGHT;
        break;
    case mgba::KEY_Z:
        mgbaKey = GBA_KEY_A;
        break;
    case mgba::KEY_X:
        mgbaKey = GBA_KEY_B;
        break;
    case mgba::KEY_A:
        mgbaKey = GBA_KEY_L;
        break;
    case mgba::KEY_S:
        mgbaKey = GBA_KEY_R;
        break;
    case mgba::KEY_ENTER:
        mgbaKey = GBA_KEY_START;
        break;
    case mgba::KEY_TAB:
        mgbaKey = GBA_KEY_SELECT;
        break;
    default:
        mgbaKey = -1;
        break;
    }
#endif

    if (mgbaKey == -1) return;

    if (action == mgba::ACTION_PRESS) {
        m_session->coreController->addKey(mgbaKey);
    } else if (action == mgba::ACTION_RELEASE) {
        m_session->coreController->clearKey(mgbaKey);
    }
}

void ClientSessionHandler::sendConnectionAck() {
    if (!m_session || !m_session->worker) return;
    m_session->worker->sendControlMessage(mgba::CM_CONNECTION, QByteArray());
}

void ClientSessionHandler::sendPlayerInfo(int totalPlayers) {
    if (!m_session || !m_session->worker) return;
    QByteArray pi;
    pi.append(static_cast<char>(m_session->playerId & 0xFF));
    pi.append(static_cast<char>(totalPlayers & 0xFF));
    m_session->worker->sendControlMessage(mgba::CM_PLAYERINFO, pi);
}

QByteArray ClientSessionHandler::encodeVideoFrame() {
    if (!m_session || !m_session->coreController) return QByteArray();
    QImage image = m_session->coreController->getPixels();
    if (image.isNull()) return QByteArray();
    if (image.format() != QImage::Format_RGB32) image = image.convertToFormat(QImage::Format_RGB32);
    QImage out = image.convertToFormat(QImage::Format_RGBA8888);
    if (out.isNull()) return QByteArray();
    int w = out.width();
    int h = out.height();
    int bpp = 4;
    QByteArray data;
    data.append("MGBI");
    uint32_t nw = htonl(static_cast<uint32_t>(w));
    uint32_t nh = htonl(static_cast<uint32_t>(h));
    uint32_t nb = htonl(static_cast<uint32_t>(bpp));
    data.append(reinterpret_cast<const char*>(&nw), sizeof(nw));
    data.append(reinterpret_cast<const char*>(&nh), sizeof(nh));
    data.append(reinterpret_cast<const char*>(&nb), sizeof(nb));
    const uchar* bits = out.constBits();
    int bytes = out.bytesPerLine() * out.height();
    data.append(reinterpret_cast<const char*>(bits), bytes);
    return data;
}

void ClientSessionHandler::onCoreStarted() {
    qDebug() << "signal started for session" << (m_session ? m_session->sessionId : QString());
}

void ClientSessionHandler::onCoreStopping() {
    qDebug() << "signal stopping for session" << (m_session ? m_session->sessionId : QString());
}

void ClientSessionHandler::onCoreCrashed(const QString& msg) {
    qDebug() << "signal crashed for session" << (m_session ? m_session->sessionId : QString()) << msg;
}

void ClientSessionHandler::onCoreFailed() {
    qDebug() << "signal failed for session" << (m_session ? m_session->sessionId : QString());
}

void ClientSessionHandler::onFrameAvailable() {
    if (!m_session) return;
    
    if (!m_session->VideoSink || !m_session->coreController) {
        qDebug() << "No RTP sink for session" << m_session->sessionId << "- dropping frame";
        return;
    }

    processVideoFrame();
    processAudioFrame();
}

void ClientSessionHandler::processVideoFrame() {
    QImage img = m_session->coreController->getPixels();
    if (img.isNull()) return;
    
    bool ok = m_session->VideoSink->sendFrame(img);
    if (!ok) {
        qDebug() << "VideoSink: failed to send frame for session" << m_session->sessionId;
    }
}

void ClientSessionHandler::processAudioFrame() {
    mCoreThread* thread = m_session->coreController->thread();
    if (!thread || !thread->core) return;
    
    struct mCore* core = thread->core;
    struct mAudioBuffer* buffer = core->getAudioBuffer(core);
    if (!buffer) return;
    
    size_t avail = mAudioBufferAvailable(buffer);
    if (avail == 0) return;
    
    unsigned int coreSampleRate = getCoreSampleRate(core);
    if (coreSampleRate == 0) {
        qDebug() << "ClientSessionHandler: no sample rate available from core, skipping audio";
        return;
    }
    
    sendAudioSamples(buffer, coreSampleRate);
}

unsigned int ClientSessionHandler::getCoreSampleRate(struct mCore* core) {
    if (!core->audioSampleRate) return 0;
    return core->audioSampleRate(core);
}

void ClientSessionHandler::sendAudioSamples(struct mAudioBuffer* buffer, unsigned int sampleRate) {
    size_t avail = mAudioBufferAvailable(buffer);
    if (avail == 0) {
        qDebug() << "ClientSessionHandler: no audio samples available";
        return;
    }

    // Read all available samples from the core
    size_t samplesToRead = avail;
    size_t framesToRead = samplesToRead / buffer->channels;
    
    std::vector<int16_t> samples(samplesToRead, 0);
    size_t produced = mAudioBufferRead(buffer, samples.data(), framesToRead);
    
    if (produced == 0) {
        qDebug() << "ClientSessionHandler: failed to read audio samples";
        return;
    }
    
    // Convert back to total samples read
    size_t totalSamplesRead = produced * buffer->channels;
    samples.resize(totalSamplesRead); // Trim to actual size
    
    bool audioOk = m_session->VideoSink->sendAudio(samples, buffer->channels, sampleRate);
    if (!audioOk) {
        qDebug() << "VideoSink: failed to send audio for session" << m_session->sessionId;
    }
}

std::vector<int16_t> ClientSessionHandler::readAudioSamples(struct mAudioBuffer* buffer, size_t samplesNeeded, size_t toRead) {
    std::vector<int16_t> samples(samplesNeeded, 0);
    size_t produced = mAudioBufferRead(buffer, samples.data(), toRead / buffer->channels);
    
    if (produced == 0) return {};
    
    // Convert back to total samples read and zero-pad if needed
    size_t totalSamplesRead = produced * buffer->channels;
    if (totalSamplesRead < samplesNeeded) {
        std::fill(samples.begin() + totalSamplesRead, samples.end(), 0);
    }
    
    return samples;
}

void ClientSessionHandler::onFrameTimerTimeout() {
    if (m_session && m_session->coreController) m_session->coreController->frameAdvance();
}
