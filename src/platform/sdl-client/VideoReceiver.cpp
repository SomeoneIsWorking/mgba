#include "VideoReceiver.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "../web/StreamingCommon.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <unordered_map>

static std::thread g_recvThread;
static std::atomic<bool> g_running { false };

// Track the last seen source width/height for a given SwsContext so we only
// recreate the context when pixel format or size changes.
static std::unordered_map<struct SwsContext*, std::pair<int,int>> g_swsSrcSizes;

static bool setupFormatContext(const char* url, AVFormatContext** fmt) {
	AVDictionary* opts = nullptr;
	av_dict_set(&opts, "protocol_whitelist", "file,udp,rtp", 0);
	av_dict_set(&opts, "fflags", "nobuffer", 0);
	av_dict_set(&opts, "probesize", "500000", 0);
	av_dict_set(&opts, "analyzeduration", "1000000", 0);
	
	if (avformat_open_input(fmt, url, NULL, &opts) < 0) {
		av_dict_free(&opts);
		return false;
	}
	
	if (avformat_find_stream_info(*fmt, NULL) < 0) {
		av_dict_free(&opts);
		return false;
	}
	
	av_dict_free(&opts);
	return true;
}

static bool findStreams(AVFormatContext* fmt, int* videoStream, int* audioStream) {
	*videoStream = -1;
	*audioStream = -1;
	
	for (unsigned i = 0; i < fmt->nb_streams; ++i) {
		if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			*videoStream = i;
		} else if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			*audioStream = i;
		}
	}
	
	return *videoStream >= 0;
}

static AVCodecContext* setupVideoDecoder(AVFormatContext* fmt, int videoStream) {
	const AVCodec* videoCodec = avcodec_find_decoder(fmt->streams[videoStream]->codecpar->codec_id);
	if (!videoCodec) return nullptr;
	
	AVCodecContext* videoCctx = avcodec_alloc_context3(videoCodec);
	if (!videoCctx) return nullptr;
	
	avcodec_parameters_to_context(videoCctx, fmt->streams[videoStream]->codecpar);
	if (avcodec_open2(videoCctx, videoCodec, NULL) < 0) {
		avcodec_free_context(&videoCctx);
		return nullptr;
	}
	
	return videoCctx;
}

static bool setupAudioDecoder(AVFormatContext* fmt, int audioStream, AVCodecContext** audioCctx, SwrContext** swrCtx, 
							   std::atomic<int>& audioChannels, std::atomic<int>& audioSampleRate) {
	const AVCodec* audioCodec = avcodec_find_decoder(fmt->streams[audioStream]->codecpar->codec_id);
	if (!audioCodec) return false;
	
	*audioCctx = avcodec_alloc_context3(audioCodec);
	if (!*audioCctx) return false;
	
	avcodec_parameters_to_context(*audioCctx, fmt->streams[audioStream]->codecpar);
	if (avcodec_open2(*audioCctx, audioCodec, NULL) < 0) {
		avcodec_free_context(audioCctx);
		return false;
	}
	
	*swrCtx = swr_alloc();
	if (!*swrCtx) return false;

	AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	int out_sample_rate = (*audioCctx)->sample_rate; // use decoder's sample rate (likely 44100)
	swr_alloc_set_opts2(swrCtx, &out_ch_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
					   &(*audioCctx)->ch_layout, (*audioCctx)->sample_fmt, (*audioCctx)->sample_rate, 0, nullptr);
	
	if (swr_init(*swrCtx) < 0) {
		swr_free(swrCtx);
		*swrCtx = nullptr;
		return false;
	}

	audioChannels.store((*audioCctx)->ch_layout.nb_channels);
	audioSampleRate.store(out_sample_rate);
	fprintf(stderr, "VideoReceiver: using decoder sample rate: %d Hz, channels: %d\n",
			out_sample_rate, (*audioCctx)->ch_layout.nb_channels);
	
	return true;
}

static void processVideoPacket(AVCodecContext* videoCctx, AVFrame* frame, AVPacket* packet,
							   std::mutex& imgMutex, std::vector<unsigned char>& imgBuf,
							   std::atomic<bool>& newFrame,
							   struct SwsContext** swsPtr) {
	int ret = avcodec_send_packet(videoCctx, packet);
	if (ret < 0) return;

	while (ret >= 0) {
		ret = avcodec_receive_frame(videoCctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
		if (ret < 0) break;

		int srcFmt = frame->format;

		// Handle deprecated "YUVJ" pixel formats which imply full-range; map them to the
		// non-J variants and set color_range accordingly so conversion uses full-range YUV.
		AVPixelFormat srcFmtMapped = (AVPixelFormat)srcFmt;
		if (srcFmt == AV_PIX_FMT_YUVJ420P) {
			srcFmtMapped = AV_PIX_FMT_YUV420P;
			frame->color_range = AVCOL_RANGE_JPEG; // full range
		} else if (srcFmt == AV_PIX_FMT_YUVJ444P) {
			srcFmtMapped = AV_PIX_FMT_YUV444P;
			frame->color_range = AVCOL_RANGE_JPEG;
		} else if (srcFmt == AV_PIX_FMT_YUVJ422P) {
			srcFmtMapped = AV_PIX_FMT_YUV422P;
			frame->color_range = AVCOL_RANGE_JPEG;
		}

		// Ensure colorspace/primaries/range have sensible defaults when unspecified
		if (frame->colorspace == AVCOL_SPC_UNSPECIFIED) frame->colorspace = mgba::STREAM_COLORSPACE;
		if (frame->color_range == AVCOL_RANGE_UNSPECIFIED) frame->color_range = mgba::STREAM_COLOR_RANGE;
		if (frame->color_primaries == AVCOL_PRI_UNSPECIFIED) frame->color_primaries = mgba::STREAM_COLOR_PRIMARIES;
		// Check if context needs to be created or recreated due to size change
		bool recreateSws = false;
		if (!*swsPtr) {
			recreateSws = true;
		} else {
			auto it = g_swsSrcSizes.find(*swsPtr);
			if (it == g_swsSrcSizes.end() || it->second.first != frame->width || it->second.second != frame->height) {
				// Dimensions have changed, or map is out of sync. Recreate.
				sws_freeContext(*swsPtr);
				if (it != g_swsSrcSizes.end()) {
					g_swsSrcSizes.erase(it);
				}
				*swsPtr = nullptr;
				recreateSws = true;
			}
		}


		if (recreateSws) {
			if (*swsPtr) {
				// erase old size record for this context before freeing
				g_swsSrcSizes.erase(*swsPtr);
				sws_freeContext(*swsPtr);
				*swsPtr = nullptr;
			}
			*swsPtr = sws_getContext(
				frame->width, frame->height, 
				srcFmtMapped,
				mgba::STREAM_WIDTH, mgba::STREAM_HEIGHT,
				AV_PIX_FMT_BGR24,
				SWS_BICUBIC,
				NULL, 
				NULL,
				NULL);
			if (!*swsPtr) {
				fprintf(stderr, "VideoReceiver: sws_getContext failed for srcFmt=%d w=%d h=%d\n", srcFmtMapped, frame->width, frame->height);
				continue;
			}
			// record the size for this sws context
			g_swsSrcSizes[*swsPtr] = std::make_pair(frame->width, frame->height);
		}

		int dstBufSize = av_image_get_buffer_size(AV_PIX_FMT_BGR24, mgba::STREAM_WIDTH, mgba::STREAM_HEIGHT, 1);
		std::vector<unsigned char> tmpBuf(dstBufSize);
		uint8_t* dstData[3] = { nullptr };
		int dstLinesize[3] = { 0 };
		av_image_fill_arrays(dstData, dstLinesize, tmpBuf.data(), AV_PIX_FMT_BGR24, mgba::STREAM_WIDTH, mgba::STREAM_HEIGHT, 1);

		int got = sws_scale(*swsPtr, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);
		if (got <= 0) {
			fprintf(stderr, "VideoReceiver: sws_scale returned %d\n", got);
			continue;
		}

		std::lock_guard<std::mutex> lk(imgMutex);
		imgBuf = std::move(tmpBuf);
		newFrame.store(true);
	}
}

static void processAudioPacket(AVCodecContext* audioCctx, SwrContext* swrCtx, AVFrame* frame, AVPacket* packet,
							   std::mutex& audioMutex, std::vector<int16_t>& audioBuf, std::atomic<bool>& newAudio) {
	int ret = avcodec_send_packet(audioCctx, packet);
	if (ret < 0) return;
	
	while (ret >= 0) {
		ret = avcodec_receive_frame(audioCctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
		if (ret < 0) break;

		// Resample audio
		int maxOutSamples = swr_get_delay(swrCtx, audioCctx->sample_rate) + frame->nb_samples;
		std::vector<uint8_t> resampledBuf(maxOutSamples * 2 * sizeof(int16_t));
		uint8_t* outData[1] = { resampledBuf.data() };
		int samplesConverted = swr_convert(swrCtx, outData, maxOutSamples, (const uint8_t**)frame->data, frame->nb_samples);

		if (samplesConverted > 0) {
			std::lock_guard<std::mutex> lk(audioMutex);
			size_t requiredSize = samplesConverted * 2; // 2 channels
			int16_t* outputPtr = (int16_t*)resampledBuf.data();
			audioBuf.insert(audioBuf.end(), outputPtr, outputPtr + requiredSize);
			newAudio.store(true);
		}
	}
}

void start_video_receiver(int localPort, std::mutex& imgMutex,
						  std::vector<unsigned char>& imgBuf, std::atomic<bool>& newFrame, std::atomic<bool>& runningFlag,
						  std::mutex& audioMutex, std::vector<int16_t>& audioBuf, std::atomic<int>& audioChannels,
						  std::atomic<int>& audioSampleRate, std::atomic<bool>& newAudio) {
	if (g_running.load()) return;
	g_running.store(true);
	
	g_recvThread = std::thread([=, &imgMutex, &imgBuf, &newFrame, &runningFlag,
							   &audioMutex, &audioBuf, &audioChannels, &audioSampleRate, &newAudio]() {
	avformat_network_init();
	
	char url[256];
	snprintf(url, sizeof(url), "udp://0.0.0.0:%d?reuse=1&pkt_size=1316", localPort);
	
	AVFormatContext* fmt = nullptr;
	if (!setupFormatContext(url, &fmt)) return;
	
	int videoStream = -1;
	int audioStream = -1;
	if (!findStreams(fmt, &videoStream, &audioStream)) {
		avformat_close_input(&fmt);
		return;
	}

	AVCodecContext* videoCctx = setupVideoDecoder(fmt, videoStream);
	if (!videoCctx) {
		avformat_close_input(&fmt);
		return;
	}

	AVCodecContext* audioCctx = nullptr;
	SwrContext* swrCtx = nullptr;
	if (audioStream >= 0) {
		setupAudioDecoder(fmt, audioStream, &audioCctx, &swrCtx, audioChannels, audioSampleRate);
	}

	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	struct SwsContext* sws = nullptr;

	while (g_running.load() && runningFlag.load()) {
		int ret = av_read_frame(fmt, packet);
		if (ret < 0) {
			usleep(1000);
			continue;
		}

		if (packet->stream_index == videoStream) {
			processVideoPacket(videoCctx, frame, packet, imgMutex, imgBuf, newFrame, &sws);
		} else if (packet->stream_index == audioStream && audioCctx && swrCtx) {
			processAudioPacket(audioCctx, swrCtx, frame, packet, audioMutex, audioBuf, newAudio);
		}

		av_packet_unref(packet);
	}

	// Cleanup
	if (sws) sws_freeContext(sws);
		if (swrCtx) swr_free(&swrCtx);
		if (audioCctx) avcodec_free_context(&audioCctx);
		av_frame_free(&frame);
		av_packet_free(&packet);
		avcodec_free_context(&videoCctx);
		avformat_close_input(&fmt);
	});
}

void stop_video_receiver() {
	if (!g_running.load())
		return;
	g_running.store(false);
	if (g_recvThread.joinable())
		g_recvThread.join();
}
