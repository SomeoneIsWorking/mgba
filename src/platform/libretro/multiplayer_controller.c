#include "multiplayer_controller.h"
#include <mgba/core/core.h>
#include <mgba/core/lockstep.h>
#include <mgba/core/thread.h>
#include <stdlib.h>
#include <string.h>

#ifdef M_CORE_GBA
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio.h>
#include <mgba/internal/gba/sio/lockstep.h>
#endif
#ifdef M_CORE_GB
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/sio.h>
#include <mgba/internal/gb/sio/lockstep.h>
#endif

void MultiplayerControllerInit(MultiplayerController* controller) {
    memset(controller, 0, sizeof(*controller));
    mLockstepInit(&controller->lockstep);
    controller->platform = mPLATFORM_NONE;
}

void MultiplayerControllerDeinit(MultiplayerController* controller) {
    mLockstepDeinit(&controller->lockstep);
    if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
        GBASIOLockstepCoordinatorDeinit(&controller->gbaCoordinator);
#endif
    }
}

bool MultiplayerControllerAttachGame(MultiplayerController* controller, struct CoreController* game) {
    if (controller->attached >= MAX_PLAYERS) {
        return false;
    }

    if (controller->platform == mPLATFORM_NONE) {
        controller->platform = game->core->platform(game->core);
        if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
            GBASIOLockstepCoordinatorInit(&controller->gbaCoordinator);
#endif
        } else if (controller->platform == mPLATFORM_GB) {
#ifdef M_CORE_GB
            GBSIOLockstepInit(&controller->gbLockstep);
#endif
        }
    } else if (controller->platform != game->core->platform(game->core)) {
        return false;
    }

    int pid = controller->attached;
    if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
        struct GBA* gba = (struct GBA*) game->core->board;
        struct GBASIOLockstepDriver* driver = &controller->gbaDrivers[pid];
#ifndef DISABLE_THREADING
        mLockstepThreadUserInit(&controller->gbaUsers[pid], &game->threadContext);
        GBASIOLockstepDriverCreate(driver, &controller->gbaUsers[pid].d);
#else
        static struct mLockstepUser dummyUser = {0};
        GBASIOLockstepDriverCreate(driver, &dummyUser);
#endif
        GBASIOLockstepCoordinatorAttach(&controller->gbaCoordinator, driver);
        GBASIOSetDriver(&gba->sio, &driver->d);
#endif
    } else if (controller->platform == mPLATFORM_GB) {
#ifdef M_CORE_GB
        struct GB* gb = (struct GB*) game->core->board;
        struct GBSIOLockstepNode* node = &controller->gbNodes[pid];
        GBSIOLockstepNodeCreate(node);
        GBSIOLockstepAttachNode(&controller->gbLockstep, node);
        GBSIOSetDriver(&gb->sio, &node->d);
#endif
    }

    controller->players[pid] = game;
    controller->attached++;
    return true;
}

void MultiplayerControllerDetachGame(MultiplayerController* controller, struct CoreController* game) {
    int pid = -1;
    for (int i = 0; i < controller->attached; ++i) {
        if (controller->players[i] == game) {
            pid = i;
            break;
        }
    }

    if (pid == -1) {
        return;
    }

    if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
        struct GBASIOLockstepDriver* driver = &controller->gbaDrivers[pid];
        GBASIOLockstepCoordinatorDetach(&controller->gbaCoordinator, driver);
#endif
    } else if (controller->platform == mPLATFORM_GB) {
#ifdef M_CORE_GB
        struct GBSIOLockstepNode* node = &controller->gbNodes[pid];
        GBSIOLockstepDetachNode(&controller->gbLockstep, node);
#endif
    }

    controller->players[pid] = NULL;
    // We don't shift players here because pid corresponds to indices in gbaDrivers/gbNodes.
}
