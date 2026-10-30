#include "open/stagger.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <random>

#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "test_support.hpp"

// Task 9 tests. Break these catches: a meter that triggers below the threshold or
// not at all when it reaches it, a stun that refreshes or fills while it is already
// running, decay that carries on during a stun, a tick that crosses the end of a
// stun crediting the whole tick to the cooldowns, and a public way to stun somebody
// from outside the simulation.
//
// The frozen-clock oracle is the one written out in the plan's technical defaults,
// hand-checked here: stun 0.75 with attack 1 / defense 0.5 / technique 2, a 0.5 step
// leaves stun 0.25 and every clock where it was, and the next 0.5 step ends the stun
// and advances each clock by the 0.25 that was actually unstunned.

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kThreshold = 20.0;

open::StaggerState state(double buildup = 0.0, double stun_remaining = 0.0) {
	open::StaggerState made;
	made.buildup = buildup;
	made.stun_remaining = stun_remaining;
	return made;
}

open::ClockState clocks(double attack, double defense, double technique) {
	open::ClockState made;
	made.attack = attack;
	made.defense = defense;
	made.technique = technique;
	return made;
}

bool close_enough(double got, double want, double tolerance = 1e-9) {
	return std::fabs(got - want) <= tolerance;
}

void the_meter_triggers_when_it_reaches_the_threshold() {
	// Below it: nothing happens but a fuller meter.
	open::StaggerState below = state();
	expect_true("a hit is taken", open::add_stagger(below, 19.0, kThreshold));
	expect_true("19 of 20 is not a stun",
				close_enough(below.buildup, 19.0) && close_enough(below.stun_remaining, 0.0));

	// Exactly on it: the spec says "when it reaches the threshold".
	open::StaggerState equal = state(19.0);
	expect_true("a hit that reaches the threshold triggers", open::add_stagger(equal, 1.0, kThreshold));
	expect_true("the meter resets to zero", close_enough(equal.buildup, 0.0));
	expect_true("and the stun is two seconds", close_enough(equal.stun_remaining, 2.0));

	// Over it, including overkill from a blow that kills: the whole calculated blunt
	// counts, and the meter never goes negative from a reset.
	open::StaggerState over = state();
	expect_true("a heavy hit triggers", open::add_stagger(over, 500.0, kThreshold));
	expect_true("reset, not 480 left over", close_enough(over.buildup, 0.0) &&
											   close_enough(over.stun_remaining, 2.0));

	// Zero blunt adds nothing and cannot trigger by itself.
	open::StaggerState empty = state(5.0);
	expect_true("a zero-blunt hit is accepted", open::add_stagger(empty, 0.0, kThreshold));
	expect_true("and changes nothing", close_enough(empty.buildup, 5.0) &&
										   close_enough(empty.stun_remaining, 0.0));
	open::StaggerState at_zero = state();
	// Accepted, because a hit that lands is a hit; it simply has nothing to add, so
	// the meter stays empty and nobody is stunned.
	expect_true("zero blunt on an empty meter is taken", open::add_stagger(at_zero, 0.0, kThreshold));
	expect_true("and leaves the sheet standing",
				close_enough(at_zero.buildup, 0.0) && close_enough(at_zero.stun_remaining, 0.0));

	// A threshold of zero (no Toughness at all) triggers on any positive blunt, and
	// the meter is not divided by anything.
	open::StaggerState brittle = state();
	expect_true("a zero threshold still triggers on blunt", open::add_stagger(brittle, 0.5, 0.0));
	expect_true("with the stun applied", close_enough(brittle.stun_remaining, 2.0));

	// A threshold that has moved (an attribute changed between hits) is honoured by
	// the hit being resolved now, against the new number, and not by a re-check of the
	// meter afterwards.
	open::StaggerState raised = state(19.0);
	expect_true("a raised threshold still takes the hit", open::add_stagger(raised, 0.5, 40.0));
	expect_true("but 19.5 of 40 stuns nobody", close_enough(raised.stun_remaining, 0.0));
	expect_true("and the buildup carried", close_enough(raised.buildup, 19.5));
	// The same meter measured against a lower threshold: the total now reaches, and
	// that is when the stun starts.
	expect_true("a later hit against the lower threshold triggers",
				open::add_stagger(raised, 0.6, kThreshold));
	expect_true("and the stun starts then", close_enough(raised.stun_remaining, 2.0));
}

void a_running_stun_neither_fills_nor_refreshes() {
	open::StaggerState stunned = state(0.0, 2.0);
	expect_true("a hit during the stun is refused", !open::add_stagger(stunned, 500.0, kThreshold));
	expect_true("the meter did not fill", close_enough(stunned.buildup, 0.0));
	expect_true("and the stun was not refreshed", close_enough(stunned.stun_remaining, 2.0));

	// Part-way through the same: no refresh, so the stun always ends when it started
	// to end, however much arrives in the meantime.
	open::StaggerState late = state(0.0, 0.5);
	expect_true("a late hit is refused", !open::add_stagger(late, 500.0, kThreshold));
	expect_true("still finishing on its own schedule", close_enough(late.stun_remaining, 0.5));

	// Decay is paused for the whole stun, and only starts again afterwards.
	open::StaggerState paused = state(0.0, 1.0);
	expect_true("time during the stun is taken", open::advance_stagger(paused, 0.5, kThreshold));
	expect_true("the stun counted down", close_enough(paused.stun_remaining, 0.5));
	expect_true("nothing decayed because there was nothing to decay",
				close_enough(paused.buildup, 0.0));

	open::StaggerState with_meter = state(10.0, 1.0);
	expect_true("a step during the stun", open::advance_stagger(with_meter, 1.0, kThreshold));
	expect_true("the stun ends exactly on time", close_enough(with_meter.stun_remaining, 0.0));
	expect_true("and the meter that survived the stun is untouched by it",
				close_enough(with_meter.buildup, 10.0));
	// ... and now it decays, from the moment the stun ended.
	expect_true("a step after the stun", open::advance_stagger(with_meter, 1.0, kThreshold));
	expect_close("decay restarted: 2 of 20 per second", with_meter.buildup, 8.0, 1e-9);
}

void buildup_decays_at_a_tenth_of_the_threshold_per_second() {
	// 10% of the threshold per second, so 2/s at a threshold of 20.
	open::StaggerState meter = state(10.0);
	expect_true("one second passes", open::advance_stagger(meter, 1.0, kThreshold));
	expect_close("two points decayed", meter.buildup, 8.0, 1e-9);
	expect_true("another second", open::advance_stagger(meter, 1.0, kThreshold));
	expect_close("four points gone in total", meter.buildup, 6.0, 1e-9);

	// A longer step is not three separate ticks: the same elapsed time decays the
	// same amount either way.
	open::StaggerState whole = state(10.0);
	open::StaggerState split = state(10.0);
	expect_true("one whole second", open::advance_stagger(whole, 1.0, kThreshold));
	expect_true("two half seconds",
				open::advance_stagger(split, 0.5, kThreshold) &&
					open::advance_stagger(split, 0.5, kThreshold));
	expect_true("the same meter either way", close_enough(whole.buildup, split.buildup));

	// The floor is zero: decay cannot go negative, and an empty meter stays empty.
	open::StaggerState low = state(1.0);
	expect_true("a long step", open::advance_stagger(low, 5.0, kThreshold));
	expect_true("stops at zero", close_enough(low.buildup, 0.0));
	open::StaggerState none = state();
	expect_true("an empty meter survives time", open::advance_stagger(none, 100.0, kThreshold));
	expect_true("and stays empty", close_enough(none.buildup, 0.0));

	// A bigger threshold decays faster, because the rate is a fraction of it.
	open::StaggerState tough = state(100.0);
	expect_true("a second at threshold 200", open::advance_stagger(tough, 1.0, 200.0));
	expect_close("20 of it decayed", tough.buildup, 80.0, 1e-9);

	// Zero time advances nothing.
	open::StaggerState waiting = state(10.0, 0.5);
	expect_true("zero seconds is accepted", open::advance_stagger(waiting, 0.0, kThreshold));
	expect_true("and changes nothing",
				close_enough(waiting.buildup, 10.0) && close_enough(waiting.stun_remaining, 0.5));
}

void the_stun_expires_on_the_second_it_was_promised() {
	open::StaggerState full = state(0.0, 2.0);
	expect_true("1.9999 of it", open::advance_stagger(full, 1.9999, kThreshold));
	expect_true("still stunned just short of the end",
				full.stun_remaining > 0.0 && close_enough(full.stun_remaining, 0.0001, 1e-6));

	open::StaggerState exact = state(0.0, 2.0);
	expect_true("exactly 2 seconds", open::advance_stagger(exact, 2.0, kThreshold));
	expect_true("the stun is over, not negative", close_enough(exact.stun_remaining, 0.0));

	open::StaggerState over = state(0.0, 2.0);
	expect_true("2.0001 seconds", open::advance_stagger(over, 2.0001, kThreshold));
	expect_true("overdue time is not banked", close_enough(over.stun_remaining, 0.0));

	// A stun cannot be ended by a negative step, and cannot be extended by a huge one.
	open::StaggerState guard = state(0.0, 1.0);
	expect_true("a negative step is refused", !open::advance_stagger(guard, -1.0, kThreshold));
	expect_true("and the stun is exactly as it was", close_enough(guard.stun_remaining, 1.0));
}

void nonsense_damage_and_time_change_nothing() {
	const open::StaggerState original = state(5.0);

	open::StaggerState nan_blunt = original;
	expect_true("NaN blunt is refused", !open::add_stagger(nan_blunt, kNan, kThreshold));
	expect_true("nothing moved", close_enough(nan_blunt.buildup, 5.0) &&
									 close_enough(nan_blunt.stun_remaining, 0.0));
	open::StaggerState negative = original;
	expect_true("negative blunt is refused", !open::add_stagger(negative, -1.0, kThreshold));
	expect_true("nothing moved", close_enough(negative.buildup, 5.0));
	open::StaggerState infinite = original;
	expect_true("infinite blunt is refused", !open::add_stagger(infinite, kInf, kThreshold));
	expect_true("nothing moved", close_enough(infinite.buildup, 5.0));
	open::StaggerState bad_threshold = original;
	expect_true("a NaN threshold is refused", !open::add_stagger(bad_threshold, 1.0, kNan));
	expect_true("and a negative one", !open::add_stagger(bad_threshold, 1.0, -1.0));
	expect_true("nothing moved", close_enough(bad_threshold.buildup, 5.0));

	open::StaggerState nan_time = original;
	expect_true("NaN time is refused", !open::advance_stagger(nan_time, kNan, kThreshold));
	expect_true("negative time is refused", !open::advance_stagger(nan_time, -0.5, kThreshold));
	expect_true("infinite time is refused", !open::advance_stagger(nan_time, kInf, kThreshold));
	expect_true("a bad threshold stops the step too",
				!open::advance_stagger(nan_time, 1.0, kInf) &&
					!open::advance_stagger(nan_time, 1.0, -5.0));
	expect_true("and the meter is untouched throughout",
				close_enough(nan_time.buildup, 5.0) && close_enough(nan_time.stun_remaining, 0.0));
}

void the_frozen_clock_oracle() {
	// docs/weapons.md Stagger: cooldowns freeze for the stun, and a tick that crosses
	// the end advances them only by the part after it. Plan default 7 pins the numbers.
	open::StaggerState stagger = state(10.0, 0.75);
	open::ClockState running = clocks(1.0, 0.5, 2.0);

	expect_true("the first half second is taken",
				open::advance_frozen_time(running, stagger, 0.5, kThreshold));
	expect_true("the stun shortened", close_enough(stagger.stun_remaining, 0.25));
	expect_true("every clock is exactly as it was while stunned",
				close_enough(running.attack, 1.0) && close_enough(running.defense, 0.5) &&
					close_enough(running.technique, 2.0));
	expect_true("and the meter did not decay during the stun", close_enough(stagger.buildup, 10.0));

	expect_true("the second half second is taken",
				open::advance_frozen_time(running, stagger, 0.5, kThreshold));
	expect_true("the stun has just ended", close_enough(stagger.stun_remaining, 0.0));
	expect_close("attack ran for the unstunned remainder", running.attack, 0.75, 1e-9);
	expect_close("defense ran for the unstunned remainder", running.defense, 0.25, 1e-9);
	expect_close("technique ran for the unstunned remainder", running.technique, 1.75, 1e-9);
	expect_close("decay ran for the unstunned remainder only", stagger.buildup, 9.5, 1e-9);

	// The same elapsed time in one whole tick must reach the same state, so nothing is
	// double-counted or lost at the boundary. The stun still ends without an attack
	// happening inside it: the clocks moved once, not twice.
	open::StaggerState one_shot = state(10.0, 0.75);
	open::ClockState whole = clocks(1.0, 0.5, 2.0);
	expect_true("a single 1.0 tick", open::advance_frozen_time(whole, one_shot, 1.0, kThreshold));
	expect_true("the stun ended", close_enough(one_shot.stun_remaining, 0.0));
	expect_true("the whole 1-tick reaches the split-tick state",
				close_enough(whole.attack, running.attack) && close_enough(whole.defense, running.defense) &&
					close_enough(whole.technique, running.technique) &&
					close_enough(one_shot.buildup, stagger.buildup));

	// Clocks never go below zero, and a stun shorter than the tick cannot make them
	// run backwards.
	open::StaggerState short_stun = state(0.0, 0.1);
	open::ClockState ready = clocks(0.05, 0.0, 0.0);
	expect_true("a long tick over a short stun",
				open::advance_frozen_time(ready, short_stun, 5.0, kThreshold));
	expect_true("no clock went negative",
				ready.attack == 0.0 && ready.defense == 0.0 && ready.technique == 0.0);
	expect_true("the stun is over", close_enough(short_stun.stun_remaining, 0.0));

	// Refusals leave both the clocks and the meter alone.
	open::StaggerState guard = state(3.0, 0.5);
	open::ClockState frozen = clocks(1.0, 1.0, 1.0);
	expect_true("NaN is refused", !open::advance_frozen_time(frozen, guard, kNan, kThreshold));
	expect_true("negative is refused", !open::advance_frozen_time(frozen, guard, -1.0, kThreshold));
	expect_true("a bad threshold is refused",
				!open::advance_frozen_time(frozen, guard, 1.0, -1.0));
	expect_true("nothing moved", close_enough(guard.buildup, 3.0) &&
									 close_enough(guard.stun_remaining, 0.5) &&
									 close_enough(frozen.attack, 1.0) &&
									 close_enough(frozen.technique, 1.0));

	// With no stun running, time is simply time.
	open::StaggerState standing = state(10.0, 0.0);
	open::ClockState ticking = clocks(2.0, 1.0, 4.0);
	expect_true("an ordinary tick", open::advance_frozen_time(ticking, standing, 1.0, kThreshold));
	expect_true("all three clocks ran for the whole second",
				close_enough(ticking.attack, 1.0) && close_enough(ticking.defense, 0.0) &&
					close_enough(ticking.technique, 3.0));
	expect_close("and the meter decayed for the whole second", standing.buildup, 8.0, 1e-9);
}

void a_sheet_with_no_stun_keeps_its_own_clocks() {
	open::CharacterConfig setup;
	setup.id = 1;
	setup.name = "Sheet";
	setup.level = 1;
	setup.blessing = open::Attribute::Strength;
	auto made = open::Character::create(setup);
	if (!made.has_value()) {
		g_failed++;
		return;
	}
	open::Character who = std::move(made).value();

	// Fresh, and there is no public way to be stunned: a sheet starts clear.
	expect_true("no buildup", who.stagger_buildup() == 0.0);
	expect_true("no stun running", who.stun_remaining() == 0.0 && !who.is_stunned());

	// A sheet that has never been hit has no clocks to freeze, and time cannot push a
	// ready clock into debt.
	expect_true("a tick is accepted", who.advance_combat_clocks(0.5f));
	expect_true("and the ready clocks stay ready",
				who.attack_cooldown_remaining() == 0.0f && who.defend_cooldown_remaining() == 0.0f);
	expect_true("bad time is refused",
				!who.advance_combat_clocks(-1.0f) && !who.advance_combat_clocks(kNan) &&
					!who.advance_combat_clocks(kInf));

	// A clock that is really running counts down once per advance, exactly as it did
	// before the meter existed: the frozen-time policy must not slow the ordinary path.
	open::CharacterConfig other_setup;
	other_setup.id = 2;
	other_setup.name = "Target";
	other_setup.level = 1;
	other_setup.blessing = open::Attribute::Strength;
	auto other_made = open::Character::create(other_setup);
	if (!other_made.has_value()) {
		g_failed++;
		return;
	}
	open::Character target = std::move(other_made).value();

	std::mt19937_64 rng(42);
	who.set_attack_target(2);
	const open::Vec3 here = {0.0f, 0.0f, 0.0f};
	const open::Vec3 there = {1.0f, 0.0f, 0.0f};
	const std::optional<open::SwingResult> swing = who.try_melee_swing(target, here, there, rng);
	expect_true("the swing happened and started a clock", swing.has_value());
	const float running = who.attack_cooldown_remaining();
	expect_true("which is running", running > 0.0f);
	expect_true("a zero tick changes nothing",
				who.advance_combat_clocks(0.0f) && who.attack_cooldown_remaining() == running);
	expect_true("half a second is accepted", who.advance_combat_clocks(0.5f));
	expect_true("and the clock moved by half a second, once",
				close_enough(who.attack_cooldown_remaining(), running - 0.5f, 1e-4));
	// More time than remains ends the clock at ready without banking the difference.
	expect_true("a long tick is accepted", who.advance_combat_clocks(600.0f));
	expect_true("and the clock is ready, not negative", who.attack_cooldown_remaining() == 0.0f);
	// None of that stunned anybody: the meter only fills from a typed hit, in Task 10.
	expect_true("the meter is still empty and no stun is running",
				who.stagger_buildup() == 0.0 && !who.is_stunned());
}

}  // namespace

int main() {
	the_meter_triggers_when_it_reaches_the_threshold();
	a_running_stun_neither_fills_nor_refreshes();
	buildup_decays_at_a_tenth_of_the_threshold_per_second();
	the_stun_expires_on_the_second_it_was_promised();
	nonsense_damage_and_time_change_nothing();
	the_frozen_clock_oracle();
	a_sheet_with_no_stun_keeps_its_own_clocks();
	return test_summary();
}
