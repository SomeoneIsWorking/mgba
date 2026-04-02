/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MGBA_INTERNAL_GBA_TRANSIENT_LOG_H
#define MGBA_INTERNAL_GBA_TRANSIENT_LOG_H

#include <mgba-util/common.h>

CXX_GUARD_START

struct GBATransientWriteEvent {
	uint32_t addr;
	uint32_t oldValue;
	uint32_t newValue;
	uint8_t width;
};

void GBATransientLogEnable(bool enabled);
bool GBATransientLogIsEnabled(void);
void GBATransientLogReset(void);
void GBATransientLogRecord8(uint32_t addr, uint32_t oldValue, uint32_t newValue);
void GBATransientLogRecord16(uint32_t addr, uint32_t oldValue, uint32_t newValue);
void GBATransientLogRecord32(uint32_t addr, uint32_t oldValue, uint32_t newValue);
size_t GBATransientLogCount(void);
const struct GBATransientWriteEvent* GBATransientLogEvents(void);

CXX_GUARD_END

#endif