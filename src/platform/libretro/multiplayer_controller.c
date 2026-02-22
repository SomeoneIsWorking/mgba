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

static void _lockstepLock(struct mLockstep* lockstep) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    MutexLock(&controller->lockstepMutex);
}

static void _lockstepUnlock(struct mLockstep* lockstep) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    MutexUnlock(&controller->lockstepMutex);
}

void MultiplayerControllerInit(MultiplayerController* controller) {
    memset(controller, 0, sizeof(*controller));
    mLockstepInit(&controller->lockstep);
    controller->lockstep.context = controller;
    controller->lockstep.lock = _lockstepLock;
    controller->lockstep.unlock = _lockstepUnlock;
    controller->platform = mPLATFORM_NONE;
    MutexInit(&controller->lockstepMutex);
}

void MultiplayerControllerDeinit(MultiplayerController* controller) {
    mLockstepDeinit(&controller->lockstep);
    if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
        GBASIOLockstepCoordinatorDeinit(&controller->gbaCoordinator);
#endif
    }
    MutexDeinit(&controller->lockstepMutex);
}

static void _onFrameDoneMult(struct mCoreThread* context) {
    mCoreThreadPauseFromThread(context);
}

bool MultiplayerControllerAttachGame(MultiplayerController* controller, struct CoreController* game) {
    if (controller->attached >= MAX_PLAYERS) {
        return false;
    }

    game->multiplayer = controller;
    game->threadContext.frameCallback = _onFrameDoneMult;

    if (controller->platform == mPLATFORM_NONE) {
        controller->platform = game->core->platform(game->core);
        if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
            GBASIOLockstepCoordinatorInit(&controller->gbaCoordinator);
#endif
        } else if (controller->platform == mPLATFORM_GB) {
#ifdef M_CORE_GB
            GBSIOLockstepInit(&controller->gbLockstep);
            controller->gbLockstep.d.lock = _lockstepLock;
            controller->gbLockstep.d.unlock = _lockstepUnlock;
            controller->gbLockstep.d.context = controller;
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

    game->multiplayer = NULL;

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

void MultiplayerControllerRunFrame(MultiplayerController* controller) {
    for (int i = 0; i < controller->attached; ++i) {
        struct CoreController* cc = controller->players[i];
        if (!cc) continue;
        struct mCoreThread* thread = &cc->threadContext;
        
        mCoreThreadUnpause(thread);
    }
}

void MultiplayerControllerWaitFrame(MultiplayerController* controller) {
    if (controller->attached == 0) return;

    for (int i = 0; i < controller->attached; ++i) {
        struct CoreController* cc = controller->players[i];
        if (!cc) continue;
        struct mCoreThread* thread = &cc->threadContext;

        MutexLock(&thread->impl->stateMutex);
        while (thread->impl->state != mTHREAD_PAUSED && mCoreThreadIsActive(thread)) {
            ConditionWait(&thread->impl->stateOffThreadCond, &thread->impl->stateMutex);
        }
        MutexUnlock(&thread->impl->stateMutex);
    }
}
