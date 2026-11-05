#include "libretro_netplay.h"

#include <mgba/core/core.h>

#include "libretro_log.h"

void mLibretroNetplayInit(struct mLibretroNetplayState* state) {
	if (!state) {
		return;
	}

	state->rollbackContextActive = false;
}

// A cartridge's clock reads the host's time, which no two machines share: in a
// session it runs from the agreed time with the emulated frames, in the agreed zone.
void mLibretroNetplayApplyClock(struct mCore* core, retro_environment_t environCallback) {
	unsigned localClientIndex = 0;
	struct retro_netplay_clock clock;
	if (!environCallback(RETRO_ENVIRONMENT_GET_NETPLAY_CLIENT_INDEX, &localClientIndex) ||
	    !environCallback(RETRO_ENVIRONMENT_GET_NETPLAY_CLOCK, &clock)) {
		return;
	}
	core->rtc.override = RTC_FAKE_EPOCH;
	core->rtc.value = clock.epoch_ms;
	core->rtc.fixedZone = true;
	core->rtc.utcOffset = clock.utc_offset;
}

bool mLibretroNetplayRefresh(struct mLibretroNetplayState* state, retro_environment_t environCallback) {
	if (!state) {
		return false;
	}

	unsigned localClientIndex = 0;
	bool netplayActive = environCallback && environCallback(RETRO_ENVIRONMENT_GET_NETPLAY_CLIENT_INDEX, &localClientIndex);

	bool haveContext = false;
	bool rollbackActive = false;
	enum retro_savestate_context context = RETRO_SAVESTATE_CONTEXT_NORMAL;
	if (environCallback && environCallback(RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT, &context)) {
		haveContext = true;
		rollbackActive = context == RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY;
	}

	if (!haveContext) {
		return false;
	}

	if (!netplayActive) {
		if (!state->rollbackContextActive) {
			return false;
		}
		state->rollbackContextActive = false;
		mLibretroLog(RETRO_LOG_INFO, "libretro: rollback netplay context ended\n");
		return true;
	}

	if (!rollbackActive || rollbackActive == state->rollbackContextActive) {
		return false;
	}

	state->rollbackContextActive = rollbackActive;
	mLibretroLog(RETRO_LOG_INFO, "libretro: rollback netplay context detected\n");

	return true;
}
