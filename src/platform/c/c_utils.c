/* Copyright (c) 2013-2017 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "c_utils.h"

#include <mgba-util/vfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

char* niceSizeFormat(size_t filesize) {
	char* buffer = malloc(16);
	if (filesize >= 1024 * 1024 * 1024) {
		snprintf(buffer, 16, "%.1f GiB", filesize / (1024.0 * 1024.0 * 1024.0));
	} else if (filesize >= 1024 * 1024) {
		snprintf(buffer, 16, "%.1f MiB", filesize / (1024.0 * 1024.0));
	} else if (filesize >= 1024) {
		snprintf(buffer, 16, "%.1f KiB", filesize / 1024.0);
	} else {
		snprintf(buffer, 16, "%zu B", filesize);
	}
	return buffer;
}

char* nicePlatformFormat(enum mPlatform platform, int validModels) {
	char* buffer = malloc(32);
	switch (platform) {
	case mPLATFORM_GBA:
		strcpy(buffer, "Game Boy Advance");
		break;
	case mPLATFORM_GB:
		if (validModels & 0x80) { // GB_MODEL_CGB
			strcpy(buffer, "Game Boy Color");
		} else if (validModels & 0x20) { // GB_MODEL_SGB
			strcpy(buffer, "Super Game Boy");
		} else {
			strcpy(buffer, "Game Boy");
		}
		break;
	default:
		strcpy(buffer, "Unknown");
		break;
	}
	return buffer;
}

bool convertAddress(const char* input, struct Address* output) {
	// Simplified: assume IPv4
	int a, b, c, d;
	if (sscanf(input, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
		output->version = IPV4;
		output->ipv4 = (a << 24) | (b << 16) | (c << 8) | d;
		return true;
	}
	return false;
}

void lockAspectRatio(int refWidth, int refHeight, int* width, int* height) {
	if (*width * refHeight > *height * refWidth) {
		*width = *height * refWidth / refHeight;
	} else if (*width * refHeight < *height * refWidth) {
		*height = *width * refHeight / refWidth;
	}
}

void lockIntegerScaling(int refWidth, int refHeight, int* width, int* height) {
	if (*width >= refWidth) {
		*width = *width - (*width % refWidth);
	}
	if (*height >= refHeight) {
		*height = *height - (*height % refHeight);
	}
}

void clampSize(int refWidth, int refHeight, int sizeWidth, int sizeHeight, bool aspectRatio, bool integerScaling, int* outX, int* outY, int* outWidth, int* outHeight) {
	int dsWidth = sizeWidth;
	int dsHeight = sizeHeight;
	if (aspectRatio) {
		lockAspectRatio(refWidth, refHeight, &dsWidth, &dsHeight);
	}
	if (integerScaling) {
		lockIntegerScaling(refWidth, refHeight, &dsWidth, &dsHeight);
	}
	*outX = (sizeWidth - dsWidth) / 2;
	*outY = (sizeHeight - dsHeight) / 2;
	*outWidth = dsWidth;
	*outHeight = dsHeight;
}

int clamp(int v, int lo, int hi) {
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

int saturateCastInt(int value) {
	if (value > INT_MAX) return INT_MAX;
	if (value < INT_MIN) return INT_MIN;
	return value;
}

unsigned saturateCastUnsigned(int value) {
	if (value < 0) return 0;
	return (unsigned) value;
}

char* romFilters(bool includeMvl, enum mPlatform platform, bool rawOnly) {
	// Simplified implementation
	char* filter = malloc(256);
	strcpy(filter, "ROM files (*.gba *.gb *.gbc");
	if (includeMvl) {
		strcat(filter, " *.mvl");
	}
	if (!rawOnly) {
		strcat(filter, " *.zip *.7z");
	}
	strcat(filter, ");;All files (*)");
	return filter;
}

bool extractMatchingFile(struct VDir* dir, char* (*filter)(struct VDirEntry*)) {
	// Simplified: not implemented
	return false;
}

char* keyName(int key) {
	char* buffer = malloc(16);
	snprintf(buffer, 16, "Key %d", key);
	return buffer;
}

void spanSetInit(SpanSet* set) {
	set->spans = NULL;
	set->count = 0;
	set->capacity = 0;
}

void spanSetDeinit(SpanSet* set) {
	free(set->spans);
	set->spans = NULL;
	set->count = 0;
	set->capacity = 0;
}

void spanSetAdd(SpanSet* set, int pos) {
	if (set->count >= set->capacity) {
		set->capacity = set->capacity ? set->capacity * 2 : 8;
		set->spans = realloc(set->spans, set->capacity * sizeof(Span));
	}
	set->spans[set->count].left = pos;
	set->spans[set->count].right = pos;
	set->count++;
}

static int spanCompare(const void* a, const void* b) {
	const Span* sa = a;
	const Span* sb = b;
	return sa->left - sb->left;
}

static int spanCompareReverse(const void* a, const void* b) {
	const Span* sa = a;
	const Span* sb = b;
	return sb->left - sa->left;
}

void spanSetSort(SpanSet* set, bool reverse) {
	if (reverse) {
		qsort(set->spans, set->count, sizeof(Span), spanCompareReverse);
	} else {
		qsort(set->spans, set->count, sizeof(Span), spanCompare);
	}
}

void spanSetMerge(SpanSet* set) {
	if (set->count < 2) return;
	spanSetSort(set, false);
	size_t i = 0;
	for (size_t j = 1; j < set->count; ++j) {
		if (set->spans[i].right + 1 >= set->spans[j].left) {
			set->spans[i].right = set->spans[j].right;
		} else {
			++i;
			set->spans[i] = set->spans[j];
		}
	}
	set->count = i + 1;
}