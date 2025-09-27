/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "main.h"

#include <mgba/internal/debugger/cli-debugger.h>

#ifdef ENABLE_SCRIPTING
#include <mgba/core/scripting.h>

#ifdef ENABLE_PYTHON
#include "platform/python/engine.h"
#endif
#endif

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/input.h>
#include <mgba/core/serialize.h>
#include <mgba/core/sync.h>
#include <mgba/core/thread.h>
#include <mgba/internal/gba/input.h>

#include <mgba/feature/commandline.h>
#include <mgba-util/vfs.h>

#include <SDL.h>

#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include "../../platform/c/c_multiplayer_controller.h"
#include "../../platform/c/c_core_controller.h"

int mSDLRunMultiplayer(struct mSDLRenderer* renderers, int numPlayers, CMultiplayerController* multiplayer, CCoreController* cControllers[4], struct mCoreThread threads[4], struct mArguments* args, struct mStandardLogger* logger) {
	int ret = 0;
	// Custom split-screen rendering
	struct mSDLRenderer* renderer = &renderers[0]; // Use first for dimensions
	int windowWidth = renderer->viewportWidth * numPlayers;
	int windowHeight = renderer->viewportHeight;
	SDL_Window* window = SDL_CreateWindow("mGBA Split Screen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
	if (!window) {
		printf("Could not create window: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Renderer* sdlRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!sdlRenderer) {
		printf("Could not create renderer: %s\n", SDL_GetError());
		SDL_DestroyWindow(window);
		return 1;
	}
#ifdef COLOR_16_BIT
#ifdef COLOR_5_6_5
	SDL_Texture* texture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, windowWidth, windowHeight);
#else
	SDL_Texture* texture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, windowWidth, windowHeight);
#endif
#else
	SDL_Texture* texture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, windowWidth, windowHeight);
#endif
	if (!texture) {
		printf("Could not create texture: %s\n", SDL_GetError());
		SDL_DestroyRenderer(sdlRenderer);
		SDL_DestroyWindow(window);
		return 1;
	}
	SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);

	// Start threads
	for (int i = 0; i < numPlayers; ++i) {
		threads[i].core = renderers[i].core;
		threads[i].logger.logger = &logger->d;
		if (!mCoreThreadStart(&threads[i])) {
			printf("Could not start thread for player %d\n", i);
			ret = 1;
			break;
		}
	}

	if (ret == 0) {
		int currentW = windowWidth;
		int currentH = windowHeight;
		bool running = true;
		bool audioInitialized = false;
		if (numPlayers > 1) {
			// Enable video sync for first player
			mCoreSyncSetVideoSync(&threads[0].impl->sync, true);
		}

		// Initialize audio for player 1
		renderers[0].audio.samples = renderers[0].core->opts.audioBuffers;
		renderers[0].audio.sampleRate = 44100;
		if (mSDLInitAudio(&renderers[0].audio, &threads[0])) {
			audioInitialized = true;
		} else {
			printf("Could not initialize audio for player 1\n");
			ret = 1;
			running = false;
		}

		while (running) {
			SDL_Event event;
			while (SDL_PollEvent(&event)) {
				if (event.type == SDL_QUIT) {
					running = false;
				} else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
					int down = (event.type == SDL_KEYDOWN) ? 1 : 0;
					int gbaKey = -1;
					switch (event.key.keysym.sym) {
					// Player 1
					case SDLK_w: gbaKey = GBA_KEY_UP; break;
					case SDLK_s: gbaKey = GBA_KEY_DOWN; break;
					case SDLK_a: gbaKey = GBA_KEY_LEFT; break;
					case SDLK_d: gbaKey = GBA_KEY_RIGHT; break;
					case SDLK_SPACE: gbaKey = GBA_KEY_A; break;
					case SDLK_RETURN: gbaKey = GBA_KEY_B; break;
					case SDLK_q: gbaKey = GBA_KEY_L; break;
					case SDLK_e: gbaKey = GBA_KEY_R; break;
				
					}
					cCoreControllerSetKey(cControllers[0], gbaKey, down);
					cCoreControllerSetKey(cControllers[1], gbaKey, down);
				}
				// Handle other events if needed
			}

			if (numPlayers > 1) {
				mCoreSyncWaitFrameStart(&threads[0].impl->sync);
			}

			SDL_GetWindowSize(window, &currentW, &currentH);

			// Update texture with combined pixels
			void* pixels;
			int pitch;
			if (SDL_LockTexture(texture, NULL, &pixels, &pitch) == 0) {
				// Copy pixels side by side
				for (int y = 0; y < windowHeight; ++y) {
					for (int p = 0; p < numPlayers; ++p) {
						struct mSDLRenderer* r = &renderers[p];
						const mColor* src = cCoreControllerDrawContext(cControllers[p]) + y * r->width;
#ifdef COLOR_16_BIT
						uint16_t* dst = (uint16_t*)pixels + y * (windowWidth) + p * r->width;
						for (int x = 0; x < r->width; ++x) {
							dst[x] = src[x];
						}
#else
						uint32_t* dst = (uint32_t*)pixels + y * (windowWidth) + p * r->width;
						for (int x = 0; x < r->width; ++x) {
							dst[x] = src[x];
						}
#endif
					}
				}
				SDL_UnlockTexture(texture);
			}

			SDL_RenderClear(sdlRenderer);
			float scale = (currentW < currentH * windowWidth / windowHeight) ? (float)currentW / windowWidth : (float)currentH / windowHeight;
			SDL_Rect dstRect;
			dstRect.w = windowWidth * scale;
			dstRect.h = windowHeight * scale;
			dstRect.x = (currentW - dstRect.w) / 2;
			dstRect.y = (currentH - dstRect.h) / 2;
			SDL_RenderCopy(sdlRenderer, texture, NULL, &dstRect);
			SDL_RenderPresent(sdlRenderer);

			if (numPlayers > 1) {
				mCoreSyncWaitFrameEnd(&threads[0].impl->sync);
			}
		}
		
		if (numPlayers > 1) {
			running = false;
		}

		// Deinitialize audio for player 1
		if (audioInitialized) {
			mSDLDeinitAudio(&renderers[0].audio);
		}
	}

	// Stop threads
	for (int i = 0; i < numPlayers; ++i) {
		cMultiplayerControllerDetachGame(multiplayer, cControllers[i]);
		mCoreThreadEnd(&threads[i]);
		mCoreThreadJoin(&threads[i]);
	}

	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(sdlRenderer);
	SDL_DestroyWindow(window);
	return ret;
}