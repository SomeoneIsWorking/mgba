#include <SDL.h>
#include <arpa/inet.h>
#include <atomic>
#include <condition_variable>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../web/StreamingCommon.h"
#include "Gui.h"
#include "UdpDiscovery.h"
#include "VideoReceiver.h"
#include "ControlClient.h"
#include "InputHandler.h"

static const int DISCOVERY_PORT = 43889;

enum class AppState { DISCOVERING, CONNECTING, CONNECTED, EXIT };

int main(int argc, char** argv) {
	(void) argc;
	(void) argv;

	// Signal handler so Ctrl+C exits immediately
	struct sigaction sa {};
	sa.sa_handler = [](int) { _exit(130); };
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, nullptr);
	sigaction(SIGTERM, &sa, nullptr);

	// GUI: create window early so we can show status while discovering/connecting
	GuiHandle gui {};
	const int winW = 480, winH = 320;
	if (!gui_init(gui, winW, winH))
		return 1;

	// App state
	AppState state = AppState::DISCOVERING;

	// Discovery state variables
	std::atomic<bool> discoveryDone{ false };
	DiscoveryResult discovered;
	std::thread discoveryThread;

	auto startDiscovery = [&]() {
		gui_set_status(gui, "mGBA Stream - Discovering server...");
		discoveryDone.store(false);
		discoveryThread = std::thread([&]{
			fprintf(stdout, "Waiting for server discovery on UDP %d...\n", DISCOVERY_PORT);
			bool found = udp_discover_server(DISCOVERY_PORT, 0, discovered);
			if (!found) fprintf(stderr, "Discovery failed or interrupted\n");
			discoveryDone.store(found);
		});
	};

	auto stopDiscovery = [&]() {
		// we rely on the blocking discovery thread to return when it finds something; detach if joinable
		if (discoveryThread.joinable()) {
			discoveryThread.join();
		}
	};

	startDiscovery();

	// Initialize SDL audio
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		fprintf(stderr, "SDL audio initialization failed: %s\n", SDL_GetError());
		return 1;
	}

	SDL_AudioSpec desiredSpec;
	SDL_zero(desiredSpec);
	desiredSpec.freq = mgba::STREAM_AUDIO_SAMPLE_RATE_CODEC; // Use codec sample rate for SDL
	desiredSpec.format = AUDIO_S16SYS;
	desiredSpec.channels = mgba::STREAM_AUDIO_CHANNELS;
	desiredSpec.samples = 512; // Smaller buffer for lower latency
	desiredSpec.callback = nullptr; // We'll use SDL_QueueAudio

	SDL_AudioSpec obtainedSpec;
	int audioDev = SDL_OpenAudioDevice(nullptr, 0, &desiredSpec, &obtainedSpec, 0);
	if (audioDev == 0) {
		fprintf(stderr, "Failed to open audio device: %s\n", SDL_GetError());
		return 1;
	}
	// Start audio immediately to reduce delay
	SDL_PauseAudioDevice(audioDev, 0);

	// Video receiver: create a local UDP socket and register our receiving port with the server
	std::mutex imgMutex;
	std::vector<unsigned char> imgBuf;
	// frame width/height are fixed (GBA resolution)
	std::atomic<bool> newFrame { false };
	std::atomic<bool> running { true };

	// Audio receiver
	std::mutex audioMutex;
	std::vector<int16_t> audioBuf; // continuous append-only buffer (producer appends)
	// readPos is the index (in samples) of the next sample to be consumed by the audio thread
	std::atomic<size_t> audioReadPos { 0 };
	std::atomic<int> audioChannels { 0 }, audioSampleRate { 0 };
	std::atomic<bool> newAudio { false };

	// Create a temporary UDP socket to get an ephemeral port the client will listen on
	int reg_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (reg_sock < 0) {
		fprintf(stderr, "failed to create UDP socket for registration\n");
		return 1;
	}
	struct sockaddr_in bind_addr {};
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind_addr.sin_port = htons(0); // ephemeral port
	if (bind(reg_sock, (struct sockaddr*) &bind_addr, sizeof(bind_addr)) < 0) {
		close(reg_sock);
		fprintf(stderr, "bind failed\n");
		return 1;
	}
	struct sockaddr_in local_sa {};
	socklen_t la = sizeof(local_sa);
	if (getsockname(reg_sock, (struct sockaddr*) &local_sa, &la) < 0) {
		close(reg_sock);
		return 1;
	}
	int localPort = ntohs(local_sa.sin_port);

	// Control/receiver variables
	ControlClient control;
	bool receiverStarted = false;

	// Input handling
	InputHandler input;
	input.setSendCallback([&](uint8_t action, uint8_t key){ control.sendInput(action, key); });

	auto startControl = [&]() {
		gui_set_status(gui, "mGBA Stream - Connecting to server...");
		control.start(discovered.host, discovered.tcpPort, (uint16_t)localPort);
	};

	auto stopControl = [&]() {
		control.stop();
	};

	auto startReceiver = [&]() {
		start_video_receiver(localPort, imgMutex, imgBuf, newFrame, running,
							 audioMutex, audioBuf, audioChannels, audioSampleRate, newAudio);
		receiverStarted = true;
	};

	auto stopReceiver = [&]() {
		stop_video_receiver();
		receiverStarted = false;
	};

	close(reg_sock);

	// Main loop: events + draw + state transitions
	while (running.load() && state != AppState::EXIT) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) {
				running.store(false);
				break;
			}
			// Let input handler consume keyboard/controller events and forward them to control
			input.processEvent(ev);
		}

		// Per-frame input polling (e.g., for analog sticks)
		input.poll();

		// Simple state machine transitions
		switch (state) {
		case AppState::DISCOVERING:
			if (discoveryDone.load()) {
				// discovered -> move to CONNECTING
				stopDiscovery();
				state = AppState::CONNECTING;
				startControl();
			}
			break;
		case AppState::CONNECTING:
			if (control.connected()) {
				// connected -> start receiver and go to CONNECTED
				gui_set_status(gui, "");
				startReceiver();
				state = AppState::CONNECTED;
			}
			// if control failed to start and stopped, we stay in CONNECTING; ControlClient handles reconnects internally
			break;
		case AppState::CONNECTED:
			if (!control.connected()) {
				// connection lost -> cleanup and go back to discovering
				if (receiverStarted) stopReceiver();
				stopControl();
				state = AppState::DISCOVERING;
				startDiscovery();
			}
			break;
		default:
			break;
		}

		if (newFrame.load()) {
			std::vector<unsigned char> copyBuf;
			{
				std::lock_guard<std::mutex> lk(imgMutex);
				copyBuf = imgBuf;
			}
			if (!copyBuf.empty()) {
				gui_draw_frame(gui, copyBuf);
			}
			newFrame.store(false);
		}

		if (newAudio.load()) {
			// Queue audio immediately to reduce latency using a continuous buffer.
			// We'll read from audioReadPos up to the current end of audioBuf.
			std::vector<int16_t> toQueue;
			{
				std::lock_guard<std::mutex> lk(audioMutex);
				size_t readPos = audioReadPos.load();
				size_t available = 0;
				if (audioBuf.size() > readPos) available = audioBuf.size() - readPos;
				if (available > 0) {
					// copy only the new samples
					toQueue.assign(audioBuf.begin() + readPos, audioBuf.end());
					// advance read position by number of samples we'll queue
					audioReadPos.store(readPos + toQueue.size());
				}
			}
			if (!toQueue.empty()) {
				// Queue audio immediately to reduce latency
				int queued = SDL_GetQueuedAudioSize(audioDev);
				if (queued < 4096) { // Keep buffer small to reduce latency
					SDL_QueueAudio(audioDev, toQueue.data(), toQueue.size() * sizeof(int16_t));
				}
			}
			newAudio.store(false);

			// Trim consumed samples from the front of audioBuf occasionally to avoid unbounded growth.
			// Do trimming while holding the mutex to avoid races with the producer.
			const size_t TRIM_THRESHOLD = 48000; // samples (~0.5s at 96kHz or 1s at 48kHz depending on rate)
			{
				std::lock_guard<std::mutex> lk(audioMutex);
				size_t rp = audioReadPos.load();
				if (rp > TRIM_THRESHOLD) {
					// erase consumed prefix and reset read position
					audioBuf.erase(audioBuf.begin(), audioBuf.begin() + rp);
					audioReadPos.store(0);
				}
			}
		}
		SDL_Delay(16);
	}

	// Shutdown
	running.store(false);
	stop_video_receiver();
	// Stop background control client
	control.stop();
	gui_shutdown(gui, true, audioDev);
	return 0;
}
