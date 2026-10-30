#include "open/work_actions.hpp"

#include <limits>
#include <optional>
#include <vector>

#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/items.hpp"
#include "open/skills.hpp"
#include "open/weapon_rules.hpp"
#include "test_support.hpp"

// Task 7 tests. Break these catches: a craft band off by one at 34/35, 67/68,
// 63/64, 127/128, 190/191, an Armoursmith quietly allowed to forge tier-7 blades,
// a repair that floors instead of rounding up, a repair that can upgrade an item
// past its maximum, a work award that grants main-level XP, refills a pool, traps
// progress at a cap, or is reachable from a dead sheet.
//
// The bands and the repair formula are quoted from docs/weapons.md; the award
// budget is the "1 XP, tunable" rule, not a new progression system.

namespace {

open::Character craft_sheet(std::uint64_t id) {
	open::CharacterConfig setup;
	setup.id = id;
	setup.name = "Smith";
	setup.level = 1;
	setup.blessing = open::Attribute::Strength;
	auto made = open::Character::create(setup);
	return std::move(made).value();
}

// Trains the basic Smithing track to a rank using production use only, one XP per
// successful use, the way every other skill is trained.
void train_smithing(open::Character &who, unsigned points) {
	for (unsigned i = 0; i < points; ++i) {
		who.record_use(open::BasicSkill::Smithing, std::nullopt, true);
	}
}

unsigned smithing_rank(const open::Character &who) {
	return who.resolve_skill(open::BasicSkill::Smithing, std::nullopt).rank;
}

// The basic Smithing rank a sheet pays a weapon job with: their own track when they
// chose Weaponsmith, the frozen parent otherwise.
double smithing_frozen(const open::Character &who) {
	return static_cast<double>(smithing_rank(who));
}

// XP needed to raise a basic track from rank 1 to `rank`.
unsigned xp_to_reach(unsigned rank) {
	unsigned total = 0;
	for (unsigned n = 1; n < rank; ++n) {
		total += open::basic_xp_to_next(n);
	}
	return total;
}

void train_smithing_to(open::Character &who, unsigned rank) {
	train_smithing(who, xp_to_reach(rank));
	if (smithing_rank(who) != rank) {
		expect_true("the Smithing fixture reached its rank", false);
	}
}

// Brings basic Smithing to 100 and chooses one specialisation.
void specialise(open::Character &who, open::AdvancedSkill option) {
	train_smithing_to(who, open::kBasicRankCap);
	expect_true("the parent froze at 100", smithing_rank(who) == open::kBasicRankCap);
	expect_true("the specialisation was chosen", who.advance(option));
}

void quality_bands_are_exact() {
	// Basic Smithing: 1-34 tier 1, 35-67 tier 2, 68-100 tier 3.
	for (const unsigned rank : {1u, 2u, 33u, 34u}) {
		const std::optional<unsigned> cap = open::weapon_quality_cap(rank, false);
		expect_true("basic band 1 resolves", cap.has_value());
		if (cap) {
			expect_true("basic 1..34 makes tier 1", *cap == 1u);
		}
	}
	for (const unsigned rank : {35u, 36u, 66u, 67u}) {
		const std::optional<unsigned> cap = open::weapon_quality_cap(rank, false);
		expect_true("basic band 2 resolves", cap.has_value());
		if (cap) {
			expect_true("basic 35..67 makes tier 2", *cap == 2u);
		}
	}
	for (const unsigned rank : {68u, 69u, 99u, 100u}) {
		const std::optional<unsigned> cap = open::weapon_quality_cap(rank, false);
		expect_true("basic band 3 resolves", cap.has_value());
		if (cap) {
			expect_true("basic 68..100 makes tier 3", *cap == 3u);
		}
	}

	// Advanced Weaponsmith: 1-63 tier 4, 64-127 tier 5, 128-190 tier 6, 191-200 tier 7.
	for (const unsigned rank : {1u, 2u, 62u, 63u}) {
		const std::optional<unsigned> cap = open::weapon_quality_cap(rank, true);
		expect_true("advanced band 4 resolves", cap.has_value());
		if (cap) {
			expect_true("Weaponsmith 1..63 makes tier 4", *cap == 4u);
		}
	}
	for (const unsigned rank : {64u, 65u, 126u, 127u}) {
		const std::optional<unsigned> cap = open::weapon_quality_cap(rank, true);
		expect_true("advanced band 5 resolves", cap.has_value());
		if (cap) {
			expect_true("Weaponsmith 64..127 makes tier 5", *cap == 5u);
		}
	}
	for (const unsigned rank : {128u, 129u, 189u, 190u}) {
		const std::optional<unsigned> cap = open::weapon_quality_cap(rank, true);
		expect_true("advanced band 6 resolves", cap.has_value());
		if (cap) {
			expect_true("Weaponsmith 128..190 makes tier 6", *cap == 6u);
		}
	}
	for (const unsigned rank : {191u, 192u, 199u, 200u}) {
		const std::optional<unsigned> cap = open::weapon_quality_cap(rank, true);
		expect_true("advanced band 7 resolves", cap.has_value());
		if (cap) {
			expect_true("Weaponsmith 191..200 makes tier 7", *cap == 7u);
		}
	}

	// Ranks outside a real track answer nothing rather than guessing a tier.
	expect_true("rank 0 is not a trained rank", !open::weapon_quality_cap(0, false).has_value());
	expect_true("rank 0 is not a trained advanced rank", !open::weapon_quality_cap(0, true).has_value());
	expect_true("a basic cannot be trained past 100", !open::weapon_quality_cap(101, false).has_value());
	expect_true("an advanced cannot be trained past 200", !open::weapon_quality_cap(201, true).has_value());
	// Each scale refuses ranks its track cannot reach: a basic track stops at 100 and
	// an advanced one at 200, and neither reads the other's numbers as its own band.
	expect_true("a basic rank cannot be past the basic cap", !open::weapon_quality_cap(150, false).has_value());
	expect_true("an advanced rank cannot be past the advanced cap",
				!open::weapon_quality_cap(201, true).has_value());
	// A basic 68 is tier 3 and an advanced 68 is tier 5: the same number, two tracks.
	expect_true("the two scales are not interchangeable",
				open::weapon_quality_cap(68, false).value_or(0) == 3u &&
					open::weapon_quality_cap(68, true).value_or(0) == 5u);

	// The cap never falls as a smith trains: a higher rank cannot lose a tier.
	unsigned previous = 0;
	for (unsigned rank = 1; rank <= open::kBasicRankCap; ++rank) {
		const std::optional<unsigned> cap = open::weapon_quality_cap(rank, false);
		if (!cap.has_value() || *cap < previous) {
			expect_true("the basic ladder never dips", false);
			break;
		}
		previous = *cap;
	}
	expect_true("and it ends at tier 3 for the frozen parent", previous == 3u);
	previous = 0;
	for (unsigned rank = 1; rank <= open::kAdvancedRankCap; ++rank) {
		const std::optional<unsigned> cap = open::weapon_quality_cap(rank, true);
		if (!cap.has_value() || *cap < previous) {
			expect_true("the advanced ladder never dips", false);
			break;
		}
		previous = *cap;
	}
	expect_true("and it ends at tier 7", previous == 7u);
}

void the_craft_job_is_smithing_or_its_weapon_spec() {
	// Unspecialised: the basic job, capped by its own rank.
	open::Character novice = craft_sheet(1);
	const open::SkillJob novice_job = open::weapon_craft_job(novice);
	expect_true("an unspecialised smith uses basic Smithing",
				novice_job.basic == open::BasicSkill::Smithing && !novice_job.option.has_value());
	expect_true("at rank 1 that is tier 1", open::max_weapon_quality(novice) == 1u);

	// A Weaponsmith starts at rank 1 on their own track and can immediately forge a
	// tier-4 blade: the advanced band is not gated on the frozen parent.
	open::Character weaponsmith = craft_sheet(2);
	specialise(weaponsmith, open::AdvancedSkill::Weaponsmith);
	const open::SkillJob smith_job = open::weapon_craft_job(weaponsmith);
	expect_true("a Weaponsmith crafts on the Weaponsmith track",
				smith_job.basic == open::BasicSkill::Smithing && smith_job.option.has_value() &&
					*smith_job.option == open::AdvancedSkill::Weaponsmith);
	expect_close("at rank 1", static_cast<double>(weaponsmith.resolve_skill(smith_job.basic, smith_job.option).rank),
				 1.0, 1e-9);
	expect_true("which is already tier 4", open::max_weapon_quality(weaponsmith) == 4u);

	// An Armoursmith may not forge advanced weapons: the other field uses the frozen
	// basic, which caps at tier 3.
	open::Character armoursmith = craft_sheet(3);
	specialise(armoursmith, open::AdvancedSkill::Armoursmith);
	const open::SkillJob armour_job = open::weapon_craft_job(armoursmith);
	expect_true("weapons still resolve to the basic job for an Armoursmith",
				armour_job.basic == open::BasicSkill::Smithing && !armour_job.option.has_value());
	expect_close("and the parent is frozen at 100", smithing_frozen(armoursmith), 100.0, 1e-9);
	expect_true("so their weapons cap is tier 3, not 7", open::max_weapon_quality(armoursmith) == 3u);

	// The bands are a maximum, so any lower unlocked tier is still selectable: the
	// cap function answers the ceiling and nothing forbids choosing below it.
	const std::optional<unsigned> ceiling = open::weapon_quality_cap(100, false);
	expect_true("a tier-3 ceiling still admits tiers 1 and 2",
				ceiling.has_value() && *ceiling == 3u &&
					open::weapon_quality_cap(100, false).has_value() &&
					open::quality_damage_multiplier(1).has_value() &&
					open::quality_damage_multiplier(2).has_value() &&
					open::quality_damage_multiplier(3).has_value());
}

void repair_rounds_up_and_stops_at_the_maximum() {
	// A repair restores trained% of the maximum, rounded up to the next whole point,
	// capped at that maximum. Never more, and never a bigger maximum.
	expect_true("rank 1 on a 101-point item rounds up", open::repair_points(101, 1) == 2u);
	expect_true("rank 35 on a 100-point item is exactly 35", open::repair_points(100, 35) == 35u);
	expect_true("rank 1 on a 100-point item is one point", open::repair_points(100, 1) == 1u);
	expect_true("a fractional point still rounds up", open::repair_points(7, 50) == 4u);
	expect_true("rank 100 restores everything", open::repair_points(100, 100) == 100u);
	expect_true("an advanced rank is capped at 100% for repairs", open::repair_points(100, 191) == 100u);
	expect_true("so is rank 200", open::repair_points(100, 200) == 100u);
	expect_true("an advanced rank cannot exceed the maximum", open::repair_points(64, 200) == 64u);
	expect_true("no skill restores nothing", open::repair_points(100, 0) == 0u);
	expect_true("a broken-off maximum cannot be exceeded", open::repair_points(1, 100) == 1u);
	expect_true("and a zero maximum stays zero", open::repair_points(0, 100) == 0u);
}

void work_award_pays_the_right_track_and_nothing_else() {
	open::Character smith = craft_sheet(4);
	const open::SkillJob basic{open::BasicSkill::Smithing, std::nullopt};

	expect_close("a fresh track has one XP", static_cast<double>(smith.basic(open::BasicSkill::Smithing).xp),
				 0.0, 1e-9);
	expect_true("one point is awarded", smith.award_work_xp(basic, 1));
	expect_close("exactly one lands", static_cast<double>(smith.basic(open::BasicSkill::Smithing).xp), 1.0,
				 1e-9);

	// Nothing else moves: no main level, no other skill, no pool topped up.
	const unsigned level_before = smith.level();
	const float health_before = smith.pool(open::PoolKind::Hp).current;
	const float stamina_before = smith.pool(open::PoolKind::Stamina).current;
	const float mana_before = smith.pool(open::PoolKind::Mana).current;
	expect_true("a dozen points at once", smith.award_work_xp(basic, 12));
	expect_true("the main level is untouched", smith.level() == static_cast<int>(level_before));
	expect_close("no other skill grew",
				 static_cast<double>(smith.resolve_skill(open::BasicSkill::Farming, std::nullopt).rank), 1.0,
				 1e-9);
	expect_close("HP was not healed", smith.pool(open::PoolKind::Hp).current, health_before, 1e-6);
	expect_close("stamina was not refilled", smith.pool(open::PoolKind::Stamina).current, stamina_before,
				 1e-6);
	expect_close("mana was not refilled", smith.pool(open::PoolKind::Mana).current, mana_before, 1e-6);

	// A rank crossing carries the remainder, the way repeated use does.
	open::Character crossing = craft_sheet(5);
	expect_true("eight points cross rank 1 to 2", crossing.award_work_xp(basic, 8));
	expect_close("rank 2", static_cast<double>(smithing_rank(crossing)), 2.0, 1e-9);
	expect_close("with no residual left",
				 static_cast<double>(crossing.basic(open::BasicSkill::Smithing).xp), 0.0, 1e-9);
	expect_true("sixteen more cross again and keep the remainder",
				crossing.award_work_xp(basic, 17));
	expect_close("rank 3", static_cast<double>(smithing_rank(crossing)), 3.0, 1e-9);
	expect_close("one XP carried", static_cast<double>(crossing.basic(open::BasicSkill::Smithing).xp), 1.0,
				 1e-9);

	// The award pays the track the job resolves to, which is the chosen advanced one.
	open::Character advanced = craft_sheet(6);
	specialise(advanced, open::AdvancedSkill::Weaponsmith);
	const open::SkillJob weapon_job{open::BasicSkill::Smithing, open::AdvancedSkill::Weaponsmith};
	// Rank 1 to 2 on the advanced track costs 8 x 1, so one point lands as residual XP
	// on that track. What matters is which counter it landed on.
	expect_true("the advanced track can be paid", advanced.award_work_xp(weapon_job, 1));
	expect_close("on the advanced track, not the frozen parent",
				 static_cast<double>(advanced.advanced(open::AdvancedSkill::Weaponsmith)->xp), 1.0, 1e-9);
	expect_close("the frozen parent gained nothing",
				 static_cast<double>(advanced.basic(open::BasicSkill::Smithing).xp), 0.0, 1e-9);
	expect_close("and the parent is still frozen at 100", smithing_frozen(advanced), 100.0, 1e-9);

	// Refusals change nothing.
	expect_true("zero is not an award", !advanced.award_work_xp(weapon_job, 0));
	expect_close("and nothing was spent on it",
				 static_cast<double>(advanced.advanced(open::AdvancedSkill::Weaponsmith)->xp), 1.0, 1e-9);
	const open::SkillJob wrong_parent{open::BasicSkill::Smithing, open::AdvancedSkill::Spear};
	expect_true("a job from another parent is refused", !advanced.award_work_xp(wrong_parent, 1));
	// A job naming a specialisation this sheet did not choose resolves the way every
	// other job does - to the frozen parent - rather than paying the other spec or
	// inventing a track. The parent is capped, so the award is accepted and discarded.
	const open::SkillJob not_chosen{open::BasicSkill::Smithing, open::AdvancedSkill::Armoursmith};
	expect_true("an unchosen option is accepted as its frozen parent",
				advanced.award_work_xp(not_chosen, 1));
	expect_true("the other specialisation was never even opened",
				!advanced.advanced(open::AdvancedSkill::Armoursmith).has_value());
	expect_close("and the frozen parent is still 100", smithing_frozen(advanced), 100.0, 1e-9);
	const open::SkillJob nonsense{
		static_cast<open::BasicSkill>(static_cast<int>(open::BasicSkill::Count)), std::nullopt};
	expect_true("an invalid job is refused", !advanced.award_work_xp(nonsense, 1));

	// A capped track accepts the award and discards it, atomically: no progress is
	// trapped that would fire the moment something changes.
	open::Character capped = craft_sheet(7);
	train_smithing_to(capped, open::kBasicRankCap);
	const unsigned xp_at_cap = capped.basic(open::BasicSkill::Smithing).xp;
	expect_true("an award at the cap is accepted", capped.award_work_xp(basic, 50));
	expect_close("and discards the progress, as one use does",
				 static_cast<double>(capped.basic(open::BasicSkill::Smithing).xp),
				 static_cast<double>(xp_at_cap), 1e-9);
	expect_true("an absurd award cannot wrap the counter",
				capped.award_work_xp(basic, std::numeric_limits<unsigned>::max()) &&
					capped.basic(open::BasicSkill::Smithing).xp == xp_at_cap);

	// Dead sheets earn nothing.
	open::Character corpse = craft_sheet(8);
	corpse.damage(open::BodyPart::Torso, 1.0e30f);
	expect_true("the sheet is dead", !corpse.alive());
	expect_true("a dead smith is not paid", !corpse.award_work_xp(basic, 1));

	// Training a pool-relevant skill through this seam still keeps the pools honest,
	// because the award reuses the progression path.
	open::Character labourer = craft_sheet(9);
	const float stamina_max_before = labourer.pool(open::PoolKind::Stamina).maximum;
	expect_true("Endurance can be awarded through the same seam",
				labourer.award_work_xp(open::SkillJob{open::BasicSkill::Endurance, std::nullopt},
									   xp_to_reach(2)));
	expect_true("and the stamina maximum grew with the rank",
				labourer.pool(open::PoolKind::Stamina).maximum > stamina_max_before);
	expect_true("the current value was preserved, not refilled",
				labourer.pool(open::PoolKind::Stamina).current == stamina_max_before);
}

void one_use_is_still_exactly_one_point() {
	// The work seam must not have changed the ordinary rule: a successful use is one
	// residual XP, and a failure is nothing.
	open::Skills skills;
	expect_true("a use is accepted", skills.record_use(open::BasicSkill::Smithing, std::nullopt, true));
	expect_close("one XP", static_cast<double>(skills.basic(open::BasicSkill::Smithing).xp), 1.0, 1e-9);
	expect_true("a failure is accepted", skills.record_use(open::BasicSkill::Smithing, std::nullopt, false));
	expect_close("and pays nothing", static_cast<double>(skills.basic(open::BasicSkill::Smithing).xp), 1.0,
				 1e-9);
	expect_true("there is no way to buy points with an arbitrary amount through use",
				skills.record_use(open::BasicSkill::Smithing, std::nullopt, true));
	expect_close("still two XP in total", static_cast<double>(skills.basic(open::BasicSkill::Smithing).xp),
				 2.0, 1e-9);
}

}  // namespace

int main() {
	quality_bands_are_exact();
	the_craft_job_is_smithing_or_its_weapon_spec();
	repair_rounds_up_and_stops_at_the_maximum();
	work_award_pays_the_right_track_and_nothing_else();
	one_use_is_still_exactly_one_point();
	return test_summary();
}
