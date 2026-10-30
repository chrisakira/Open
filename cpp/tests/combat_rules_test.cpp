#include "open/combat_rules.hpp"

#include <cstdint>
#include <set>
#include <random>

#include "test_support.hpp"

// The two primitives that survive the typed switch unchanged, because they belong to
// neither the scalar path nor the weapon rules: the geometry of "in reach when there is
// no weapon to read a reach from", and the body-part draw that every exchange pays into.
//
// Break these catches: reach measured on the wrong axes, an exclusive boundary, a
// non-finite observation that opens a swing, a part draw with seven faces, a draw that
// can name Count, or a seeded fight that cannot be replayed.

namespace {

void the_fallback_reach_is_horizontal_and_inclusive() {
	const open::Vec3 origin{0.0f, 0.0f, 0.0f};

	// The constant itself is the unarmed fallback's maximum, which is also the number
	// the scene's placeholders were built around.
	expect_close("the fallback melee range is 1.5", open::kMeleeRange, 1.5f);

	expect_true("1.4999 is in range", open::melee_in_range(origin, {1.4999f, 0.0f, 0.0f}));
	expect_true("exactly 1.5 is in range", open::melee_in_range(origin, {1.5f, 0.0f, 0.0f}));
	expect_true("1.5001 is out of range", !open::melee_in_range(origin, {1.5001f, 0.0f, 0.0f}));

	// Height is ignored, exactly as it is when walking.
	expect_true("height does not leave range",
				open::melee_in_range(origin, {1.4999f, 40.0f, 0.0f}));
	expect_true("nor does it enter it", open::melee_in_range(origin, {1.5f, 40.0f, 0.0f}));
	expect_true("and 1.5001 is still out with height on it",
				!open::melee_in_range(origin, {1.5001f, 40.0f, 0.0f}));

	// Diagonals measure the distance, not an axis.
	expect_true("a 1.5 diagonal is in range", open::melee_in_range(origin, {0.9f, 0.0f, 1.2f}));
	expect_true("a hair past it is not", !open::melee_in_range(origin, {0.9001f, 0.0f, 1.2001f}));

	// A bad observation never opens a swing.
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	expect_true("a nan axis is never in range", !open::melee_in_range(origin, {nan, 0.0f, 0.0f}));
	expect_true("an infinite axis is never in range", !open::melee_in_range(origin, {0.0f, 0.0f, inf}));
	expect_true("a nan self position is never in range",
				!open::melee_in_range({nan, 0.0f, 0.0f}, origin));
	// Two huge opposite coordinates overflow the square: that is far away, not close.
	expect_true("an unmeasurable separation is out of range",
				!open::melee_in_range({-1.0e30f, 0.0f, 0.0f}, {1.0e30f, 0.0f, 0.0f}));
}

void the_part_draw_covers_six_parts_and_replays() {
	// Every face appears, Count never does, and the same seed gives the same fight.
	std::mt19937_64 rng(2026);
	std::set<open::BodyPart> seen;
	for (int roll = 0; roll < 600; ++roll) {
		const open::BodyPart part = open::random_melee_part(rng);
		expect_true("the draw names a real part",
					part != open::BodyPart::Count &&
						part >= open::BodyPart::Head && part <= open::BodyPart::RightLeg);
		seen.insert(part);
	}
	expect_true("all six parts are reachable", seen.size() == 6u);

	std::mt19937_64 first(7);
	std::mt19937_64 second(7);
	bool replayed = true;
	for (int roll = 0; roll < 50; ++roll) {
		if (open::random_melee_part(first) != open::random_melee_part(second)) {
			replayed = false;
		}
	}
	expect_true("a seeded fight replays part for part", replayed);

	// And the draw is not a fixed answer: two different seeds disagree somewhere in
	// enough rolls, which is what makes the above a sequence rather than a constant.
	std::mt19937_64 left(1);
	std::mt19937_64 right(999983);
	bool differs = false;
	for (int roll = 0; roll < 200 && !differs; ++roll) {
		differs = open::random_melee_part(left) != open::random_melee_part(right);
	}
	expect_true("different seeds can disagree", differs);
}

}  // namespace

int main() {
	the_fallback_reach_is_horizontal_and_inclusive();
	the_part_draw_covers_six_parts_and_replays();
	return test_summary();
}
