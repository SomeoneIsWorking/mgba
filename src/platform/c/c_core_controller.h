/* Copyright (c) 2013-2017 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include "c_log_controller.h"
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/thread.h>

#include <stdbool.h>

struct mCore;
struct mCoreThread;
struct mCacheSet;
struct mAVStream;

typedef struct CCoreController CCoreController;

typedef void (*FrameCallback)(void* user);
typedef void (*LogCallback)(int level, int category, const char* log, void* user);

typedef struct {
	void (*interrupt)(CCoreController* controller, void* user);
	void (*resume)(void* user);
	bool (*held)(void* user);
	void* user;
} CInterrupter;

#include <mgba-util/common.h>
#include <pthread.h>

struct CCoreController {
	struct mCore* core;
	struct mCoreThread* thread;
	char* path;
	char* baseDirectory;
	char* savePath;
	char* internalTitle;
	char* dbTitle;
	bool audioSync;
	bool videoSync;
	bool paused;
	bool started;
	int stateSlot;
	struct mCacheSet* cacheSet;
	CLogController* logger;
	bool multiplayerSync;

	int m_moreFrames;

	// Callbacks
	FrameCallback frameCallback;
	void* frameCallbackUser;
	LogCallback logCallback;
	void* logCallbackUser;

	// Mutexes and buffers for thread safety
	pthread_mutex_t m_actionMutex;
	pthread_mutex_t m_bufferMutex;
	mColor* m_activeBuffer;
	mColor* m_completeBuffer;
	size_t m_bufferSize;
	void (*frameAvailableCallback)(CCoreController* controller, void* user);
	void* frameAvailableUser;
};

CCoreController* cCoreControllerCreate(struct mCore* core, struct mCoreThread* thread);
void cCoreControllerDestroy(CCoreController* controller);

struct mCoreThread* cCoreControllerThread(CCoreController* controller);
struct mCore* cCoreControllerCore(CCoreController* controller);
void cCoreControllerSetPath(CCoreController* controller, const char* path, const char* base);
const char* cCoreControllerPath(const CCoreController* controller);
const char* cCoreControllerBaseDirectory(const CCoreController* controller);
enum mPlatform cCoreControllerPlatform(const CCoreController* controller);
bool cCoreControllerIsPaused(const CCoreController* controller);
bool cCoreControllerHasStarted(const CCoreController* controller);
const char* cCoreControllerTitle(const CCoreController* controller);
struct mCacheSet* cCoreControllerGraphicCaches(CCoreController* controller);
int cCoreControllerStateSlot(const CCoreController* controller);
void cCoreControllerSetSync(CCoreController* controller, bool enable);
bool cCoreControllerAudioSync(const CCoreController* controller);
bool cCoreControllerVideoSync(const CCoreController* controller);
void cCoreControllerSetKey(CCoreController* controller, int key, bool down);
void cCoreControllerSetLogger(CCoreController* controller, struct mLogger* logger);
void cCoreControllerAddFrameAction(CCoreController* controller, FrameCallback callback, void* user);
uint64_t cCoreControllerFrameCounter(const CCoreController* controller);

// Simplified methods
void cCoreControllerStart(CCoreController* controller);
void cCoreControllerStop(CCoreController* controller);
void cCoreControllerReset(CCoreController* controller);

void cCoreControllerSetPaused(CCoreController* controller, bool paused);
void cCoreControllerFrameAdvance(CCoreController* controller);
void cCoreControllerLoadState(CCoreController* controller, int slot);
void cCoreControllerSaveState(CCoreController* controller, int slot);
void cCoreControllerFinishFrame(struct mCoreThread* thread);

void cCoreControllerSetFrameAvailableCallback(CCoreController* controller, void (*callback)(CCoreController*, void*), void* user);
const mColor* cCoreControllerDrawContext(CCoreController* controller);