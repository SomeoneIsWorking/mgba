/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include "c_core_controller.h"

#include <mgba/core/lockstep.h>

#include <pthread.h>
#include <stdbool.h>

#ifdef M_CORE_GBA
#include <mgba/internal/gba/sio/lockstep.h>
#endif
#ifdef M_CORE_GB
#include <mgba/internal/gb/sio/lockstep.h>
#endif

#ifdef M_CORE_GBA
#include <mgba/internal/gba/gba.h>
#endif
#ifdef M_CORE_GB
#include <mgba/internal/gb/gb.h>
#endif

#define MAX_GBAS 4

typedef struct CMultiplayerController CMultiplayerController;

typedef void (*GameAttachedCallback)(void* user);
typedef void (*GameDetachedCallback)(void* user);

typedef struct {
	char* path;
	char* baseDirectory;
	int claimed;
} ClaimedSave;

typedef struct {
	CCoreController* controller;
	int preferredId;
	int saveId;
	int awake;
	int waitMask;
	int32_t cyclesPosted;
	bool attached;
	union {
#ifdef M_CORE_GBA
		struct GBASIOLockstepDriver* gbaDriver;
#endif
#ifdef M_CORE_GB
		struct GBSIOLockstepNode* gbNode;
#endif
	} driver;
} CPlayer;

struct CMultiplayerController {
	pthread_mutex_t lock;
	enum mPlatform platform;
	int claimedIds;
	int nextPid;
	CPlayer* pids; // array
	size_t pidsCount;
	size_t pidsCapacity;
	int* players; // array of pids
	size_t playersCount;
	size_t playersCapacity;

	ClaimedSave* claimedSaves;
	size_t claimedSavesCount;
	size_t claimedSavesCapacity;

	// GBA specific
#ifdef M_CORE_GBA
	struct GBASIOLockstepCoordinator gbaCoordinator;
#endif
#ifdef M_CORE_GB
	struct GBSIOLockstep gbLockstep;
#endif

	// Callbacks
	GameAttachedCallback gameAttached;
	void* gameAttachedUser;
	GameDetachedCallback gameDetached;
	void* gameDetachedUser;
};

CMultiplayerController* cMultiplayerControllerCreate();
void cMultiplayerControllerDestroy(CMultiplayerController* controller);

bool cMultiplayerControllerAttachGame(CMultiplayerController* controller, CCoreController* coreController);
void cMultiplayerControllerDetachGame(CMultiplayerController* controller, CCoreController* coreController);

int cMultiplayerControllerPlayerId(const CMultiplayerController* controller, CCoreController* coreController);
int cMultiplayerControllerSaveId(const CMultiplayerController* controller, CCoreController* coreController);
int cMultiplayerControllerAttached(const CMultiplayerController* controller);

CPlayer* cMultiplayerControllerPlayer(CMultiplayerController* controller, int id);
const CPlayer* cMultiplayerControllerPlayerConst(const CMultiplayerController* controller, int id);

int cPlayerId(const CPlayer* player);
bool cPlayerLess(const CPlayer* a, const CPlayer* b);