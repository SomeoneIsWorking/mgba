#ifndef CORE_CONTROLLER_H
#define CORE_CONTROLLER_H

#include <mgba/core/core.h>
#include <mgba/core/thread.h>

typedef struct CoreController {
    struct mCoreThread threadContext;
    struct mCore* core;
    char path[256];
    char savePath[256];
} CoreController;

CoreController* CoreController_create(struct mCore* core);
void CoreController_destroy(CoreController* controller);
void CoreController_setPath(CoreController* controller, const char* path);
bool CoreController_isPaused(CoreController* controller);

#endif
