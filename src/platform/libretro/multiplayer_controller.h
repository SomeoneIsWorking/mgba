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

typedef struct MultiplayerPlayer {
    struct CoreController* controller;
    int preferredId;
    bool attached;
    int awake;
    unsigned waitMask;
    int32_t cyclesPosted;
#ifdef M_CORE_GBA
    struct mLockstepThreadUser gbaUser;
    struct GBASIOLockstepDriver gbaDriver;
#endif
#ifdef M_CORE_GB
    struct GBSIOLockstepNode gbNode;
#endif
} MultiplayerPlayer;

typedef struct MultiplayerController {
    struct mLockstep lockstep;
    Mutex lock;
#ifdef M_CORE_GB
    struct GBSIOLockstep gbLockstep;
#endif
#ifdef M_CORE_GBA
    struct GBASIOLockstepCoordinator gbaCoordinator;
#endif

    MultiplayerPlayer players[MAX_PLAYERS];
    int nPlayers;
    enum mPlatform platform;
    uint32_t claimedIds;
} MultiplayerController;

void MultiplayerControllerInit(MultiplayerController* controller);
void MultiplayerControllerDeinit(MultiplayerController* controller);
bool MultiplayerControllerAttachGame(MultiplayerController* controller, struct CoreController* game);
void MultiplayerControllerDetachGame(MultiplayerController* controller, struct CoreController* game);

void MultiplayerControllerRunFrame(MultiplayerController* controller);
void MultiplayerControllerWaitFrame(MultiplayerController* controller);

#endif
