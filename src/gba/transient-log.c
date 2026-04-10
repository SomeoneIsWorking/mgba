/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/transient-log.h>

#include <mgba/internal/arm/decoder.h>
#include <mgba/internal/arm/arm.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

struct GBATransientLogState {
	struct GBATransientWriteEvent* events;
	size_t count;
	size_t capacity;
	bool enabled;
};

static struct GBATransientLogState sTransientLog;

static bool _trackAddress(uint32_t addr) {
	if (addr >= 0x02000000 && addr < 0x02040000) {
		return true;
	}
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

static uint32_t _callerPc(struct ARMCore* cpu) {
	if (!cpu) {
		return 0;
	}
	return cpu->gprs[ARM_PC] - (cpu->cpsr.t == MODE_ARM ? WORD_SIZE_ARM : WORD_SIZE_THUMB) * 2;
}

static void _sanitizeOperationText(char* dst, size_t dstSize, const char* src) {
	if (!dst || dstSize == 0) {
		return;
	}
	if (!src) {
		dst[0] = '\0';
		return;
	}

	size_t out = 0;
	for (size_t i = 0; src[i] != '\0' && out + 1 < dstSize; ++i) {
		unsigned char ch = (unsigned char) src[i];
		if (isspace(ch)) {
			if (out > 0 && dst[out - 1] != '_') {
				dst[out++] = '_';
			}
			continue;
		}
		dst[out++] = ch == '|' ? '!' : (char) ch;
	}
	while (out > 0 && dst[out - 1] == '_') {
		--out;
	}
	dst[out] = '\0';
}

static void _describeOperation(struct ARMCore* cpu, struct GBATransientWriteEvent* event) {
	event->opcode = 0;
	event->execMode = MODE_ARM;
	event->hasOperation = false;
	event->opText[0] = '\0';

	if (!cpu) {
		return;
	}

	uint32_t pc = _callerPc(cpu);
	bool thumb = cpu->cpsr.t == MODE_THUMB;
	uint32_t opcode = thumb
		? cpu->memory.load16(cpu, pc, NULL)
		: cpu->memory.load32(cpu, pc, NULL);
	struct ARMInstructionInfo info;
	char buffer[sizeof(event->opText)];

	event->opcode = opcode;
	event->execMode = thumb ? MODE_THUMB : MODE_ARM;
	if (thumb) {
		ARMDecodeThumb((uint16_t) opcode, &info);
	} else {
		ARMDecodeARM(opcode, &info);
	}

#ifdef ENABLE_DEBUGGERS
	if (ARMDisassemble(&info, cpu, NULL, pc, buffer, sizeof(buffer)) > 0) {
		_sanitizeOperationText(event->opText, sizeof(event->opText), buffer);
	} else
#endif
	{
		snprintf(buffer, sizeof(buffer), "%s_%0*X",
		         thumb ? "thumb" : "arm",
		         thumb ? 4 : 8,
		         opcode);
		_sanitizeOperationText(event->opText, sizeof(event->opText), buffer);
	}
	event->hasOperation = event->opText[0] != '\0';
}

static void _record(struct ARMCore* cpu, uint32_t addr, uint32_t oldValue, uint32_t newValue, uint8_t width) {
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
	event->pc = _callerPc(cpu);
	event->lr = cpu ? (uint32_t) cpu->gprs[ARM_LR] : 0;
	event->width = width;
	_describeOperation(cpu, event);
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

void GBATransientLogRecord8(struct ARMCore* cpu, uint32_t addr, uint32_t oldValue, uint32_t newValue) {
	_record(cpu, addr, oldValue & 0xFF, newValue & 0xFF, 1);
}

void GBATransientLogRecord16(struct ARMCore* cpu, uint32_t addr, uint32_t oldValue, uint32_t newValue) {
	_record(cpu, addr, oldValue & 0xFFFF, newValue & 0xFFFF, 2);
}

void GBATransientLogRecord32(struct ARMCore* cpu, uint32_t addr, uint32_t oldValue, uint32_t newValue) {
	_record(cpu, addr, oldValue, newValue, 4);
}

size_t GBATransientLogCount(void) {
	return sTransientLog.count;
}

const struct GBATransientWriteEvent* GBATransientLogEvents(void) {
	return sTransientLog.events;
}
