#pragma once

namespace open {

// Blunt damage accumulates between hits, and when it reaches the threshold the
// character is stunned for a moment: no move, no attack, no defence, armour worth
// nothing, and every combat clock frozen until it passes. The meter is not a second
// health pool and the stun is not an injury - it is a pause with a schedule, so it
// lives apart from Vitals and never touches a limb's debuff.
//
// docs/weapons.md "Stagger" owns the rules; this owns the state and the arithmetic.
struct StaggerState {
	double buildup = 0.0;        // points earned since the last trigger or decay
	double stun_remaining = 0.0;  // seconds of stun left, 0 when standing
};

// The clocks a stunned character cannot run. The technique clock joins the other two
// for the same reason: a stun that froze attack and defence but let a technique
// cooldown tick would hand back a move the stun was meant to deny.
struct ClockState {
	double attack = 0.0;
	double defense = 0.0;
	double technique = 0.0;
};

// Stagger threshold = 10 x the defender's effective Toughness. The coefficient is a
// starting balance value, and this is the only place the number appears.
inline constexpr double kStaggerThresholdCoefficient = 10.0;

// Buildup decays between hits by 10% of the threshold per second, down to zero, so a
// tough opponent's meter drains faster in absolute points.
inline constexpr double kStaggerDecayPerSecond = 0.1;

// How long a trigger holds the character, and the only duration the meter gives.
inline constexpr double kStunSeconds = 2.0;

// Adds one hit's final blunt - the calculated component after armour and Toughness,
// overkill included, which is what makes a killing blow still count. The meter resets
// and the stun starts when the total reaches the threshold.
//
// True when the hit was taken, which is not the same as triggering: whether a stun
// started is read from stun_remaining, so no caller has to guess it from a return
// value. False, with the state untouched, when the numbers are not real (negative,
// NaN, infinite) or when a stun is already running - during a stun the meter neither
// fills nor refreshes, so hitting a stunned character cannot keep them down.
bool add_stagger(StaggerState &state, double final_blunt, double threshold);

// Moves the meter and a running stun forward by real elapsed time: the stun counts
// down and the meter holds still, or the meter decays when there is no stun. A step
// longer than the stun that remains does not bank the leftover anywhere.
bool advance_stagger(StaggerState &state, double seconds, double threshold);

// One simulation step for a character and their clocks: every clock freezes for the
// part of the step spent stunned and runs only for the unstunned remainder, and the
// meter decays for that remainder alone. A tick that crosses the end of a stun
// therefore advances the clocks by less than it advanced time, and the same elapsed
// time reaches the same state whether it arrives in one step or several.
bool advance_frozen_time(ClockState &clocks, StaggerState &state, double seconds, double threshold);

}  // namespace open
