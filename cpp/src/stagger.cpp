#include "open/stagger.hpp"

#include <algorithm>
#include <cmath>

namespace open {
namespace {

bool usable_amount(double value) {
	return std::isfinite(value) && value >= 0.0;
}

// The meter's own step, for a moment known to be unstunned.
void decay(StaggerState &state, double seconds, double threshold) {
	state.buildup = std::max(0.0, state.buildup - kStaggerDecayPerSecond * threshold * seconds);
}

void run_clocks(ClockState &clocks, double seconds) {
	clocks.attack = std::max(0.0, clocks.attack - seconds);
	clocks.defense = std::max(0.0, clocks.defense - seconds);
	// Every technique clock runs for the same unstunned remainder and clamps at ready:
	// overdue time is not banked against the next technique, exactly as for the other two.
	for (double &remaining : clocks.techniques) {
		remaining = std::max(0.0, remaining - seconds);
	}
}

}  // namespace

bool add_stagger(StaggerState &state, double final_blunt, double threshold) {
	if (!usable_amount(final_blunt) || !usable_amount(threshold)) {
		return false;
	}
	if (state.stun_remaining > 0.0) {
		// Already down: no fill, no refresh. The stun ends when it started to end,
		// however much blunt arrives in the meantime.
		return false;
	}

	state.buildup += final_blunt;
	if (state.buildup < threshold) {
		return true;
	}
	// Reaching the threshold is enough. The meter resets rather than carrying the
	// overkill into the next exchange, and the stun is always the same length: a
	// harder blow stuns sooner, not for longer.
	state.buildup = 0.0;
	state.stun_remaining = kStunSeconds;
	return true;
}

bool advance_stagger(StaggerState &state, double seconds, double threshold) {
	if (!usable_amount(seconds) || !usable_amount(threshold)) {
		return false;
	}
	if (state.stun_remaining > 0.0) {
		// The meter holds still for the whole stun, and overdue time is not banked.
		const double used = std::min(seconds, state.stun_remaining);
		state.stun_remaining -= used;
		return true;
	}
	decay(state, seconds, threshold);
	return true;
}

bool advance_frozen_time(ClockState &clocks, StaggerState &state, double seconds, double threshold) {
	if (!usable_amount(seconds) || !usable_amount(threshold)) {
		return false;
	}

	// Spent stunned first, so only what is left can move anything.
	double left = seconds;
	if (state.stun_remaining > 0.0) {
		const double stunned = std::min(left, state.stun_remaining);
		state.stun_remaining -= stunned;
		left -= stunned;
		// Frozen means frozen: no clock moved and nothing decayed for `stunned`.
	}
	if (left > 0.0) {
		run_clocks(clocks, left);
		decay(state, left, threshold);
	}
	return true;
}

}  // namespace open
