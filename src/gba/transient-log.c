/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/transient-log.h>

#include <stdlib.h>

struct GBATransientLogState {
	struct GBATransientWriteEvent* events;
	size_t count;
	size_t capacity;
	bool enabled;
};

static struct GBATransientLogState sTransientLog;

static bool _trackAddress(uint32_t addr) {
	if (addr >= 0x03000000 && addr < 0x03008000) {
		return true;
	}
	if (addr >= 0x05000000 && addr < 0x05000400) {
		return true;
	}
	if (addr >= 0x06000000 && addr < 0x06018000) {
		return true;
	}
	if (addr >= 0x07000000 && addr < 0x07000400) {
		return true;
	}
	return false;
}

static bool _reserve(size_t needed) {
	if (needed <= sTransientLog.capacity) {
		return true;
	}

	size_t capacity = sTransientLog.capacity ? sTransientLog.capacity * 2 : 1024;
	while (capacity < needed) {
		capacity *= 2;
	}

	struct GBATransientWriteEvent* events = realloc(sTransientLog.events,
			capacity * sizeof(*events));
	if (!events) {
		return false;
	}

	sTransientLog.events = events;
	sTransientLog.capacity = capacity;
	return true;
}

static void _record(uint32_t addr, uint32_t oldValue, uint32_t newValue, uint8_t width) {
	if (!sTransientLog.enabled || !_trackAddress(addr) || oldValue == newValue) {
		return;
	}

	if (!_reserve(sTransientLog.count + 1)) {
		return;
	}

	struct GBATransientWriteEvent* event = &sTransientLog.events[sTransientLog.count++];
	event->addr = addr;
	event->oldValue = oldValue;
	event->newValue = newValue;
	event->width = width;
}

void GBATransientLogEnable(bool enabled) {
	sTransientLog.enabled = enabled;
}

bool GBATransientLogIsEnabled(void) {
	return sTransientLog.enabled;
}

void GBATransientLogReset(void) {
	sTransientLog.count = 0;
}

void GBATransientLogRecord8(uint32_t addr, uint32_t oldValue, uint32_t newValue) {
	_record(addr, oldValue & 0xFF, newValue & 0xFF, 1);
}

void GBATransientLogRecord16(uint32_t addr, uint32_t oldValue, uint32_t newValue) {
	_record(addr, oldValue & 0xFFFF, newValue & 0xFFFF, 2);
}

void GBATransientLogRecord32(uint32_t addr, uint32_t oldValue, uint32_t newValue) {
	_record(addr, oldValue, newValue, 4);
}

size_t GBATransientLogCount(void) {
	return sTransientLog.count;
}

const struct GBATransientWriteEvent* GBATransientLogEvents(void) {
	return sTransientLog.events;
}