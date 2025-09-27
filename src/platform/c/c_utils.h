/* Copyright (c) 2013-2017 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <mgba/core/core.h>
#include <mgba-util/socket.h>
#include <mgba-util/vfs.h>

#include <stdbool.h>
#include <stddef.h>

struct VDir;
struct VDirEntry;

enum Endian {
	NONE    = 0b00,
	BIG     = 0b01,
	LITTLE  = 0b10,
	UNKNOWN = 0b11
};

char* niceSizeFormat(size_t filesize);
char* nicePlatformFormat(enum mPlatform platform, int validModels);

bool convertAddress(const char* input, struct Address* output);

void lockAspectRatio(int refWidth, int refHeight, int* width, int* height);
void lockIntegerScaling(int refWidth, int refHeight, int* width, int* height);
void clampSize(int refWidth, int refHeight, int sizeWidth, int sizeHeight, bool aspectRatio, bool integerScaling, int* outX, int* outY, int* outWidth, int* outHeight);

int clamp(int v, int lo, int hi);
int saturateCastInt(int value);
unsigned saturateCastUnsigned(int value);

char* romFilters(bool includeMvl, enum mPlatform platform, bool rawOnly);
bool extractMatchingFile(struct VDir* dir, char* (*filter)(struct VDirEntry*));

char* keyName(int key);

typedef struct {
	int left;
	int right;
} Span;

typedef struct {
	Span* spans;
	size_t count;
	size_t capacity;
} SpanSet;

void spanSetInit(SpanSet* set);
void spanSetDeinit(SpanSet* set);
void spanSetAdd(SpanSet* set, int pos);
void spanSetMerge(SpanSet* set);
void spanSetSort(SpanSet* set, bool reverse);