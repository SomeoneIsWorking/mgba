/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to thvoid cLogControllerSetLevelsForCategory(CLogController* controller, int levels, int category) {
	mLogFilterSet(controller->filter, "category", levels);
}erms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "c_log_controller.h"

#include <mgba/core/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CLogController* s_global = NULL;

CLogController* cLogControllerCreate(int levels) {
	CLogController* controller = calloc(1, sizeof(CLogController));
	controller->filter = calloc(1, sizeof(struct mLogFilter));
	mLogFilterInit(controller->filter);
	controller->filter->defaultLevels = levels;
	controller->defaultLevels = levels;
	return controller;
}

void cLogControllerDestroy(CLogController* controller) {
	if (controller->logFile) {
		fclose(controller->logFile);
	}
	free(controller->logFilePath);
	free(controller->filter);
	free(controller);
}

int cLogControllerLevels(const CLogController* controller) {
	return controller->filter->defaultLevels;
}

int cLogControllerLevelsForCategory(const CLogController* controller, int category) {
	return mLogFilterLevels(controller->filter, category);
}

struct mLogFilter* cLogControllerFilter(CLogController* controller) {
	return controller->filter;
}

void cLogControllerPostLog(CLogController* controller, int level, int category, const char* string) {
	if (controller->logToStdout) {
		printf("[%s] %s\n", cLogControllerToString(level), string);
	}
	if (controller->logToFile && controller->logFile) {
		fprintf(controller->logFile, "[%s] %s\n", cLogControllerToString(level), string);
		fflush(controller->logFile);
	}
	if (controller->logPosted) {
		controller->logPosted(level, category, string, controller->logPostedUser);
	}
}

void cLogControllerSetLevels(CLogController* controller, int levels) {
	controller->filter->defaultLevels = levels;
	if (controller->levelsSet) {
		controller->levelsSet(levels, controller->levelsSetUser);
	}
}

void cLogControllerEnableLevels(CLogController* controller, int levels) {
	controller->filter->defaultLevels |= levels;
	if (controller->levelsEnabled) {
		controller->levelsEnabled(levels, controller->levelsEnabledUser);
	}
}

void cLogControllerDisableLevels(CLogController* controller, int levels) {
	controller->filter->defaultLevels &= ~levels;
	if (controller->levelsDisabled) {
		controller->levelsDisabled(levels, controller->levelsDisabledUser);
	}
}

void cLogControllerSetLevelsForCategory(CLogController* controller, int levels, int category) {
	mLogFilterSet(controller->filter, "category", levels);
}

void cLogControllerEnableLevelsForCategory(CLogController* controller, int levels, int category) {
	int current = mLogFilterLevels(controller->filter, category);
	mLogFilterSet(controller->filter, "category", current | levels); // Simplified
}

void cLogControllerDisableLevelsForCategory(CLogController* controller, int levels, int category) {
	int current = mLogFilterLevels(controller->filter, category);
	mLogFilterSet(controller->filter, "category", current & ~levels);
}

void cLogControllerClearLevelsForCategory(CLogController* controller, int category) {
	mLogFilterReset(controller->filter, "category");
}

void cLogControllerLogToFile(CLogController* controller, bool enable) {
	controller->logToFile = enable;
	if (enable && !controller->logFile && controller->logFilePath) {
		controller->logFile = fopen(controller->logFilePath, "a");
	} else if (!enable && controller->logFile) {
		fclose(controller->logFile);
		controller->logFile = NULL;
	}
}

void cLogControllerLogToStdout(CLogController* controller, bool enable) {
	controller->logToStdout = enable;
}

void cLogControllerSetLogFile(CLogController* controller, const char* path) {
	free(controller->logFilePath);
	controller->logFilePath = strdup(path);
	if (controller->logToFile) {
		if (controller->logFile) {
			fclose(controller->logFile);
		}
		controller->logFile = fopen(path, "a");
	}
}

char* cLogControllerToString(int level) {
	switch (level) {
	case mLOG_DEBUG:
		return "DEBUG";
	case mLOG_INFO:
		return "INFO";
	case mLOG_WARN:
		return "WARN";
	case mLOG_ERROR:
		return "ERROR";
	case mLOG_FATAL:
		return "FATAL";
	case mLOG_GAME_ERROR:
		return "GAME_ERROR";
	case mLOG_STUB:
		return "STUB";
	default:
		return "UNKNOWN";
	}
}

int cLogControllerCategoryId(const char* category) {
	// Simplified: return 0 for all
	return 0;
}

CLogController* cLogControllerGlobal() {
	if (!s_global) {
		s_global = cLogControllerCreate(mLOG_ALL);
	}
	return s_global;
}