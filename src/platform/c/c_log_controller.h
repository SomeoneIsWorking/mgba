/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <mgba/core/log.h>

#include <stdbool.h>

struct mLogFilter;

typedef struct CLogController CLogController;

typedef void (*LogPostedCallback)(int level, int category, const char* log, void* user);
typedef void (*LevelsChangedCallback)(int levels, void* user);

struct CLogController {
	struct mLogFilter* filter;
	int defaultLevels;
	bool logToFile;
	bool logToStdout;
	char* logFilePath;
	FILE* logFile;

	// Callbacks
	LogPostedCallback logPosted;
	void* logPostedUser;
	LevelsChangedCallback levelsSet;
	void* levelsSetUser;
	LevelsChangedCallback levelsEnabled;
	void* levelsEnabledUser;
	LevelsChangedCallback levelsDisabled;
	void* levelsDisabledUser;
};

CLogController* cLogControllerCreate(int levels);
void cLogControllerDestroy(CLogController* controller);

int cLogControllerLevels(const CLogController* controller);
int cLogControllerLevelsForCategory(const CLogController* controller, int category);
struct mLogFilter* cLogControllerFilter(CLogController* controller);

void cLogControllerPostLog(CLogController* controller, int level, int category, const char* string);
void cLogControllerSetLevels(CLogController* controller, int levels);
void cLogControllerEnableLevels(CLogController* controller, int levels);
void cLogControllerDisableLevels(CLogController* controller, int levels);
void cLogControllerSetLevelsForCategory(CLogController* controller, int levels, int category);
void cLogControllerEnableLevelsForCategory(CLogController* controller, int levels, int category);
void cLogControllerDisableLevelsForCategory(CLogController* controller, int levels, int category);
void cLogControllerClearLevelsForCategory(CLogController* controller, int category);

void cLogControllerLogToFile(CLogController* controller, bool enable);
void cLogControllerLogToStdout(CLogController* controller, bool enable);
void cLogControllerSetLogFile(CLogController* controller, const char* path);

char* cLogControllerToString(int level);
int cLogControllerCategoryId(const char* category);