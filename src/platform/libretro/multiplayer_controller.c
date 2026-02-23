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

static void _onFrameDoneMult(struct mCoreThread* context) {
	mCoreThreadPauseFromThread(context);
}

static void _lockstepLock(struct mLockstep* lockstep) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    MutexLock(&controller->lock);
}

static void _lockstepUnlock(struct mLockstep* lockstep) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    MutexUnlock(&controller->lock);
}

static int _requestedId(struct mLockstepUser* ctx) {
    struct mLockstepThreadUser* tctx = (struct mLockstepThreadUser*) ctx;
    struct mCoreThread* thread = tctx->thread;
    struct CoreController* cc = (struct CoreController*) thread->userData;
    MultiplayerController* mc = cc->multiplayer;
    if (!mc) {
        return -1;
    }
    for (int i = 0; i < mc->nPlayers; ++i) {
        if (mc->players[i].controller == cc) {
            return mc->players[i].preferredId;
        }
    }
    return -1;
}

static bool _lockstepSignal(struct mLockstep* lockstep, unsigned mask) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    if (controller->nPlayers < 1 || !controller->players[0].controller) {
        return false;
    }

    MultiplayerPlayer* player = &controller->players[0];
    bool woke = false;
    player->waitMask &= ~mask;
    if (!player->waitMask && player->awake < 1) {
        mCoreThreadStopWaiting(&player->controller->threadContext);
        player->awake = 1;
        woke = true;
    }
    return woke;
}

static bool _lockstepWait(struct mLockstep* lockstep, unsigned mask) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    if (controller->nPlayers < 1 || !controller->players[0].controller) {
        return false;
    }

    MultiplayerPlayer* player = &controller->players[0];
    bool slept = false;
    player->waitMask |= mask;
    if (player->awake > 0) {
        mCoreThreadWaitFromThread(&player->controller->threadContext);
        player->awake = 0;
        slept = true;
    }
    return slept;
}

static void _lockstepAddCycles(struct mLockstep* lockstep, int id, int32_t cycles) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    if (cycles < 0 || id < 0 || id >= controller->nPlayers) {
        return;
    }

    MultiplayerPlayer* player = &controller->players[id];
    switch (controller->platform) {
#ifdef M_CORE_GBA
    case mPLATFORM_GBA:
        break;
#endif
#ifdef M_CORE_GB
    case mPLATFORM_GB:
        if (!id && controller->nPlayers > 1) {
            player = &controller->players[1];
            player->cyclesPosted += cycles;
            if (player->awake < 1 && player->controller) {
                mCoreThreadStopWaiting(&player->controller->threadContext);
                player->awake = 1;
            }
        } else {
            player->cyclesPosted += cycles;
        }
        break;
#endif
    default:
        break;
    }
}

static int32_t _lockstepUseCycles(struct mLockstep* lockstep, int id, int32_t cycles) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    if (id < 0 || id >= controller->nPlayers || !controller->players[id].controller) {
        return 0;
    }

    MultiplayerPlayer* player = &controller->players[id];
    player->cyclesPosted -= cycles;
    if (player->cyclesPosted <= 0) {
        mCoreThreadWaitFromThread(&player->controller->threadContext);
        player->awake = 0;
    }
    return player->cyclesPosted;
}

static int32_t _lockstepUnusedCycles(struct mLockstep* lockstep, int id) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    if (id < 0 || id >= controller->nPlayers) {
        return 0;
    }
    return controller->players[id].cyclesPosted;
}

static void _lockstepUnload(struct mLockstep* lockstep, int id) {
    MultiplayerController* controller = (MultiplayerController*) lockstep->context;
    if (controller->nPlayers < 1) {
        return;
    }

    if (id > 0 && id < controller->nPlayers) {
        MultiplayerPlayer* player = &controller->players[id];
        player->cyclesPosted = 0;

        MultiplayerPlayer* master = &controller->players[0];
        master->waitMask &= ~(1U << id);
        if (!master->waitMask && master->awake < 1 && master->controller) {
            mCoreThreadStopWaiting(&master->controller->threadContext);
            master->awake = 1;
        }
        return;
    }

    if (id == 0) {
        for (int i = 1; i < controller->nPlayers; ++i) {
            MultiplayerPlayer* player = &controller->players[i];
            player->cyclesPosted = 0;
            if (player->awake < 1 && player->controller) {
                mCoreThreadStopWaiting(&player->controller->threadContext);
                player->awake = 1;
            }
        }
    }
}

void MultiplayerControllerInit(MultiplayerController* controller) {
    memset(controller, 0, sizeof(*controller));
    mLockstepInit(&controller->lockstep);
    controller->lockstep.context = controller;
    controller->lockstep.lock = _lockstepLock;
    controller->lockstep.unlock = _lockstepUnlock;
    controller->lockstep.signal = _lockstepSignal;
    controller->lockstep.wait = _lockstepWait;
    controller->lockstep.addCycles = _lockstepAddCycles;
    controller->lockstep.useCycles = _lockstepUseCycles;
    controller->lockstep.unusedCycles = _lockstepUnusedCycles;
    controller->lockstep.unload = _lockstepUnload;
    controller->platform = mPLATFORM_NONE;
    MutexInit(&controller->lock);
}

void MultiplayerControllerDeinit(MultiplayerController* controller) {
    mLockstepDeinit(&controller->lockstep);
    if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
        GBASIOLockstepCoordinatorDeinit(&controller->gbaCoordinator);
#endif
    }
    MutexDeinit(&controller->lock);
}

bool MultiplayerControllerAttachGame(MultiplayerController* controller, struct CoreController* game) {
    if (controller->nPlayers >= MAX_PLAYERS) {
        return false;
    }

    CoreControllerSetMultiplayer(game, controller);
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

    int pid = controller->nPlayers++;
    MultiplayerPlayer* player = &controller->players[pid];
    player->controller = game;
    player->awake = 1;
    player->waitMask = 0;
    player->cyclesPosted = 0;
    player->attached = false;

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (!(controller->claimedIds & (1 << i))) {
            player->preferredId = i;
            controller->claimedIds |= 1 << i;
            break;
        }
    }

    if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
        mLockstepThreadUserInit(&player->gbaUser, &game->threadContext);
        player->gbaUser.d.requestedId = _requestedId;
        GBASIOLockstepDriverCreate(&player->gbaDriver, &player->gbaUser.d);

        GBASIOLockstepCoordinatorAttach(&controller->gbaCoordinator, &player->gbaDriver);
        game->core->setPeripheral(game->core, mPERIPH_GBA_LINK_PORT, &player->gbaDriver.d);
        player->attached = true;
#endif
    } else if (controller->platform == mPLATFORM_GB) {
#ifdef M_CORE_GB
        struct GB* gb = (struct GB*) game->core->board;
        GBSIOLockstepNodeCreate(&player->gbNode);
        GBSIOLockstepAttachNode(&controller->gbLockstep, &player->gbNode);
        GBSIOSetDriver(&gb->sio, &player->gbNode.d);
        player->attached = true;
#endif
    }

    return true;
}

void MultiplayerControllerDetachGame(MultiplayerController* controller, struct CoreController* game) {
    int pid = -1;
    for (int i = 0; i < controller->nPlayers; ++i) {
        if (controller->players[i].controller == game) {
            pid = i;
            break;
        }
    }

    if (pid == -1) {
        return;
    }

    MultiplayerPlayer* player = &controller->players[pid];
    if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
        if (player->attached) {
            GBASIOLockstepCoordinatorDetach(&controller->gbaCoordinator, &player->gbaDriver);
            game->core->setPeripheral(game->core, mPERIPH_GBA_LINK_PORT, NULL);
            player->attached = false;
        }
#endif
    } else if (controller->platform == mPLATFORM_GB) {
#ifdef M_CORE_GB
        struct GB* gb = (struct GB*) game->core->board;
        GBSIOSetDriver(&gb->sio, NULL);
        GBSIOLockstepDetachNode(&controller->gbLockstep, &player->gbNode);
        player->attached = false;
#endif
    }

    controller->claimedIds &= ~(1 << player->preferredId);
    CoreControllerClearMultiplayer(game);
    game->threadContext.frameCallback = NULL;

    // Shift players down
    for (int i = pid; i < controller->nPlayers - 1; ++i) {
        controller->players[i] = controller->players[i + 1];
    }
    controller->nPlayers--;

    if (controller->nPlayers == 0) {
        if (controller->platform == mPLATFORM_GBA) {
#ifdef M_CORE_GBA
            GBASIOLockstepCoordinatorDeinit(&controller->gbaCoordinator);
#endif
        }
        controller->platform = mPLATFORM_NONE;
    }
}

void MultiplayerControllerRunFrame(MultiplayerController* controller) {
    for (int i = 0; i < controller->nPlayers; ++i) {
        MultiplayerPlayer* player = &controller->players[i];
        if (!player->controller) {
            continue;
        }
        struct mCoreThread* thread = &player->controller->threadContext;
        mCoreThreadUnpause(thread);
    }
}

void MultiplayerControllerWaitFrame(MultiplayerController* controller) {
    if (controller->nPlayers == 0) {
        return;
    }

    for (int i = 0; i < controller->nPlayers; ++i) {
        MultiplayerPlayer* player = &controller->players[i];
        if (!player->controller) {
            continue;
        }

        struct mCoreThread* thread = &player->controller->threadContext;
        MutexLock(&thread->impl->stateMutex);
        while (thread->impl->state != mTHREAD_PAUSED && mCoreThreadIsActive(thread)) {
            ConditionWait(&thread->impl->stateOffThreadCond, &thread->impl->stateMutex);
        }
        MutexUnlock(&thread->impl->stateMutex);
    }
}
