#include "core_controller.h"
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>
#include <string.h>
#include <stdlib.h>

void CoreControllerInit(struct CoreController* controller, struct mCore* core) {
    memset(controller, 0, sizeof(*controller));
    controller->core = core;
    controller->threadContext.core = core;
    controller->threadContext.userData = controller;
    controller->hasStarted = false;
    controller->multiplayer = NULL;
}

void CoreControllerDeinit(struct CoreController* controller) {
    if (!controller) {
        return;
    }
    controller->hasStarted = false;
    controller->multiplayer = NULL;
    controller->core = NULL;
}

void CoreControllerSetPath(struct CoreController* controller, const char* path) {
    if (!path) {
        controller->path[0] = '\0';
        return;
    }
    strncpy(controller->path, path, sizeof(controller->path) - 1);
    controller->path[sizeof(controller->path) - 1] = '\0';
}

void CoreControllerStart(struct CoreController* controller) {
    if (!controller || !controller->core || controller->hasStarted) {
        return;
    }
    mCoreThreadStart(&controller->threadContext);
    controller->hasStarted = true;
}

void CoreControllerStop(struct CoreController* controller) {
    if (!controller || !controller->hasStarted) {
        return;
    }
    mCoreThreadEnd(&controller->threadContext);
    mCoreThreadJoin(&controller->threadContext);
    controller->hasStarted = false;
}

bool CoreControllerIsPaused(const struct CoreController* controller) {
    if (!controller || !controller->hasStarted) {
        return false;
    }
    return mCoreThreadIsPaused((struct mCoreThread*) &controller->threadContext);
}

bool CoreControllerHasStarted(const struct CoreController* controller) {
    return controller && controller->hasStarted;
}

void CoreControllerSetMultiplayer(struct CoreController* controller, struct MultiplayerController* multiplayer) {
    if (!controller) {
        return;
    }
    controller->multiplayer = multiplayer;
}

void CoreControllerClearMultiplayer(struct CoreController* controller) {
    if (!controller) {
        return;
    }
    controller->multiplayer = NULL;
}
