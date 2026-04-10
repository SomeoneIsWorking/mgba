/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/transient-log.h>

#include <mgba/internal/arm/decoder.h>
#include <mgba/internal/arm/arm.h>
#ifdef ENABLE_DEBUGGERS
#include <mgba/debugger/debugger.h>
#include <mgba/core/core.h>
#include <mgba/internal/debugger/stack-trace.h>
#include <mgba/internal/debugger/symbols.h>
#include <mgba/core/cpu.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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

static void _appendText(char* dst, size_t dstSize, size_t* used, const char* text) {
	if (!dst || dstSize == 0 || !used || !text || *used >= dstSize) {
		return;
	}
	int written = snprintf(dst + *used, dstSize - *used, "%s", text);
	if (written < 0) {
		return;
	}
	size_t writtenSize = (size_t) written;
	if (writtenSize >= dstSize - *used) {
		*used = dstSize - 1;
		dst[dstSize - 1] = '\0';
		return;
	}
	*used += writtenSize;
}

static void _appendFormat(char* dst, size_t dstSize, size_t* used, const char* fmt, ...) {
	if (!dst || dstSize == 0 || !used || !fmt || *used >= dstSize) {
		return;
	}
	va_list args;
	va_start(args, fmt);
	int written = vsnprintf(dst + *used, dstSize - *used, fmt, args);
	va_end(args);
	if (written < 0) {
		return;
	}
	size_t writtenSize = (size_t) written;
	if (writtenSize >= dstSize - *used) {
		*used = dstSize - 1;
		dst[dstSize - 1] = '\0';
		return;
	}
	*used += writtenSize;
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

static const char* _mnemonicLabel(const struct ARMInstructionInfo* info) {
	switch (info->mnemonic) {
	case ARM_MN_STR:
		switch (info->memory.width) {
		case ARM_ACCESS_BYTE:
			return "strb";
		case ARM_ACCESS_HALFWORD:
			return "strh";
		default:
			return "str";
		}
	case ARM_MN_STM:
		if (info->execMode == MODE_THUMB && info->memory.baseReg == ARM_SP) {
			return "push";
		}
		return "stm";
	case ARM_MN_LDM:
		if (info->execMode == MODE_THUMB && info->memory.baseReg == ARM_SP) {
			return "pop";
		}
		return "ldm";
	case ARM_MN_SWP:
		return "swp";
	case ARM_MN_ADD:
		return "add";
	case ARM_MN_SUB:
		return "sub";
	case ARM_MN_MOV:
		return "mov";
	case ARM_MN_B:
		return "b";
	case ARM_MN_BL:
		return "bl";
	case ARM_MN_BX:
		return "bx";
	default:
		return "op";
	}
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

	snprintf(buffer, sizeof(buffer), "%s_%s_%0*X",
	         thumb ? "thumb" : "arm",
	         _mnemonicLabel(&info),
	         thumb ? 4 : 8,
	         opcode);
	_sanitizeOperationText(event->opText, sizeof(event->opText), buffer);
	event->hasOperation = event->opText[0] != '\0';
}

static void _describeStackTrace(struct ARMCore* cpu, struct GBATransientWriteEvent* event) {
	event->stackDepth = 0;
	event->hasStackTrace = false;
	event->stackText[0] = '\0';
#ifdef ENABLE_DEBUGGERS
	if (!cpu || !cpu->components) {
		return;
	}
	struct mCPUComponent* component = cpu->components[CPU_COMPONENT_DEBUGGER];
	if (!component || component->id != DEBUGGER_ID) {
		return;
	}
	struct mDebugger* debugger = (struct mDebugger*) component;
	if (!debugger->platform || debugger->platform->getStackTraceMode == NULL ||
	    debugger->platform->getStackTraceMode(debugger->platform) == STACK_TRACE_DISABLED) {
		return;
	}

	struct mStackTrace* stack = &debugger->stackTrace;
	size_t depth = mStackTraceGetDepth(stack);
	if (!depth) {
		return;
	}

	event->stackDepth = depth > UINT8_MAX ? UINT8_MAX : (uint8_t) depth;
	size_t used = 0;
	size_t frameLimit = depth < 6 ? depth : 6;
	struct mDebuggerSymbols* symbols = debugger->core ? debugger->core->symbolTable : NULL;
	for (size_t i = 0; i < frameLimit; ++i) {
		struct mStackFrame* frame = mStackTraceGetFrame(stack, (uint32_t) i);
		if (!frame) {
			break;
		}
		if (i > 0) {
			_appendText(event->stackText, sizeof(event->stackText), &used, "; ");
		}
		const char* entryName = symbols
			? mDebuggerSymbolReverseLookup(symbols, frame->entryAddress, frame->entrySegment)
			: NULL;
		if (entryName && entryName[0] != '\0') {
			_appendFormat(event->stackText, sizeof(event->stackText), &used,
			              "#%zu %s@0x%08X", i, entryName, frame->entryAddress);
		} else {
			_appendFormat(event->stackText, sizeof(event->stackText), &used,
			              "#%zu 0x%08X", i, frame->entryAddress);
		}
		if (frame->callAddress != 0) {
			_appendFormat(event->stackText, sizeof(event->stackText), &used,
			              "<=0x%08X", frame->callAddress);
		}
		if (frame->interrupt) {
			_appendText(event->stackText, sizeof(event->stackText), &used, "[irq]");
		}
	}
	if (depth > frameLimit) {
		_appendFormat(event->stackText, sizeof(event->stackText), &used,
		              "; ... +%zu more", depth - frameLimit);
	}
	event->hasStackTrace = event->stackText[0] != '\0';
#endif
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
	_describeStackTrace(cpu, event);
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
