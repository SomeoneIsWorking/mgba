#ifndef MULTIPLAYER_CONTROLLER_H
#define MULTIPLAYER_CONTROLLER_H

#include <mgba/core/core.h>
#include <mgba/core/lockstep.h>
#include <mgba-util/common.h>
#include "core_controller.h"

#ifdef M_CORE_GBA
#include <mgba/internal/gba/sio/lockstep.h>
#endif
#ifdef M_CORE_GB
#include <mgba/internal/gb/sio/lockstep.h>
#endif

#define MAX_PLAYERS 4

typedef struct MultiplayerController {
    struct mLockstep lockstep;
#ifdef M_CORE_GB
    struct GBSIOLockstep gbLockstep;
    struct GBSIOLockstepNode gbNodes[MAX_PLAYERS];
#endif
#ifdef M_CORE_GBA
    struct GBASIOLockstepCoordinator gbaCoordinator;
    struct GBASIOLockstepDriver gbaDrivers[MAX_PLAYERS];
#ifndef DISABLE_THREADING
    struct mLockstepThreadUser gbaUsers[MAX_PLAYERS];
#endif
#endif

    struct CoreController* players[MAX_PLAYERS];
    int attached;
    enum mPlatform platform;
    int nextPid;
} MultiplayerController;

void MultiplayerControllerInit(MultiplayerController* controller);
void MultiplayerControllerDeinit(MultiplayerController* controller);
bool MultiplayerControllerAttachGame(MultiplayerController* controller, struct CoreController* game);
void MultiplayerControllerDetachGame(MultiplayerController* controller, struct CoreController* game);

#endif
