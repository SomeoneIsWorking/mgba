#pragma once

#include <QString>
#include <QImage>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace QGBA {

class VideoSink {
public:
    VideoSink();
    ~VideoSink();

    // Open an RTP sink to rtp://<host>:<port>
    // Uses shared stream constants from StreamingCommon.h (240x160 YUV420P)
    bool open(const QString& url);
    void close();

    // Send a frame (QImage in any format convertible). Non-blocking.
    bool sendFrame(const QImage& img);

    // Send audio samples (16-bit PCM, interleaved)
    bool sendAudio(const std::vector<int16_t>& samples, int channels, int sampleRate);

    // Get SDP text (if created), empty if not available
    QString getSdp() const;

private:
    AVFormatContext* fmtCtx;
    AVStream* videoStream;
    AVStream* audioStream;
    AVCodecContext* videoCodecCtx;
    AVCodecContext* audioCodecCtx;
    SwsContext* swsCtx;
    SwrContext* swrCtx;
    AVFrame* videoFrameYUV;
    AVFrame* audioFrame;
    int videoFramePts;
    int audioFramePts;
    int width;
    int height;
    int fps;
    int audioSampleRate;
    int audioChannels;
    bool localClient; // true when client is localhost -> prefer lossless/low-latency
    QString m_sdp;
    
    // Audio buffering for AAC frame alignment
    std::vector<int16_t> audioBuffer;
    int bufferedSamples;

    // Helper functions for open()
    bool setupVideoCodec();
    bool setupAudioCodec();
    bool initializeResampler();
    bool openOutput(const QString& url);

    // Helper functions for sendFrame()
    bool convertImage(const QImage& img, std::vector<unsigned char>& bgraBuf);
    bool encodeVideoFrame(const std::vector<unsigned char>& bgraBuf);
    bool writeVideoPacket(AVPacket* pkt);

    // Helper functions for sendAudio()
    bool bufferAudio(const std::vector<int16_t>& samples, int channels, int sampleRate);
    bool reconfigureResampler(int sampleRate);
    bool resampleAudioFrame(std::vector<int16_t>& frameSamples);
    bool encodeAudioFrame();
    bool writeAudioPacket(AVPacket* pkt);
};

}

