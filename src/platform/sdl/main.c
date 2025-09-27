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
#include <mgba/core/thread.h>
#include <mgba/internal/gba/input.h>

#include <mgba/feature/commandline.h>
#include <mgba-util/vfs.h>

#include <SDL.h>

#include <errno.h>
#include <signal.h>

#define PORT "sdl"

static void mSDLDeinit(struct mSDLRenderer* renderer);

static int mSDLRun(struct mSDLRenderer* renderer, struct mArguments* args);

static struct mStandardLogger _logger;

static struct VFile* _state = NULL;

static void _loadState(struct mCoreThread* thread) {
	mCoreLoadStateNamed(thread->core, _state, SAVESTATE_RTC);
}

#include "../../platform/c/c_multiplayer_controller.h"
#include "../../platform/c/c_core_controller.h"

int mSDLRunMultiplayer(struct mSDLRenderer* renderers, int numPlayers, CMultiplayerController* multiplayer, CCoreController* cControllers[4], struct mCoreThread threads[4], struct mArguments* args, struct mStandardLogger* logger);

int main(int argc, char** argv) {
#ifdef _WIN32
	AttachConsole(ATTACH_PARENT_PROCESS);
	freopen("CONOUT$", "w", stdout);
#endif
	int numPlayers = 1;
	struct mSDLRenderer* renderers = NULL;
	CMultiplayerController* multiplayer = NULL;
	struct mCoreThread threads[4] = {0};
	CCoreController* cControllers[4] = {0};

	struct mCoreOptions opts = {
		.useBios = true,
		.rewindEnable = true,
		.rewindBufferCapacity = 600,
		.rewindBufferInterval = 1,
		.audioBuffers = 1024,
		.videoSync = false,
		.audioSync = false,
		.volume = 0x100,
		.logLevel = mLOG_WARN | mLOG_ERROR | mLOG_FATAL,
	};

	struct mArguments args;
	struct mGraphicsOpts graphicsOpts;

	struct mSubParser subparser;

	mSubParserGraphicsInit(&subparser, &graphicsOpts);
	bool parsed = mArgumentsParse(&args, argc, argv, &subparser, 1);
	if (!args.fname && !args.showVersion) {
		parsed = false;
	}
	if (!parsed || args.showHelp) {
		usage(argv[0], NULL, NULL, &subparser, 1);
		mArgumentsDeinit(&args);
		return !parsed;
	}
	if (args.showVersion) {
		version(argv[0]);
		mArgumentsDeinit(&args);
		return 0;
	}

	numPlayers = args.split > 1 ? args.split : 1;
	renderers = calloc(numPlayers, sizeof(struct mSDLRenderer));
	if (numPlayers > 1) {
		multiplayer = cMultiplayerControllerCreate();
	}

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("Could not initialize video: %s\n", SDL_GetError());
		mArgumentsDeinit(&args);
		return 1;
	}

	// Initialize logger
	mStandardLoggerInit(&_logger);

	for (int i = 0; i < numPlayers; ++i) {
		struct mSDLRenderer* renderer = &renderers[i];
		renderer->core = mCoreFind(args.fname);
		if (!renderer->core) {
			printf("Could not run game. Are you sure the file exists and is a compatible game?\n");
			mArgumentsDeinit(&args);
			return 1;
		}

		if (!renderer->core->init(renderer->core)) {
			mArgumentsDeinit(&args);
			return 1;
		}

		renderer->core->baseVideoSize(renderer->core, &renderer->width, &renderer->height);
		renderer->ratio = graphicsOpts.multiplier;
		if (renderer->ratio == 0) {
			renderer->ratio = 1;
		}

		opts.width = renderer->width * renderer->ratio;
		opts.height = renderer->height * renderer->ratio;

		mInputMapInit(&renderer->core->inputMap, &GBAInputInfo);
		mCoreInitConfig(renderer->core, PORT);
		mArgumentsApply(&args, &subparser, 1, &renderer->core->config);

		mCoreConfigSetDefaultIntValue(&renderer->core->config, "logToStdout", true);
		mCoreConfigLoadDefaults(&renderer->core->config, &opts);
		mCoreLoadConfig(renderer->core);

		if (i == 0) {
			mStandardLoggerConfig(&_logger, &renderer->core->config);
		}

		renderer->outputBuffer = malloc(renderer->width * renderer->height * sizeof(mColor));
		renderer->core->setVideoBuffer(renderer->core, renderer->outputBuffer, renderer->width);

		if (!mCoreLoadFile(renderer->core, args.fname)) {
			printf("Could not load ROM for player %d\n", i);
			return 1;
		}
		mCoreAutoloadSave(renderer->core);
		mArgumentsApplyFileLoads(&args, renderer->core);

		renderer->viewportWidth = renderer->core->opts.width;
		renderer->viewportHeight = renderer->core->opts.height;

		if (numPlayers == 1) {

#ifdef BUILD_GL
			if (mSDLGLCommonInit(renderer)) {
				mSDLGLCreate(renderer);
			} else
#elif defined(BUILD_GLES2) || defined(USE_EPOXY)
			if (mSDLGLCommonInit(renderer))
			{
				mSDLGLES2Create(renderer);
			} else
#endif
			{
				mSDLSWCreate(renderer);
			}

			if (!renderer->init(renderer)) {
				mArgumentsDeinit(&args);
				mCoreConfigDeinit(&renderer->core->config);
				renderer->core->deinit(renderer->core);
				return 1;
			}

#if SDL_VERSION_ATLEAST(2, 0, 0)
			// Set window position for side-by-side
			if (numPlayers > 1) {
				int x = (i % 2) * renderer->viewportWidth; // Side by side, no gap
				int y = (i / 2) * renderer->viewportHeight; // Stacked if more than 2
				SDL_SetWindowPosition(renderer->window, x, y);
			}
#endif

			renderer->player.bindings = &renderer->core->inputMap;
			mSDLInitBindingsGBA(&renderer->core->inputMap);
			mSDLInitEvents(&renderer->events);
			mSDLEventsLoadConfig(&renderer->events, mCoreConfigGetInput(&renderer->core->config));
			mSDLAttachPlayer(&renderer->events, &renderer->player);
			mSDLPlayerLoadConfig(&renderer->player, mCoreConfigGetInput(&renderer->core->config));

#if SDL_VERSION_ATLEAST(2, 0, 0)
			renderer->core->setPeripheral(renderer->core, mPERIPH_RUMBLE, &renderer->player.rumble.d.d);
#endif
		}

		// For multiplayer
		if (multiplayer) {
			// Create CCoreController wrapper
			CCoreController* cController = cCoreControllerCreate(renderer->core, &threads[i]);
			cCoreControllerSetPath(cController, args.fname, ".");
			cCoreControllerSetLogger(cController, &(_logger.d)); // Set the logger
			cCoreControllerStart(cController);
			cMultiplayerControllerAttachGame(multiplayer, cController);
			cControllers[i] = cController;
		}
	}

	int ret = 0;
	if (numPlayers > 1) {
		ret = mSDLRunMultiplayer(renderers, numPlayers, multiplayer, cControllers, threads, &args, &_logger);
	} else {
		// Original single player
		struct mSDLRenderer* renderer = &renderers[0];
		ret = mSDLRun(renderer, &args);
	}

	for (int i = 0; i < numPlayers; ++i) {
		struct mSDLRenderer* renderer = &renderers[i];
		if (numPlayers == 1) {
			mSDLDetachPlayer(&renderer->events, &renderer->player);
			mInputMapDeinit(&renderer->core->inputMap);
			mSDLDeinit(renderer);
		}
		free(renderer->outputBuffer);
		mCoreConfigFreeOpts(&opts);
		mCoreConfigDeinit(&renderer->core->config);
		renderer->core->deinit(renderer->core);
	}

	if (multiplayer) {
		cMultiplayerControllerDestroy(multiplayer);
	}
	free(renderers);

	mArgumentsDeinit(&args);
	mCoreConfigFreeOpts(&opts);

	mStandardLoggerDeinit(&_logger);

	return ret;
}

#if defined(_WIN32) && !defined(_UNICODE)
#include <mgba-util/string.h>

int wmain(int argc, wchar_t** argv) {
	char** argv8 = malloc(sizeof(char*) * argc);
	int i;
	for (i = 0; i < argc; ++i) {
		argv8[i] = utf16to8((uint16_t*) argv[i], wcslen(argv[i]) * 2);
	}
	__argv = argv8;
	int ret = main(argc, argv8);
	for (i = 0; i < argc; ++i) {
		free(argv8[i]);
	}
	free(argv8);
	return ret;
}
#endif

int mSDLRun(struct mSDLRenderer* renderer, struct mArguments* args) {
	struct mCoreThread thread = {
		.core = renderer->core
	};
	if (!mCoreLoadFile(renderer->core, args->fname)) {
		return 1;
	}
	mCoreAutoloadSave(renderer->core);
	mArgumentsApplyFileLoads(args, renderer->core);
#ifdef ENABLE_SCRIPTING
	struct mScriptBridge* bridge = mScriptBridgeCreate();
#ifdef ENABLE_PYTHON
	mPythonSetup(bridge);
#endif
#ifdef ENABLE_DEBUGGERS
	CLIDebuggerScriptEngineInstall(bridge);
#endif
#endif

#ifdef ENABLE_DEBUGGERS
	struct mDebugger debugger;
	mDebuggerInit(&debugger);
	bool hasDebugger = mArgumentsApplyDebugger(args, renderer->core, &debugger);

	if (hasDebugger) {
		mDebuggerAttach(&debugger, renderer->core);
		mDebuggerEnter(&debugger, DEBUGGER_ENTER_MANUAL, NULL);
#ifdef ENABLE_SCRIPTING
		mScriptBridgeSetDebugger(bridge, &debugger);
#endif
	} else {
		mDebuggerDeinit(&debugger);
	}
#endif

	renderer->audio.samples = renderer->core->opts.audioBuffers;
	renderer->audio.sampleRate = 44100;
	thread.logger.logger = &_logger.d;

	bool didFail = !mCoreThreadStart(&thread);

	if (!didFail) {
#if SDL_VERSION_ATLEAST(2, 0, 0)
		renderer->core->currentVideoSize(renderer->core, &renderer->width, &renderer->height);
		unsigned width = renderer->width * renderer->ratio;
		unsigned height = renderer->height * renderer->ratio;
		if (width != (unsigned) renderer->viewportWidth && height != (unsigned) renderer->viewportHeight) {
			SDL_SetWindowSize(renderer->window, width, height);
			renderer->player.windowUpdated = 1;
		}
		mSDLSetScreensaverSuspendable(&renderer->events, renderer->core->opts.suspendScreensaver);
		mSDLSuspendScreensaver(&renderer->events);
#endif
		if (mSDLInitAudio(&renderer->audio, &thread)) {
			if (args->savestate) {
				struct VFile* state = VFileOpen(args->savestate, O_RDONLY);
				if (state) {
					_state = state;
					mCoreThreadRunFunction(&thread, _loadState);
					_state = NULL;
					state->close(state);
				}
			}
			renderer->runloop(renderer, &thread);
			mSDLPauseAudio(&renderer->audio);
			if (mCoreThreadHasCrashed(&thread)) {
				didFail = true;
				printf("The game crashed!\n");
				mCoreThreadEnd(&thread);
			}
		} else {
			didFail = true;
			printf("Could not initialize audio.\n");
		}
#if SDL_VERSION_ATLEAST(2, 0, 0)
		mSDLResumeScreensaver(&renderer->events);
		mSDLSetScreensaverSuspendable(&renderer->events, false);
#endif

		mCoreThreadJoin(&thread);
	} else {
		printf("Could not run game. Are you sure the file exists and is a compatible game?\n");
	}
	renderer->core->unloadROM(renderer->core);

#ifdef ENABLE_SCRIPTING
	mScriptBridgeDestroy(bridge);
#endif

#ifdef ENABLE_DEBUGGERS
	if (hasDebugger) {
		renderer->core->detachDebugger(renderer->core);
		mDebuggerDeinit(&debugger);
	}
#endif

	return didFail;
}

static void mSDLDeinit(struct mSDLRenderer* renderer) {
	mSDLDeinitEvents(&renderer->events);
	mSDLDeinitAudio(&renderer->audio);
#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_DestroyWindow(renderer->window);
#endif

	renderer->deinit(renderer);

	SDL_Quit();
}
