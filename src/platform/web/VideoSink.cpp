#include "VideoSink.h"
#include "StreamingCommon.h"
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QPainter>
#include <QUrl>
#include "LocalClientDetector.h"

using namespace QGBA;

VideoSink::VideoSink()
    : fmtCtx(nullptr)
    , videoStream(nullptr)
    , audioStream(nullptr)
    , videoCodecCtx(nullptr)
    , audioCodecCtx(nullptr)
    , swsCtx(nullptr)
    , swrCtx(nullptr)
    , videoFrameYUV(nullptr)
    , audioFrame(nullptr)
    , videoFramePts(0)
    , audioFramePts(0)
    , width(0)
    , height(0)
    , fps(60)
	, audioSampleRate(0)
	, audioChannels(mgba::STREAM_AUDIO_CHANNELS)
	, localClient(false)
	, audioBuffer()
	, bufferedSamples(0)
	 { }



VideoSink::~VideoSink() {
	close();
}

// Helper functions for open()
bool VideoSink::setupVideoCodec() {
	const AVCodec* videoCodec = avcodec_find_encoder_by_name("libx264");
	if (!videoCodec)
		videoCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!videoCodec) {
		qDebug() << "VideoSink: H.264 encoder not found";
		return false;
	}

	videoStream = avformat_new_stream(fmtCtx, nullptr);
	if (!videoStream) {
		qDebug() << "VideoSink: failed to create video stream";
		return false;
	}

	videoCodecCtx = avcodec_alloc_context3(videoCodec);
	if (!videoCodecCtx) {
		return false;
	}
	videoCodecCtx->codec_id = videoCodec->id;
	videoCodecCtx->width = width;
	videoCodecCtx->height = height;
	videoCodecCtx->time_base = AVRational { 1, fps };
	videoStream->time_base = videoCodecCtx->time_base;
	// Choose codec pixel format. For local clients request YUV444 (no chroma subsampling)
	if (localClient) {
		videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV444P;
		qDebug() << "VideoSink: requesting codec pix_fmt YUV444P for lossless local streaming";
		av_opt_set(videoCodecCtx->priv_data, "yv12", "0", 0);
	} else {
		videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
		av_opt_set(videoCodecCtx->priv_data, "yv12", "0", 0);
	}

	// Set color metadata: emulator frames are full-range (0-255).
	videoCodecCtx->color_range = mgba::STREAM_COLOR_RANGE;
	// Use BT.601 / SMPTE170M for classic GBA content
	videoCodecCtx->colorspace = mgba::STREAM_COLORSPACE;
	videoCodecCtx->color_primaries = mgba::STREAM_COLOR_PRIMARIES;
	videoCodecCtx->color_trc = mgba::STREAM_COLOR_TRC; // use sRGB-ish transfer

	// low-latency settings (default)
	av_opt_set(videoCodecCtx->priv_data, "preset", "ultrafast", 0);
	av_opt_set(videoCodecCtx->priv_data, "tune", "zerolatency", 0);
	videoCodecCtx->gop_size = fps; // keyframe interval
	videoCodecCtx->max_b_frames = 0;

	// If this is a local client (localhost), prefer lossless and more extreme low-latency
	if (localClient) {
		qDebug() << "VideoSink: enabling local lossless/low-latency encoder settings";
		// Attempt lossless: CRF 0 and constant quality if encoder supports it
		av_opt_set(videoCodecCtx->priv_data, "crf", "0", 0);
		// reduce lookahead and threads to lower latency
		av_opt_set(videoCodecCtx->priv_data, "rc-lookahead", "0", 0);
		av_opt_set(videoCodecCtx->priv_data, "threads", "1", 0);
		// ultrafast + zerolatency already set; ensure no B-frames
		videoCodecCtx->max_b_frames = 0;
		// Try to request highest chroma fidelity if supported
		av_opt_set(videoCodecCtx->priv_data, "profile", "high444", 0);
		// Ensure we don't constrain bitrate — allow large bitrate for lossless
		videoCodecCtx->bit_rate = 0;
		// Some encoders accept 'qp' or 'qpmin'/'qpmax' — force zero quantizer if supported
		av_opt_set(videoCodecCtx->priv_data, "qp", "0", 0);
		// Report what we attempted to set
		qDebug() << "VideoSink: encoder options -> crf=0, qp=0, rc-lookahead=0, threads=1, profile=high444";
	}

	if (fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
		videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	if (avcodec_open2(videoCodecCtx, videoCodec, nullptr) < 0) {
		qDebug() << "VideoSink: failed to open video codec";
		return false;
	}
	// Log if encoder didn't accept our requested pixel format
	if (videoCodecCtx->pix_fmt != AV_PIX_FMT_YUV420P && videoCodecCtx->pix_fmt != AV_PIX_FMT_YUV444P) {
		qDebug() << "VideoSink: warning - codec opened with pix_fmt" << videoCodecCtx->pix_fmt << "(requested might not be supported)";
	}
	avcodec_parameters_from_context(videoStream->codecpar, videoCodecCtx);
	return true;
}

bool VideoSink::setupAudioCodec() {
	const AVCodec* audioCodec = avcodec_find_encoder_by_name("aac");
	if (!audioCodec)
		audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!audioCodec) {
		qDebug() << "VideoSink: AAC encoder not found";
		return false;
	}

	audioStream = avformat_new_stream(fmtCtx, nullptr);
	if (!audioStream) {
		qDebug() << "VideoSink: failed to create audio stream";
		return false;
	}

	audioCodecCtx = avcodec_alloc_context3(audioCodec);
	if (!audioCodecCtx) {
		return false;
	}
	audioCodecCtx->codec_id = audioCodec->id;
	audioCodecCtx->sample_rate = mgba::STREAM_AUDIO_SAMPLE_RATE_CODEC; // AAC codec sample rate
	audioCodecCtx->time_base = AVRational { 1, mgba::STREAM_AUDIO_SAMPLE_RATE_CODEC };
	audioStream->time_base = audioCodecCtx->time_base;

	audioCodecCtx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	audioCodecCtx->ch_layout.nb_channels = mgba::STREAM_AUDIO_CHANNELS;

	audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP; // AAC requires planar float format

	if (fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
		audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	if (avcodec_open2(audioCodecCtx, audioCodec, nullptr) < 0) {
		qDebug() << "VideoSink: failed to open audio codec";
		return false;
	}

	avcodec_parameters_from_context(audioStream->codecpar, audioCodecCtx);
	return true;
}

bool VideoSink::openOutput(const QString& url) {
	AVDictionary* opts = nullptr;
	// for RTP we want payload type mapping, but keep defaults
	// When serving to a local client prefer minimum buffering and immediate flushing
	if (localClient) {
		qDebug() << "VideoSink: enabling local muxer options (nobuffer, flush_packets, max_delay=0)";
		av_dict_set(&opts, "fflags", "nobuffer", 0);
		av_dict_set(&opts, "flush_packets", "1", 0);
		av_dict_set(&opts, "max_delay", "0", 0);
	}

	if (avio_open(&fmtCtx->pb, url.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
		qDebug() << "VideoSink: avio_open failed for" << url;
		av_dict_free(&opts);
		return false;
	}

	if (avformat_write_header(fmtCtx, &opts) < 0) {
		qDebug() << "VideoSink: avformat_write_header failed";
		av_dict_free(&opts);
		return false;
	}

	// Explicitly set max_delay on the context as an extra safety for low-latency local streaming
	if (localClient) {
		fmtCtx->max_delay = 0;
		qDebug() << "VideoSink: fmtCtx->max_delay set to 0 for local client";
	}

	// Generate SDP for this RTP session so clients can open the correct stream
	const int SDP_BUFSIZE = 8192;
	char sdpBuf[SDP_BUFSIZE];
	AVFormatContext* arr[1];
	arr[0] = fmtCtx;
	if (av_sdp_create(arr, 1, sdpBuf, SDP_BUFSIZE) >= 0) {
		m_sdp = QString::fromUtf8(sdpBuf);
		qDebug() << "VideoSink: generated SDP:\n" << m_sdp;
	} else {
		qDebug() << "VideoSink: failed to generate SDP";
	}
	return true;
}

bool VideoSink::initializeResampler() {
	// Prepare video frame + sws
	videoFrameYUV = av_frame_alloc();
	videoFrameYUV->format = videoCodecCtx->pix_fmt;
	videoFrameYUV->width = width;
	videoFrameYUV->height = height;
	av_frame_get_buffer(videoFrameYUV, 32);

	const AVPixelFormat srcFmt = AV_PIX_FMT_BGR24;
	const AVPixelFormat dstFmt = static_cast<AVPixelFormat>(videoCodecCtx->pix_fmt);
	int swsFlags = SWS_BILINEAR;
	if (localClient)
		swsFlags = SWS_BICUBIC; // better resampling quality for lossless local
	swsCtx = sws_getContext(width, height, srcFmt, width, height, dstFmt,
							swsFlags, nullptr, nullptr, nullptr);
	if (!swsCtx) {
		qDebug() << "VideoSink: sws_getContext failed";
		return false;
	}

	// Prepare audio frame and resampler
	audioFrame = av_frame_alloc();
	audioFrame->format = audioCodecCtx->sample_fmt;
	audioFrame->nb_samples = audioCodecCtx->frame_size;
	audioFrame->sample_rate = audioCodecCtx->sample_rate;

	audioFrame->ch_layout = audioCodecCtx->ch_layout;

	av_frame_get_buffer(audioFrame, 0);

	// Create resampler for converting from S16 to FLTP
	// Initialize with a reasonable default, will be reconfigured on first audio data
	swrCtx = swr_alloc();
	if (!swrCtx) {
		qDebug() << "VideoSink: swr_alloc failed";
		return false;
	}

	AVChannelLayout in_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	swr_alloc_set_opts2(&swrCtx, &audioCodecCtx->ch_layout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate,
	                    &in_ch_layout, AV_SAMPLE_FMT_S16, 44100, 0, nullptr); // Use 44100 as initial guess

	if (swr_init(swrCtx) < 0) {
		qDebug() << "VideoSink: swr_init failed";
		return false;
	}
	return true;
}

// Helper functions for sendFrame()
bool VideoSink::convertImage(const QImage& img, std::vector<unsigned char>& bufferBGR24) {
	QImage conv = img.convertToFormat(QImage::Format_BGR888);
	int bufSize = conv.sizeInBytes();
	bufferBGR24.resize(bufSize);
	memcpy(bufferBGR24.data(), conv.constBits(), bufSize);
	return true;
}

bool VideoSink::encodeVideoFrame(const std::vector<unsigned char>& bufferBGR24) {
	const uint8_t* srcData[1] = { bufferBGR24.data() };
	int srcLinesize[1] = { width * 3 };

	// Mark the frame color metadata to match the encoder settings (BT.601 / full-range)
	videoFrameYUV->format = videoCodecCtx->pix_fmt;
	videoFrameYUV->color_range = mgba::STREAM_COLOR_RANGE;
	videoFrameYUV->colorspace = mgba::STREAM_COLORSPACE;
	videoFrameYUV->color_primaries = mgba::STREAM_COLOR_PRIMARIES;
	videoFrameYUV->color_trc = mgba::STREAM_COLOR_TRC;

	// Convert to YUV420P into frameYUV
	sws_scale(swsCtx, srcData, srcLinesize, 0, height, videoFrameYUV->data, videoFrameYUV->linesize);

	videoFrameYUV->pts = videoFramePts++;

	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = nullptr;
	pkt.size = 0;

	int ret = avcodec_send_frame(videoCodecCtx, videoFrameYUV);
	if (ret < 0) {
		qDebug() << "VideoSink: avcodec_send_frame failed" << ret;
		return false;
	}
	while (ret >= 0) {
		ret = avcodec_receive_packet(videoCodecCtx, &pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		if (ret < 0) {
			qDebug() << "VideoSink: avcodec_receive_packet error" << ret;
			return false;
		}
		if (!writeVideoPacket(&pkt)) {
			av_packet_unref(&pkt);
			return false;
		}
		av_packet_unref(&pkt);
	}
	return true;
}

bool VideoSink::writeVideoPacket(AVPacket* pkt) {
	pkt->stream_index = videoStream->index;
	// Ensure PTS is in stream timebase
	pkt->pts = av_rescale_q(pkt->pts, videoCodecCtx->time_base, videoStream->time_base);
	pkt->dts = pkt->pts;
	int ret = av_interleaved_write_frame(fmtCtx, pkt);
	if (ret < 0) {
		qDebug() << "VideoSink: av_interleaved_write_frame failed" << ret;
		return false;
	}
	return true;
}

bool VideoSink::open(const QString& url) {
	close();
	// Use shared constants for dimensions and fps
	width = mgba::STREAM_WIDTH;
	height = mgba::STREAM_HEIGHT;
	fps = mgba::STREAM_FPS;
	audioChannels = mgba::STREAM_AUDIO_CHANNELS;

	// Detect localhost or same-machine clients: if URL host is loopback, empty (e.g., rtp://:1234)
	// or matches an IP assigned to any local network interface, treat as localClient.
	QUrl qurl(url);
	QString reason;
	localClient = LocalClientDetector::isLocal(qurl, &reason);
	qDebug() << "VideoSink::open() url=" << url << " host=" << qurl.host() << " localClient=" << localClient << " reason=" << reason;

	avformat_network_init();

	const char* ofmt = "mpegts";
	int ret = avformat_alloc_output_context2(&fmtCtx, nullptr, ofmt, url.toUtf8().constData());
	if (ret < 0 || !fmtCtx) {
		qDebug() << "VideoSink: failed to alloc output context for" << url << "err=" << ret;
		close();
		return false;
	}

	if (!setupVideoCodec()) {
		close();
		return false;
	}

	if (!setupAudioCodec()) {
		close();
		return false;
	}

	if (!openOutput(url)) {
		close();
		return false;
	}

	if (!initializeResampler()) {
		close();
		return false;
	}

	videoFramePts = 0;
	audioFramePts = 0;
	qDebug() << "VideoSink: opened" << url << "w=" << width << "h=" << height << "fps=" << fps
	         << "audio=" << audioSampleRate << "Hz" << audioChannels << "ch";
	return true;
}

void VideoSink::close() {
	if (fmtCtx) {
		if (fmtCtx->pb) {
			av_write_trailer(fmtCtx);
			avio_closep(&fmtCtx->pb);
		}
	}
	if (swsCtx) {
		sws_freeContext(swsCtx);
		swsCtx = nullptr;
	}
	if (swrCtx) {
		swr_free(&swrCtx);
		swrCtx = nullptr;
	}
	if (videoFrameYUV) {
		av_frame_free(&videoFrameYUV);
		videoFrameYUV = nullptr;
	}
	if (audioFrame) {
		av_frame_free(&audioFrame);
		audioFrame = nullptr;
	}
	if (videoCodecCtx) {
		avcodec_free_context(&videoCodecCtx);
		videoCodecCtx = nullptr;
	}
	if (audioCodecCtx) {
		avcodec_free_context(&audioCodecCtx);
		audioCodecCtx = nullptr;
	}
	if (fmtCtx) {
		avformat_free_context(fmtCtx);
		fmtCtx = nullptr;
	}

	// Clear audio buffer
	audioBuffer.clear();
	bufferedSamples = 0;
}

bool VideoSink::sendFrame(const QImage& img) {
	if (!fmtCtx || !videoCodecCtx || !videoFrameYUV)
		return false;
	if (img.isNull())
		return false;

	std::vector<unsigned char> bufferBGR24;
	if (!convertImage(img, bufferBGR24))
		return false;
	if (!encodeVideoFrame(bufferBGR24))
		return false;
	return true;
}

// Helper functions for sendAudio()
bool VideoSink::bufferAudio(const std::vector<int16_t>& samples, int channels, int sampleRate) {
	if (sampleRate != audioSampleRate) {
		if (!reconfigureResampler(sampleRate)) {
			return false;
		}
	}

	audioBuffer.insert(audioBuffer.end(), samples.begin(), samples.end());
	bufferedSamples += samples.size();

	int requiredSamples = audioCodecCtx->frame_size * channels;
	int samplesToProcess = std::min(bufferedSamples, requiredSamples * 4);

	if (samplesToProcess < requiredSamples / 2) {
		return false; // Keep buffering
	}
	return true;
}

bool VideoSink::reconfigureResampler(int sampleRate) {
	qDebug() << "VideoSink: core sample rate changed from" << audioSampleRate << "to" << sampleRate;
	audioSampleRate = sampleRate;
	swr_free(&swrCtx);
	swrCtx = swr_alloc();
	if (!swrCtx) {
		qDebug() << "VideoSink: swr_alloc failed during rate change";
		return false;
	}

	AVChannelLayout in_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	swr_alloc_set_opts2(&swrCtx, &out_ch_layout, audioCodecCtx->sample_fmt, mgba::STREAM_AUDIO_SAMPLE_RATE_CODEC,
	                    &in_ch_layout, AV_SAMPLE_FMT_S16, audioSampleRate, 0, nullptr);

	if (swr_init(swrCtx) < 0) {
		qDebug() << "VideoSink: swr_init failed during rate change";
		return false;
	}
	qDebug() << "VideoSink: resampler reconfigured for core rate" << audioSampleRate << "-> codec rate"
	         << mgba::STREAM_AUDIO_SAMPLE_RATE_CODEC;
	return true;
}

bool VideoSink::resampleAudioFrame(std::vector<int16_t>& frameSamples) {
	const int outSamples = audioCodecCtx->frame_size;
	const int outChannels = audioCodecCtx->ch_layout.nb_channels;

	// If input and codec rates match and codec expects planar float, do a direct format conversion
	if (audioSampleRate == audioCodecCtx->sample_rate && audioCodecCtx->sample_fmt == AV_SAMPLE_FMT_FLTP) {
		int requiredInSamples = outSamples * outChannels;
		if (bufferedSamples < requiredInSamples)
			return false;

		// Pull exactly the number of interleaved S16 input samples we need
		std::vector<int16_t> inSamplesVec(audioBuffer.begin(), audioBuffer.begin() + requiredInSamples);
		audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + requiredInSamples);
		bufferedSamples -= requiredInSamples;

		av_frame_make_writable(audioFrame);

		audioFrame->nb_samples = outSamples;
		audioFrame->pts = audioFramePts;
		// Advance pts by the number of input samples consumed (which equals outSamples here)
		audioFramePts += av_rescale_q(outSamples, av_make_q(1, audioSampleRate), audioCodecCtx->time_base);
		return true;
	}

	// General case: manual linear interpolation resampler from input sample rate -> codec sample rate
	// Compute how many input samples we need to produce `outSamples` output samples
	double ratio = static_cast<double>(audioSampleRate) / static_cast<double>(audioCodecCtx->sample_rate);
	int inSamplesNeeded = static_cast<int>(std::ceil(outSamples * ratio)) + 1; // +1 for interpolation access
	int requiredInSamples = inSamplesNeeded * audioChannels;
	if (bufferedSamples < requiredInSamples)
		return false;

	// Collect input samples (interleaved S16)
	std::vector<int16_t> inBuf(audioBuffer.begin(), audioBuffer.begin() + requiredInSamples);
	audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + requiredInSamples);
	bufferedSamples -= requiredInSamples;

	av_frame_make_writable(audioFrame);

	// For each output sample, perform linear interpolation per channel and write planar float
	for (int ch = 0; ch < outChannels; ++ch) {
		float* dst = reinterpret_cast<float*>(audioFrame->data[ch]);
		for (int out = 0; out < outSamples; ++out) {
			double inPos = out * ratio; // floating point position in input samples
			int idx = static_cast<int>(std::floor(inPos));
			double frac = inPos - idx;
			int idx0 = idx;
			int idx1 = std::min(idx + 1, inSamplesNeeded - 1);
			int16_t s0 = inBuf[idx0 * audioChannels + ch];
			int16_t s1 = inBuf[idx1 * audioChannels + ch];
			double sample = (1.0 - frac) * static_cast<double>(s0) + frac * static_cast<double>(s1);
			dst[out] = static_cast<float>(sample / 32768.0);
		}
	}

	audioFrame->nb_samples = outSamples;
	audioFrame->pts = audioFramePts;
	// Advance pts by the number of input samples consumed
	audioFramePts += av_rescale_q(inSamplesNeeded, av_make_q(1, audioSampleRate), audioCodecCtx->time_base);
	return true;
}

bool VideoSink::encodeAudioFrame() {
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = nullptr;
	pkt.size = 0;

	int ret = avcodec_send_frame(audioCodecCtx, audioFrame);
	if (ret < 0) {
		qDebug() << "VideoSink: avcodec_send_frame (audio) failed" << ret;
		return false;
	}
	while (ret >= 0) {
		ret = avcodec_receive_packet(audioCodecCtx, &pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		if (ret < 0) {
			qDebug() << "VideoSink: avcodec_receive_packet (audio) error" << ret;
			return false;
		}
		if (!writeAudioPacket(&pkt)) {
			av_packet_unref(&pkt);
			return false;
		}
		av_packet_unref(&pkt);
	}
	return true;
}

bool VideoSink::writeAudioPacket(AVPacket* pkt) {
	pkt->stream_index = audioStream->index;
	pkt->pts = av_rescale_q(pkt->pts, audioCodecCtx->time_base, audioStream->time_base);
	pkt->dts = pkt->pts;
	int ret = av_interleaved_write_frame(fmtCtx, pkt);
	if (ret < 0) {
		qDebug() << "VideoSink: av_interleaved_write_frame (audio) failed" << ret;
		return false;
	}
	return true;
}

bool VideoSink::sendAudio(const std::vector<int16_t>& samples, int channels, int sampleRate) {
	if (!fmtCtx || !audioCodecCtx || !audioFrame || !swrCtx)
		return false;
	if (samples.empty() || channels != audioChannels || sampleRate <= 0)
		return false;

	if (!bufferAudio(samples, channels, sampleRate))
		return false;

	int requiredSamples = audioCodecCtx->frame_size * channels;
	int samplesToProcess = std::min(bufferedSamples, requiredSamples * 4);

	while (samplesToProcess >= requiredSamples && bufferedSamples >= requiredSamples) {
		std::vector<int16_t> frameSamples;
		if (!resampleAudioFrame(frameSamples))
			break;
		if (!encodeAudioFrame())
			return false;
	}
	return true;
}

QString VideoSink::getSdp() const {
	return m_sdp;
}
