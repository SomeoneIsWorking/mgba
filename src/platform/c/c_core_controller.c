/* Copyright (c) 2013-2017 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "c_core_controller.h"
#include "c_log_controller.h"

#include <mgba/core/cache-set.h>
#include <mgba/core/thread.h>
#include <mgba/core/core.h>
#include <mgba/core/input.h>
#include <mgba-util/threading.h>
#include <mgba-util/common.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static pthread_mutex_t s_interruptMutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_interruptHeld = false;

CCoreController* cCoreControllerCreate(struct mCore* core, struct mCoreThread* thread) {
	CCoreController* controller = calloc(1, sizeof(CCoreController));
	controller->core = core;
	controller->thread = thread;
	controller->thread->userData = controller;
	controller->thread->frameCallback = cCoreControllerFinishFrame;
	controller->audioSync = true;
	controller->videoSync = false;
	controller->stateSlot = 1;
	controller->cacheSet = calloc(1, sizeof(struct mCacheSet));
	mCacheSetInit(controller->cacheSet, 0, 0, 0); // Simplified
	controller->m_moreFrames = -1;
	pthread_mutex_init(&controller->m_actionMutex, NULL);
	pthread_mutex_init(&controller->m_bufferMutex, NULL);
	controller->m_activeBuffer = NULL;
	controller->m_completeBuffer = NULL;
	controller->m_bufferSize = 0;
	return controller;
}

void cCoreControllerDestroy(CCoreController* controller) {
	mCacheSetDeinit(controller->cacheSet);
	free(controller->cacheSet);
	free(controller->path);
	free(controller->baseDirectory);
	free(controller->savePath);
	free(controller->internalTitle);
	free(controller->dbTitle);
	if (controller->m_activeBuffer) free(controller->m_activeBuffer);
	if (controller->m_completeBuffer) free(controller->m_completeBuffer);
	pthread_mutex_destroy(&controller->m_actionMutex);
	pthread_mutex_destroy(&controller->m_bufferMutex);
	free(controller);
}

struct mCoreThread* cCoreControllerThread(CCoreController* controller) {
	return controller->thread;
}

void cCoreControllerSetPath(CCoreController* controller, const char* path, const char* base) {
	free(controller->path);
	controller->path = strdup(path);
	free(controller->baseDirectory);
	controller->baseDirectory = strdup(base);
}

const char* cCoreControllerPath(const CCoreController* controller) {
	return controller->path;
}

const char* cCoreControllerBaseDirectory(const CCoreController* controller) {
	return controller->baseDirectory;
}

enum mPlatform cCoreControllerPlatform(const CCoreController* controller) {
	return controller->core->platform(controller->core);
}

bool cCoreControllerIsPaused(const CCoreController* controller) {
	return mCoreThreadIsPaused(controller->thread);
}

bool cCoreControllerHasStarted(const CCoreController* controller) {
	return controller->started;
}

const char* cCoreControllerTitle(const CCoreController* controller) {
	return controller->dbTitle ? controller->dbTitle : controller->internalTitle;
}

struct mCacheSet* cCoreControllerGraphicCaches(CCoreController* controller) {
	return controller->cacheSet;
}

int cCoreControllerStateSlot(const CCoreController* controller) {
	return controller->stateSlot;
}

void cCoreControllerSetSync(CCoreController* controller, bool enable) {
	controller->multiplayerSync = enable;
}

bool cCoreControllerAudioSync(const CCoreController* controller) {
	return controller->audioSync;
}

bool cCoreControllerVideoSync(const CCoreController* controller) {
	return controller->videoSync;
}

void cCoreControllerSetLogger(CCoreController* controller, struct mLogger* logger) {
	controller->logger = (CLogController*)logger;
}

void cCoreControllerSetKey(CCoreController* controller, int key, bool down) {
	if (down) {
		controller->core->addKeys(controller->core, 1 << key);
	} else {
		controller->core->clearKeys(controller->core, 1 << key);
	}
}

void cCoreControllerAddFrameAction(CCoreController* controller, FrameCallback callback, void* user) {
	controller->frameCallback = callback;
	controller->frameCallbackUser = user;
}

uint64_t cCoreControllerFrameCounter(const CCoreController* controller) {
	return controller->core->frameCounter(controller->core);
}

void cCoreControllerStart(CCoreController* controller) {
	controller->started = true;
	unsigned width, height;
	controller->core->currentVideoSize(controller->core, &width, &height);
	size_t size = width * height * sizeof(mColor);
	if (controller->m_bufferSize != size) {
		free(controller->m_activeBuffer);
		free(controller->m_completeBuffer);
		controller->m_activeBuffer = malloc(size);
		controller->m_completeBuffer = malloc(size);
		controller->m_bufferSize = size;
	}
	controller->core->setVideoBuffer(controller->core, controller->m_activeBuffer, width);
}

void cCoreControllerStop(CCoreController* controller) {
	controller->started = false;
}

void cCoreControllerReset(CCoreController* controller) {
	controller->core->reset(controller->core);
}

void cCoreControllerSetPaused(CCoreController* controller, bool paused) {
	pthread_mutex_lock(&controller->m_actionMutex);
	if (paused) {
		if (controller->m_moreFrames < 0) {
			controller->m_moreFrames = 1;
		}
	} else {
		controller->m_moreFrames = -1;
		if (mCoreThreadIsPaused(controller->thread)) {
			mCoreThreadUnpause(controller->thread);
		}
	}
	pthread_mutex_unlock(&controller->m_actionMutex);
}

void cCoreControllerFrameAdvance(CCoreController* controller) {
	pthread_mutex_lock(&controller->m_actionMutex);
	controller->m_moreFrames = 1;
	if (mCoreThreadIsPaused(controller->thread)) {
		mCoreThreadUnpause(controller->thread);
	}
	pthread_mutex_unlock(&controller->m_actionMutex);
}

void cCoreControllerFinishFrame(struct mCoreThread* thread) {
	CCoreController* controller = (CCoreController*)thread->userData;
	pthread_mutex_lock(&controller->m_actionMutex);
	if (controller->m_moreFrames > 0) {
		--controller->m_moreFrames;
		if (!controller->m_moreFrames) {
			mCoreThreadPauseFromThread(thread);
		}
	}
	pthread_mutex_unlock(&controller->m_actionMutex);

	// Copy buffer
	pthread_mutex_lock(&controller->m_bufferMutex);
	if (controller->m_activeBuffer && controller->m_completeBuffer) {
		memcpy(controller->m_completeBuffer, controller->m_activeBuffer, controller->m_bufferSize);
	}
	pthread_mutex_unlock(&controller->m_bufferMutex);

	// Call frame available callback
	if (controller->frameAvailableCallback) {
		controller->frameAvailableCallback(controller, controller->frameAvailableUser);
	}
}

void cCoreControllerLoadState(CCoreController* controller, int slot) {
	(void)controller;
	(void)slot;
	// Simplified
}

void cCoreControllerSaveState(CCoreController* controller, int slot) {
	(void)controller;
	(void)slot;
	// Simplified
}

void cCoreControllerSetFrameAvailableCallback(CCoreController* controller, void (*callback)(CCoreController*, void*), void* user) {
	controller->frameAvailableCallback = callback;
	controller->frameAvailableUser = user;
}

const mColor* cCoreControllerDrawContext(CCoreController* controller) {
	// Note: Assumes the buffer is not being written during read, which is the case since write is in finishFrame from core thread
	return controller->m_completeBuffer;
}

void cInterrupterDestroy(CInterrupter* interrupter) {
	free(interrupter);
}

void cInterrupterInterrupt(CInterrupter* interrupter) {
	(void)interrupter;
	pthread_mutex_lock(&s_interruptMutex);
	s_interruptHeld = true;
}

void cInterrupterResume(CInterrupter* interrupter) {
	(void)interrupter;
	s_interruptHeld = false;
	pthread_mutex_unlock(&s_interruptMutex);
}

bool cInterrupterHeld(CInterrupter* interrupter) {
	(void)interrupter;
	return s_interruptHeld;
}