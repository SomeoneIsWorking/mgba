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
    
    // Minimal version from Qt:
    controller->isPaused = false;
    controller->hasStarted = false;
    
    // Qt version adds a lot of logging, etc. but we'll use libretro callbacks directly later.
}

void CoreControllerDeinit(struct CoreController* controller) {
    if (controller->core) {
        controller->core->deinit(controller->core);
    }
}

void CoreControllerSetPath(struct CoreController* controller, const char* path) {
    strncpy(controller->path, path, sizeof(controller->path) - 1);
}

void CoreControllerStart(struct CoreController* controller) {
    if (controller->hasStarted) {
        return;
    }
    mCoreThreadStart(&controller->threadContext);
    controller->hasStarted = true;
}

void CoreControllerStop(struct CoreController* controller) {
    if (!controller->hasStarted) {
        return;
    }
    mCoreThreadInterrupt(&controller->threadContext);
    mCoreThreadJoin(&controller->threadContext);
    controller->hasStarted = false;
}
