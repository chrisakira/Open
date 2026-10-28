#include "open/attributes.hpp"
#include "test_support.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <random>

// Task 2 tests. Break these catches: a base that is not 1 at creation, level or
// blessing applied to the wrong attribute, a pool-style multiply, a mutation
// that lands even though it was rejected, or an RNG that reads the clock.

namespace {

constexpr float kBig = 1.0e37f;

bool all_attributes_valid(const open::Attributes &a) {
	for (int i = 0; i < static_cast<int>(open::Attribute::Count); ++i) {
		const open::Attribute attribute = static_cast<open::Attribute>(i);
		if (!std::isfinite(a.effective(attribute))) {
			return false;
		}
	}
	return true;
}

}  // namespace

int main() {
	{
		// Break this catches: an attribute base that starts somewhere else.
		const open::Attributes a(1, open::Attribute::Strength);
		expect_true("level is kept", a.level() == 1);
		expect_true("blessing is kept", a.blessing() == open::Attribute::Strength);
		for (int i = 0; i < static_cast<int>(open::Attribute::Count); ++i) {
			const open::Attribute attribute = static_cast<open::Attribute>(i);
			expect_close("every base starts at one", a.base(attribute), 1.0f);
		}
	}
	{
		// Break this catches: a blessing multiplied into the wrong place, or level
		// leaking into the base.
		const open::Attributes a(1, open::Attribute::Strength);
		expect_close("level-one blessed strength", a.effective(open::Attribute::Strength), 1.25f);
		expect_true("level-one unblessed dexterity", a.effective(open::Attribute::Dexterity) == 1.0f);
		expect_close("blessing does not change the base", a.base(open::Attribute::Strength), 1.0f);
	}
	{
		// Break this catches: a wrong level multiplier table entry.
		const open::Attributes a(7, open::Attribute::Strength);
		expect_true("level-seven blessed strength", a.effective(open::Attribute::Strength) == 125.0f);
		expect_true("level-seven unblessed dexterity", a.effective(open::Attribute::Dexterity) == 100.0f);
	}
	{
		// Break this catches: level applied twice, or the blessing dropped on a
		// raised base.
		open::Attributes a(7, open::Attribute::Strength);
		expect_true("raising strength base is accepted", a.set_base(open::Attribute::Strength, 2.0f));
		expect_close("strength base two at level seven", a.effective(open::Attribute::Strength), 250.0f);
		expect_true("base change keeps the level", a.level() == 7);
		expect_true("base change keeps the blessing", a.blessing() == open::Attribute::Strength);
	}
	{
		// Break this catches: a blessing affecting more than its own attribute.
		for (int b = 0; b < static_cast<int>(open::Attribute::Count); ++b) {
			const open::Attribute blessed = static_cast<open::Attribute>(b);
			const open::Attributes a(3, blessed);  // level 3 multiplies by 4
			expect_close("each blessing boosts its own attribute", a.effective(blessed), 5.0f);
			for (int o = 0; o < static_cast<int>(open::Attribute::Count); ++o) {
				if (o == b) {
					continue;
				}
				const open::Attribute other = static_cast<open::Attribute>(o);
				expect_true("no other attribute sees the blessing", a.effective(other) == 4.0f);
			}
			expect_true("every effective value stays finite", all_attributes_valid(a));
		}
	}
	{
		// Break this catches: a bad level or blessing accepted at construction.
		expect_throws_invalid("level zero is rejected", [] { open::Attributes(0, open::Attribute::Strength); });
		expect_throws_invalid("level eight is rejected", [] { open::Attributes(8, open::Attribute::Strength); });
		expect_throws_invalid("negative level is rejected", [] { open::Attributes(-1, open::Attribute::Strength); });
		expect_throws_invalid("bad blessing id is rejected",
							  [] { open::Attributes(1, static_cast<open::Attribute>(10)); });
		expect_throws_invalid("negative blessing id is rejected",
							  [] { open::Attributes(1, static_cast<open::Attribute>(-1)); });
	}
	{
		// Break this catches: an unchecked id indexing the base array.
		const open::Attributes a(1, open::Attribute::Strength);
		expect_throws_invalid("base rejects a bad attribute id",
							  [&a] { a.base(static_cast<open::Attribute>(10)); });
		expect_throws_invalid("effective rejects a bad attribute id",
							  [&a] { a.effective(static_cast<open::Attribute>(-1)); });
	}
	{
		// Break this catches: a rejected level change that still moved the level.
		open::Attributes a(1, open::Attribute::Strength);
		expect_true("level change to seven is accepted", a.set_level(7));
		expect_true("level is now seven", a.level() == 7);
		expect_close("level change rescales the effective value", a.effective(open::Attribute::Strength), 125.0f);
		expect_close("level change leaves bases alone", a.base(open::Attribute::Strength), 1.0f);
		expect_true("level zero is rejected", !a.set_level(0));
		expect_true("level eight is rejected", !a.set_level(8));
		expect_true("rejected level change keeps the old level", a.level() == 7);
		expect_close("rejected level change keeps the old effective value",
					 a.effective(open::Attribute::Strength), 125.0f);
	}
	{
		// Break this catches: a base mutation landing despite being rejected.
		open::Attributes a(1, open::Attribute::Strength);
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		expect_true("zero base is rejected", !a.set_base(open::Attribute::Strength, 0.0f));
		expect_true("negative base is rejected", !a.set_base(open::Attribute::Strength, -1.0f));
		expect_true("nan base is rejected", !a.set_base(open::Attribute::Strength, nan));
		expect_true("infinite base is rejected", !a.set_base(open::Attribute::Strength, inf));
		expect_true("bad attribute id is rejected", !a.set_base(static_cast<open::Attribute>(10), 2.0f));
		expect_true("bad attribute id for a negative base is rejected",
					!a.set_base(static_cast<open::Attribute>(-1), -5.0f));
		expect_close("no rejected base mutation changed strength", a.base(open::Attribute::Strength), 1.0f);
		expect_close("no rejected base mutation changed dexterity", a.base(open::Attribute::Dexterity), 1.0f);
	}
	{
		// Break this catches: a finite base that turns the effective value into
		// infinity.
		open::Attributes a(7, open::Attribute::Strength);
		expect_true("overflowing base is rejected", !a.set_base(open::Attribute::Strength, kBig));
		expect_close("overflowing base left the old base", a.base(open::Attribute::Strength), 1.0f);
		expect_true("overflowing unblessed base is rejected", !a.set_base(open::Attribute::Luck, kBig));
		expect_close("overflowing unblessed base left the old base", a.base(open::Attribute::Luck), 1.0f);
	}
	{
		// Break this catches: a level change that is legal for the bases but would
		// make an effective value infinite.
		open::Attributes a(1, open::Attribute::Strength);
		expect_true("a large base is legal at level one", a.set_base(open::Attribute::Constitution, kBig));
		expect_true("level seven would overflow, so it is rejected", !a.set_level(7));
		expect_true("level is unchanged after the overflow guard", a.level() == 1);
		expect_true("effective value is still finite", all_attributes_valid(a));
	}
	{
		// Break this catches: the character's blessing changing under itself, and
		// a library-specific sequence being asserted instead of validity.
		std::mt19937_64 first(20261028ULL);
		std::mt19937_64 second(20261028ULL);
		std::array<bool, static_cast<std::size_t>(open::Attribute::Count)> seen{};
		bool same_sequence = true;
		for (int draw = 0; draw < 400; ++draw) {
			const open::Attribute got = open::random_blessing(first);
			const open::Attribute other = open::random_blessing(second);
			expect_true("random blessing is always a real attribute", open::valid(got));
			if (got != other) {
				same_sequence = false;
			}
			seen[static_cast<std::size_t>(got)] = true;
		}
		expect_true("same seed gives the same blessing sequence", same_sequence);
		bool every_blessing_reachable = true;
		for (const bool one : seen) {
			every_blessing_reachable = every_blessing_reachable && one;
		}
		expect_true("every blessing is reachable", every_blessing_reachable);
	}
	{
		// Break this catches: the NPC path reseeding from the clock per character.
		std::mt19937_64 shared(7ULL);
		const open::Attribute first = open::random_blessing(shared);
		const open::Attribute second = open::random_blessing(shared);
		std::mt19937_64 again(7ULL);
		expect_true("a fresh same-seed generator replays", open::random_blessing(again) == first);
		expect_true("a replayed generator's next draw replays too", open::random_blessing(again) == second);
	}

	return test_summary();
}
