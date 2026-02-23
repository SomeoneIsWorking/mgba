#ifndef CORE_CONTROLLER_H
#define CORE_CONTROLLER_H

#include <mgba/core/core.h>
#include <mgba/core/thread.h>
#include <mgba-util/common.h>

struct CoreController {
    struct mCoreThread threadContext;
    struct mCore* core;
    char path[256];
    char savePath[256];
    bool hasStarted;
    struct MultiplayerController* multiplayer;
};

void CoreControllerInit(struct CoreController* controller, struct mCore* core);
void CoreControllerDeinit(struct CoreController* controller);
void CoreControllerSetPath(struct CoreController* controller, const char* path);
void CoreControllerStart(struct CoreController* controller);
void CoreControllerStop(struct CoreController* controller);
bool CoreControllerIsPaused(const struct CoreController* controller);
bool CoreControllerHasStarted(const struct CoreController* controller);
void CoreControllerSetMultiplayer(struct CoreController* controller, struct MultiplayerController* multiplayer);
void CoreControllerClearMultiplayer(struct CoreController* controller);

#endif
