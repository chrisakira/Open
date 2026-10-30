#include "open/character.hpp"
#include "open/combat_rules.hpp"
#include "open/move_order.hpp"
#include "open/weapon_rules.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "test_support.hpp"

// Task 1 attack-order tests. Break these catches: an attack that survives a
// rejected move, a move that survives an attack, an attack kept by a dead actor,
// a range test that reads height or each axis instead of the horizontal
// distance, and a pursuit that walks into the target or stalls outside melee
// range because walking's arrive radius is 0.4.

namespace {

open::CharacterConfig config(std::uint64_t id) {
	open::CharacterConfig made;
	made.id = id;
	made.name = "";
	made.faction = open::Faction::Friendly;
	made.level = 1;
	made.blessing = open::Attribute::Strength;
	return made;
}

open::Character make_character(std::uint64_t id = 1) {
	const std::optional<open::Character> made = open::Character::create(config(id));
	expect_true("the test character is created", made.has_value());
	return *made;
}

bool is_target(const std::optional<std::uint64_t> &got, std::uint64_t want) {
	return got.has_value() && *got == want;
}

// Reading an empty destination must not be undefined behaviour in a test that is
// already failing: NaN simply fails every float comparison here.
float dest_x(const std::optional<open::Vec3> &got) {
	return got.has_value() ? got->x : std::numeric_limits<float>::quiet_NaN();
}

}  // namespace

int main() {
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();

	{
		// attack_replaces_move: an accepted attack owns the actor's intent. IDs 2
		// and 3 are both unfiltered integers here, so any faction gate would show
		// up as a refusal.
		open::Character c = make_character(1);
		expect_true("a move is accepted first", c.set_destination(8.0f, 0.0f, 0.0f));
		expect_true("target 2 is accepted", c.set_attack_target(2));
		expect_true("the target id round-trips", is_target(c.attack_target(), 2));
		expect_true("the accepted attack clears the move", !c.destination().has_value());
		expect_close("the cleared move no longer walks", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);

		expect_true("retargeting from 2 to 3 is accepted", c.set_attack_target(3));
		expect_true("the new target replaces the old", is_target(c.attack_target(), 3));
		expect_true("retargeting leaves no move order", !c.destination().has_value());
	}
	{
		// move_and_clear_cancel_attack: a successful move and a clear both end it.
		open::Character c = make_character(1);
		expect_true("the attack is accepted", c.set_attack_target(2));
		expect_true("a valid move is accepted", c.set_destination(5.0f, 0.0f, 0.0f));
		expect_true("the accepted move clears the attack", !c.attack_target().has_value());
		expect_close("the move survives", dest_x(c.destination()), 5.0f);
		expect_close("and the character walks", c.velocity_at(0.0f, 0.0f, 0.0f).x, 4.5f);

		expect_true("the attack comes back", c.set_attack_target(2));
		expect_close("an attack-only order never enters the move query",
					 c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
		c.clear_order();
		expect_true("clear_order removes the attack", !c.attack_target().has_value());
		expect_true("clear_order removes the move too", !c.destination().has_value());
	}
	{
		// invalid_intent_is_atomic: a refused command cannot destroy a working order.
		open::Character c = make_character(1);
		expect_true("a move is accepted", c.set_destination(8.0f, 0.0f, 0.0f));
		expect_true("the zero target id is refused", !c.set_attack_target(0));
		expect_close("the move survives a refused attack", dest_x(c.destination()), 8.0f);
		expect_true("the refused attack left nothing behind", !c.attack_target().has_value());

		expect_true("attacking self is refused", !c.set_attack_target(c.id()));
		expect_close("the move survives a self attack", dest_x(c.destination()), 8.0f);
		expect_close("the move still walks", c.velocity_at(0.0f, 0.0f, 0.0f).x, 4.5f);

		expect_true("a real attack is accepted", c.set_attack_target(2));
		expect_true("a nan destination is refused", !c.set_destination(nan, 0.0f, 0.0f));
		expect_true("the attack survives a refused move", is_target(c.attack_target(), 2));
		expect_true("an infinite destination is refused too", !c.set_destination(inf, 0.0f, 0.0f));
		expect_true("the attack survives that refusal too", is_target(c.attack_target(), 2));
		expect_true("a refused move leaves no destination", !c.destination().has_value());
		expect_close("and the attack still pursues",
					 c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}).x, 4.5f);
	}
	{
		// death_cancels_attack: a corpse keeps no intent and takes no new intent.
		open::Character c = make_character(1);
		expect_true("the attack is accepted", c.set_attack_target(2));
		expect_true("a torso hit lands", c.damage(open::BodyPart::Torso, 50.0f));
		expect_true("dead", !c.alive());
		expect_true("death clears the attack", !c.attack_target().has_value());
		expect_true("a dead actor takes no new attack", !c.set_attack_target(3));
		expect_true("still no attack after the attempt", !c.attack_target().has_value());
		expect_close("a dead actor does not pursue",
					 c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}).x, 0.0f);
	}
	{
		// horizontal_melee_boundary: X/Z distance, inclusive at 1.5, height ignored.
		expect_close("the placeholder melee range is 1.5", open::kMeleeRange, 1.5f);
		const open::Vec3 origin{0.0f, 0.0f, 0.0f};
		expect_true("1.4999 is in range", open::melee_in_range(origin, {1.4999f, 0.0f, 0.0f}));
		expect_true("exactly 1.5 is in range", open::melee_in_range(origin, {1.5f, 0.0f, 0.0f}));
		expect_true("1.5001 is out of range", !open::melee_in_range(origin, {1.5001f, 0.0f, 0.0f}));

		expect_true("a height difference cannot put it out of range",
					open::melee_in_range(origin, {1.4999f, 40.0f, 0.0f}));
		expect_true("the boundary holds with a height difference",
					open::melee_in_range(origin, {1.5f, 40.0f, 0.0f}));
		expect_true("a height difference cannot reach further",
					!open::melee_in_range(origin, {1.5001f, 40.0f, 0.0f}));

		// 0.9/1.2 is a 1.5 diagonal: the horizontal distance matters, not each axis.
		expect_true("a 1.5 diagonal is in range", open::melee_in_range(origin, {0.9f, 0.0f, 1.2f}));
		expect_true("a hair past the diagonal is out",
					!open::melee_in_range(origin, {0.9001f, 0.0f, 1.2001f}));

		expect_true("a nan axis is never in range", !open::melee_in_range(origin, {nan, 0.0f, 0.0f}));
		expect_true("an infinite axis is never in range", !open::melee_in_range(origin, {0.0f, 0.0f, inf}));
		expect_true("a nan self position is never in range", !open::melee_in_range({nan, 0.0f, 0.0f}, origin));
		expect_true("an overflowing separation is never in range",
					!open::melee_in_range({-1.0e30f, 0.0f, 0.0f}, {1.0e30f, 0.0f, 0.0f}));
	}
	{
		// approach_stops_beside_target: the walk's 0.4 band is moved to melee range.
		open::Character c = make_character(1);
		expect_vec("no attack order means no pursuit",
				   c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {10.0f, 7.0f, 0.0f}), 0.0f, 0.0f, 0.0f);
		expect_true("target 2 is accepted", c.set_attack_target(2));
		expect_vec("walks toward the target at walk speed",
				   c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {10.0f, 7.0f, 0.0f}), 4.5f, 0.0f, 0.0f);
		expect_close("the ordinary query ignores an attack order", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);

		const open::Vec3 almost = c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {1.5001f, 0.0f, 0.0f});
		expect_true("just outside melee range it keeps walking", almost.x > 0.0f);
		expect_close("and still at walk speed", almost.x, 4.5f);
		expect_vec("exactly in melee range it stops",
				   c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {1.5f, 0.0f, 0.0f}), 0.0f, 0.0f, 0.0f);
		expect_vec("in range with a height difference it still stops",
				   c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {1.5f, 9.0f, 0.0f}), 0.0f, 0.0f, 0.0f);
		expect_vec("coincident x/z does not jitter",
				   c.attack_velocity_at({3.0f, 0.0f, 7.0f}, {3.0f, 9.0f, 7.0f}), 0.0f, 0.0f, 0.0f);

		expect_vec("pursuit stays horizontal", c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f}),
				   0.0f, 0.0f, 4.5f);
		expect_vec("pursuit heads straight at the target",
				   c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 10.0f}), 3.1819805f, 0.0f,
				   3.1819805f);

		// Ordinary point movement keeps its own 0.4 radius and ignores the attack query.
		expect_true("a point order comes back", c.set_destination(0.4f, 0.0f, 0.0f));
		expect_close("point movement stops at 0.4", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
		expect_true("just past 0.4 is accepted", c.set_destination(0.41f, 0.0f, 0.0f));
		expect_close("point movement walks at 0.41", c.velocity_at(0.0f, 0.0f, 0.0f).x, 4.5f);
		expect_vec("the attack query is zero with no attack order",
				   c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}), 0.0f, 0.0f, 0.0f);
	}
	{
		// The pursuit speed is the same C++ walk speed, so limbs and the raw switch
		// behave exactly as they do for a click.
		open::Character c = make_character(1);
		expect_true("the attack is accepted", c.set_attack_target(2));
		expect_true("one leg goes", c.damage(open::BodyPart::LeftLeg, 40.0f));
		expect_close("one leg halves pursuit",
					 c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}).x, 2.25f);
		expect_true("the other leg goes", c.damage(open::BodyPart::RightLeg, 40.0f));
		expect_close("both legs stop pursuit",
					 c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}).x, 0.0f);
		expect_true("still alive with both legs gone", c.alive());
		expect_true("and the attack is still there", is_target(c.attack_target(), 2));

		open::Character statue = make_character(4);
		expect_true("the standing actor takes an attack", statue.set_attack_target(2));
		statue.set_can_move(false);
		expect_vec("a standing actor does not pursue",
				   statue.attack_velocity_at({0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}), 0.0f, 0.0f, 0.0f);
	}
	{
		// Non-finite inputs must never produce a NaN velocity that flies through the world.
		open::Character c = make_character(1);
		expect_true("the attack is accepted", c.set_attack_target(2));
		expect_vec("a nan self position does not move",
				   c.attack_velocity_at({nan, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}), 0.0f, 0.0f, 0.0f);
		expect_vec("a nan target does not move",
				   c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {nan, 0.0f, 0.0f}), 0.0f, 0.0f, 0.0f);
		expect_vec("an infinite target does not move",
				   c.attack_velocity_at({0.0f, 0.0f, 0.0f}, {inf, 0.0f, 0.0f}), 0.0f, 0.0f, 0.0f);
		expect_vec("an overflowing separation does not move",
				   c.attack_velocity_at({-1.0e30f, 0.0f, 0.0f}, {1.0e30f, 0.0f, 0.0f}), 0.0f, 0.0f, 0.0f);
	}

	{
		// Task 5 reach helpers. Break this catches: an exclusive maximum, a minimum
		// penalty applied on the boundary, height entering the test, a non-finite
		// measurement opening a swing, or pursuit that cannot close on a weapon
		// whose reach is shorter than the 0.4 arrival radius.
		const open::Vec3 here = {0.0f, 1.0f, 0.0f};
		const auto across = [](float distance) {
			return open::Vec3{distance, 1.0f, 0.0f};
		};

		expect_true("just inside the maximum is in reach",
					open::horizontal_in_reach(here, across(1.4999f), 1.5));
		expect_true("exactly on the maximum is in reach",
					open::horizontal_in_reach(here, across(1.5f), 1.5));
		expect_true("just past the maximum is not",
					!open::horizontal_in_reach(here, across(1.5001f), 1.5));
		expect_true("height is ignored, both ways",
					open::horizontal_in_reach(here, {1.5f, 900.0f, 0.0f}, 1.5) &&
						!open::horizontal_in_reach(here, {1.5001f, -900.0f, 0.0f}, 1.5));
		expect_true("a short reach has its own bound",
					open::horizontal_in_reach(here, across(0.2f), 0.2) &&
						!open::horizontal_in_reach(here, across(0.2001f), 0.2));
		expect_true("a long reach has its own bound",
					open::horizontal_in_reach(here, across(3.0f), 3.0) &&
						!open::horizontal_in_reach(here, across(3.0001f), 3.0));
		expect_true("0.4 is not privileged any more",
					open::horizontal_in_reach(here, across(0.4f), 0.4) &&
						!open::horizontal_in_reach(here, across(0.4001f), 0.4));
		expect_true("a non-finite position is never in reach",
					!open::horizontal_in_reach(here, {nan, 0.0f, 0.0f}, 1.5) &&
						!open::horizontal_in_reach(here, {inf, 0.0f, 0.0f}, 1.5));
		expect_true("a non-positive reach admits nothing",
					!open::horizontal_in_reach(here, here, 0.0) && !open::horizontal_in_reach(here, here, -1.0));

		// The minimum is strict: standing on it costs nothing, one hair inside halves.
		expect_close("no minimum never penalises", open::minimum_range_factor(0.0, 0.0), 1.0f, 1e-6f);
		expect_close("exactly at the minimum", open::minimum_range_factor(0.6, 0.6), 1.0f, 1e-6f);
		expect_close("a hair inside the minimum", open::minimum_range_factor(0.59999, 0.6), 0.5f, 1e-6f);
		expect_close("well inside the minimum", open::minimum_range_factor(0.1, 0.6), 0.5f, 1e-6f);
		expect_close("outside the minimum", open::minimum_range_factor(2.0, 0.6), 1.0f, 1e-6f);
		expect_close("minimum equal to maximum, on it", open::minimum_range_factor(1.0, 1.0), 1.0f, 1e-6f);
		expect_close("minimum equal to maximum, inside it", open::minimum_range_factor(0.999, 1.0), 0.5f,
					 1e-6f);

		const auto speed_of = [](const open::Vec3 &v) {
			return std::sqrt(v.x * v.x + v.z * v.z);
		};
		const float walk = 4.5f;

		const open::Vec3 closing = open::pursuit_velocity(here, across(10.0f), 1.5, walk);
		expect_true("out of reach it walks", speed_of(closing) > 0.05f);
		expect_true("toward the target", closing.x > 0.0f);
		expect_close("at the caller's speed", speed_of(closing), walk, 1e-3f);
		expect_true("and never vertically", closing.y == 0.0f);
		expect_close("in reach it stops", speed_of(open::pursuit_velocity(here, across(1.0f), 1.5, walk)),
					 0.0f, 1e-6f);
		expect_close("too close it does not retreat",
					 speed_of(open::pursuit_velocity(here, across(0.2f), 1.5, walk)), 0.0f, 1e-6f);

		// Walk the pursuit to a stop with the same arrival band a floor click uses.
		const auto settle = [&](double reach, float target_x) {
			open::Vec3 walker = here;
			for (int step = 0; step < 4000; ++step) {
				const open::Vec3 move =
					open::pursuit_velocity(walker, {target_x, 1.0f, 0.0f}, reach, walk);
				if (speed_of(move) <= 0.0f) {
					return std::make_pair(true, std::fabs(walker.x - target_x));
				}
				walker.x += move.x * 0.1f;
				walker.z += move.z * 0.1f;
			}
			return std::make_pair(false, 0.0f);
		};
		{
			const std::pair<bool, float> normal = settle(1.5, 10.0f);
			expect_true("a normal reach closes", normal.first);
			expect_true("and stops inside its own reach", normal.second <= 1.5 + 1e-3);
			// A weapon shorter than the 0.4 arrival radius must still reach: the aim
			// point moves past the target rather than refusing to move.
			const std::pair<bool, float> short_reach = settle(0.2, 10.0f);
			expect_true("a 0.2 reach closes instead of stalling", short_reach.first);
			expect_true("a 0.2 reach stops at 0.2", short_reach.second <= 0.2 + 1e-3);
			const std::pair<bool, float> radius_reach = settle(0.4, 10.0f);
			expect_true("a reach equal to the arrival radius closes", radius_reach.first);
			expect_true("and stops within it", radius_reach.second <= 0.4 + 1e-3);
			const std::pair<bool, float> long_reach = settle(3.0, 20.0f);
			expect_true("a 3.0 reach closes", long_reach.first);
			expect_true("and stops at 3.0", long_reach.second <= 3.0 + 1e-3);
		}

		expect_close("an injured leg's 2.25 is the caller's to choose",
					 speed_of(open::pursuit_velocity(here, across(10.0f), 1.5, 2.25f)), 2.25f, 1e-3f);
		expect_true("zero speed never moves",
					speed_of(open::pursuit_velocity(here, across(10.0f), 1.5, 0.0f)) == 0.0f);
		expect_true("bad geometry produces no velocity",
					speed_of(open::pursuit_velocity({nan, 0.0f, 0.0f}, across(10.0f), 1.5, walk)) == 0.0f &&
						speed_of(open::pursuit_velocity(here, {inf, 0.0f, 0.0f}, 1.5, walk)) == 0.0f &&
						speed_of(open::pursuit_velocity(here, {1.5f, 0.0f, 0.0f}, 0.0, walk)) == 0.0f);
		expect_true("an overflowing separation produces no velocity",
					speed_of(open::pursuit_velocity({-1.0e30f, 0.0f, 0.0f}, {1.0e30f, 0.0f, 0.0f}, 1.5,
													walk)) == 0.0f);
	}

	return test_summary();
}
