/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "c_multiplayer_controller.h"

#include <mgba/core/interface.h>
#include <mgba/core/lockstep.h>

#ifdef M_CORE_GBA
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba/gba/interface.h>
#endif
#ifdef M_CORE_GB
#include <mgba/internal/gb/sio/lockstep.h>
#include <mgba/internal/gb/sio.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct {
	struct mLockstepThreadUser d;
	CMultiplayerController* controller;
	int pid;
} CLockstepUser;

static int lockstepRequestedId(struct mLockstepUser* ctx) {
	struct mLockstepThreadUser* tctx = (struct mLockstepThreadUser*)ctx;
	CLockstepUser* user = (CLockstepUser*)tctx;
	CMultiplayerController* controller = user->controller;
	CPlayer* p = &controller->pids[user->pid];
	return p->preferredId;
}

CMultiplayerController* cMultiplayerControllerCreate() {
	CMultiplayerController* controller = calloc(1, sizeof(CMultiplayerController));
	if (!controller) return NULL;
	pthread_mutex_init(&controller->lock, NULL);
	controller->platform = mPLATFORM_NONE;
	controller->claimedIds = 0;
	controller->nextPid = 0;
	controller->pids = NULL;
	controller->pidsCount = 0;
	controller->pidsCapacity = 0;
	controller->players = NULL;
	controller->playersCount = 0;
	controller->playersCapacity = 0;
	controller->claimedSaves = NULL;
	controller->claimedSavesCount = 0;
	controller->claimedSavesCapacity = 0;
	return controller;
}

void cMultiplayerControllerDestroy(CMultiplayerController* controller) {
	if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
		GBASIOLockstepCoordinatorDeinit(&controller->gbaCoordinator);
#endif
	}
	for (size_t i = 0; i < controller->claimedSavesCount; ++i) {
		free(controller->claimedSaves[i].path);
		free(controller->claimedSaves[i].baseDirectory);
	}
	free(controller->claimedSaves);
	for (size_t i = 0; i < controller->pidsCount; ++i) {
		// Cleanup if needed
	}
	free(controller->pids);
	free(controller->players);
	pthread_mutex_destroy(&controller->lock);
	free(controller);
}

bool cMultiplayerControllerAttachGame(CMultiplayerController* controller, CCoreController* coreController) {
	if (controller->platform == mPLATFORM_NONE) {
		enum mPlatform platform = cCoreControllerPlatform(coreController);
		if (platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
			GBASIOLockstepCoordinatorInit(&controller->gbaCoordinator);
#endif
		} else if (platform == mPLATFORM_GB) {
#ifdef M_CORE_GB
			GBSIOLockstepInit(&controller->gbLockstep);
#endif
		} else {
			return false;
		}
		controller->platform = platform;
	} else if (cCoreControllerPlatform(coreController) != controller->platform) {
		return false;
	}

	struct mCoreThread* thread = cCoreControllerThread(coreController);
	if (!thread) {
		return false;
	}

	CPlayer player = {0};
	player.controller = coreController;
	for (int i = 0; i < MAX_GBAS; ++i) {
		if (controller->claimedIds & (1 << i)) {
			continue;
		}
		player.preferredId = i;
		controller->claimedIds |= 1 << i;
		break;
	}

	bool isGBA = controller->platform == mPLATFORM_GBA;
	if (isGBA) {
#ifdef M_CORE_GBA
		struct GBASIOLockstepDriver* driver = calloc(1, sizeof(struct GBASIOLockstepDriver));
		CLockstepUser* user = calloc(1, sizeof(CLockstepUser));
		mLockstepThreadUserInit(&user->d, thread);
		user->controller = controller;
		user->pid = controller->nextPid;
		user->d.d.requestedId = lockstepRequestedId;
		GBASIOLockstepDriverCreate(driver, (struct mLockstepUser*)&user->d);
		player.driver.gbaDriver = driver;
		GBASIOLockstepCoordinatorAttach(&controller->gbaCoordinator, driver);
		coreController->core->setPeripheral(coreController->core, mPERIPH_GBA_LINK_PORT, &driver->d);
#endif
	} else {
#ifdef M_CORE_GB
		struct GBSIOLockstepNode* gbNode = calloc(1, sizeof(struct GBSIOLockstepNode));
		GBSIOLockstepNodeCreate(gbNode);
		player.driver.gbNode = gbNode;
		GBSIOLockstepAttachNode(&controller->gbLockstep, gbNode);
		struct GB* gb = (struct GB*)thread->core->board;
		GBSIOSetDriver(&gb->sio, &gbNode->d);
#endif
	}
	player.attached = true;

	// Save ID logic
	const char* path = cCoreControllerPath(coreController);
	const char* base = cCoreControllerBaseDirectory(coreController);
	int claimed = 0;
	size_t saveIndex = controller->claimedSavesCount;
	for (size_t i = 0; i < controller->claimedSavesCount; ++i) {
		if (strcmp(controller->claimedSaves[i].path, path) == 0 && strcmp(controller->claimedSaves[i].baseDirectory, base) == 0) {
			claimed = controller->claimedSaves[i].claimed;
			saveIndex = i;
			break;
		}
	}

	if (claimed) {
		player.saveId = 0;
		for (int i = 0; i < MAX_GBAS; ++i) {
			if (claimed & (1 << i)) continue;
			player.saveId = i + 1;
			break;
		}
		if (!player.saveId) {
			player.saveId = 1;
		}
	} else {
		player.saveId = 1;
	}

	if (saveIndex == controller->claimedSavesCount) {
		if (controller->claimedSavesCount >= controller->claimedSavesCapacity) {
			controller->claimedSavesCapacity = controller->claimedSavesCapacity ? controller->claimedSavesCapacity * 2 : 8;
			controller->claimedSaves = realloc(controller->claimedSaves, controller->claimedSavesCapacity * sizeof(ClaimedSave));
		}
		controller->claimedSaves[saveIndex].path = strdup(path);
		controller->claimedSaves[saveIndex].baseDirectory = strdup(base);
		controller->claimedSaves[saveIndex].claimed = 0;
		controller->claimedSavesCount++;
	}
	controller->claimedSaves[saveIndex].claimed |= 1 << (player.saveId - 1);

	// Add player
	if (controller->pidsCount >= controller->pidsCapacity) {
		controller->pidsCapacity = controller->pidsCapacity ? controller->pidsCapacity * 2 : 8;
		controller->pids = realloc(controller->pids, controller->pidsCapacity * sizeof(CPlayer));
	}
	controller->pids[controller->nextPid] = player;

	// Add to players
	if (controller->playersCount >= controller->playersCapacity) {
		controller->playersCapacity = controller->playersCapacity ? controller->playersCapacity * 2 : 8;
		controller->players = realloc(controller->players, controller->playersCapacity * sizeof(int));
	}
	controller->players[controller->playersCount] = controller->nextPid;
	controller->playersCount++;
	controller->pidsCount++;
	controller->nextPid++;

	if (controller->gameAttached) {
		controller->gameAttached(controller->gameAttachedUser);
	}
	return true;
}void cMultiplayerControllerDetachGame(CMultiplayerController* controller, CCoreController* coreController) {
	if (!controller->playersCount) {
		return;
	}
	struct mCoreThread* thread = cCoreControllerThread(coreController);
	if (!thread) {
		return;
	}
	int pid = -1;
	for (size_t i = 0; i < controller->playersCount; ++i) {
		CPlayer* p = cMultiplayerControllerPlayer(controller, i);
		if (p && p->controller == coreController) {
			pid = controller->players[i];
			break;
		}
	}
	if (pid < 0) {
		return;
	}

	CPlayer* p = &controller->pids[pid];
	if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
		coreController->core->setPeripheral(coreController->core, mPERIPH_GBA_LINK_PORT, NULL);
		GBASIOLockstepCoordinatorDetach(&controller->gbaCoordinator, p->driver.gbaDriver);
		free(p->driver.gbaDriver->user);
		free(p->driver.gbaDriver);
#endif
	} else {
#ifdef M_CORE_GB
		struct GB* gb = (struct GB*)thread->core->board;
		GBSIOSetDriver(&gb->sio, NULL);
		GBSIOLockstepDetachNode(&controller->gbLockstep, p->driver.gbNode);
		free(p->driver.gbNode);
#endif
	}

	// Remove from claimedSaves
	const char* path = cCoreControllerPath(coreController);
	const char* base = cCoreControllerBaseDirectory(coreController);
	for (size_t i = 0; i < controller->claimedSavesCount; ++i) {
		if (strcmp(controller->claimedSaves[i].path, path) == 0 && strcmp(controller->claimedSaves[i].baseDirectory, base) == 0) {
			controller->claimedSaves[i].claimed &= ~(1 << (p->saveId - 1));
			if (!controller->claimedSaves[i].claimed) {
				free(controller->claimedSaves[i].path);
				free(controller->claimedSaves[i].baseDirectory);
				for (size_t j = i; j < controller->claimedSavesCount - 1; ++j) {
					controller->claimedSaves[j] = controller->claimedSaves[j + 1];
				}
				controller->claimedSavesCount--;
			}
			break;
		}
	}

	if (p->preferredId >= 0) {
		controller->claimedIds &= ~(1 << p->preferredId);
	}

	// Remove from pids and players
	for (size_t i = pid; i < controller->pidsCount - 1; ++i) {
		controller->pids[i] = controller->pids[i + 1];
	}
	controller->pidsCount--;
	for (size_t i = 0; i < controller->playersCount; ++i) {
		if (controller->players[i] == pid) {
			for (size_t j = i; j < controller->playersCount - 1; ++j) {
				controller->players[j] = controller->players[j + 1];
			}
			controller->playersCount--;
			break;
		}
	}
	if (!controller->pidsCount) {
		if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
			GBASIOLockstepCoordinatorDeinit(&controller->gbaCoordinator);
#endif
		}
		controller->platform = mPLATFORM_NONE;
	}
	if (controller->gameDetached) {
		controller->gameDetached(controller->gameDetachedUser);
	}
}

int cMultiplayerControllerPlayerId(const CMultiplayerController* controller, CCoreController* coreController) {
	for (size_t i = 0; i < controller->playersCount; ++i) {
		const CPlayer* p = cMultiplayerControllerPlayerConst(controller, i);
		if (p && p->controller == coreController) {
			return i;
		}
	}
	return -1;
}

int cMultiplayerControllerSaveId(const CMultiplayerController* controller, CCoreController* coreController) {
	for (size_t i = 0; i < controller->playersCount; ++i) {
		const CPlayer* p = cMultiplayerControllerPlayerConst(controller, i);
		if (p && p->controller == coreController) {
			return p->saveId;
		}
	}
	return -1;
}

int cMultiplayerControllerAttached(const CMultiplayerController* controller) {
	switch (controller->platform) {
	case mPLATFORM_GBA:
#ifdef M_CORE_GBA
		return GBASIOLockstepCoordinatorAttached((struct GBASIOLockstepCoordinator*)&controller->gbaCoordinator);
#endif
	case mPLATFORM_GB:
#ifdef M_CORE_GB
		return controller->gbLockstep.d.attached;
#endif
	default:
		return 0;
	}
}

CPlayer* cMultiplayerControllerPlayer(CMultiplayerController* controller, int id) {
	if (id >= (int)controller->playersCount) {
		return NULL;
	}
	int pid = controller->players[id];
	for (size_t i = 0; i < controller->pidsCount; ++i) {
		if (&controller->pids[i] == &controller->pids[pid]) { // Simplified
			return &controller->pids[i];
		}
	}
	return NULL;
}

const CPlayer* cMultiplayerControllerPlayerConst(const CMultiplayerController* controller, int id) {
	return cMultiplayerControllerPlayer((CMultiplayerController*)controller, id);
}

int cPlayerId(const CPlayer* player) {
	switch (cCoreControllerPlatform(player->controller)) {
#ifdef M_CORE_GBA
	case mPLATFORM_GBA: {
		int id = player->driver.gbaDriver->d.deviceId(&player->driver.gbaDriver->d);
		if (id >= 0) {
			return id;
		} else {
			return player->preferredId;
		}
	}
#endif
#ifdef M_CORE_GB
	case mPLATFORM_GB:
		return player->driver.gbNode->id;
#endif
	default:
		return -1;
	}
}

bool cPlayerLess(const CPlayer* a, const CPlayer* b) {
	return cPlayerId(a) < cPlayerId(b);
}