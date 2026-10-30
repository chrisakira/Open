#include "open/weapon_rules.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/items.hpp"
#include "open/vitals.hpp"
#include "test_support.hpp"

// Task 4 tests. Break these catches: quality smoothing the ×100 tier away,
// requirement shortfalls multiplied instead of taking the worst, trained/200
// folded twice into the same attack, a cooldown that divides by zero effectiveness
// or escapes the [1/3, 5] band, and a stamina cost that charges an attack it
// cannot pay for.
//
// These are the pure rules only. Nothing here switches the live melee path over;
// that is Task 10, and the scalar helpers still in combat_rules.hpp stay valid.

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

open::Character make_character(int level = 1, open::Attribute blessing = open::Attribute::Strength) {
	open::CharacterConfig config;
	config.id = 1;
	config.name = "Fixture";
	config.level = level;
	config.blessing = blessing;
	auto made = open::Character::create(config);
	return made.value();
}

open::WeaponStats sword_like() {
	open::WeaponStats stats;
	stats.damage = {6.0, 2.0, 0.0};
	stats.max_reach = 1.5;
	stats.speed = 1.0;
	stats.stamina_cost = 8.0;
	stats.requirements = {{open::Attribute::Dexterity, 5.0}};
	stats.required_trained_rank = 1;
	return stats;
}

const open::SkillJob kSwordJob{open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword};

// Training only through production use, the way the sheet is actually trained.
void train(open::Character &character, open::BasicSkill skill, unsigned points) {
	for (unsigned i = 0; i < points; ++i) {
		character.record_use(skill, std::nullopt, true);
	}}

void quality_is_a_seven_tier_table() {
	// Hand-checked from docs/weapons.md: tiers double, and the last tier jumps.
	const double want[7] = {1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 100.0};
	for (unsigned tier = 1; tier <= 7; ++tier) {
		const std::optional<double> got = open::quality_damage_multiplier(tier);
		expect_true("every tier has a multiplier", got.has_value());
		if (got) {
			expect_close("the tier multiplier is exact", *got, static_cast<float>(want[tier - 1]), 1e-6f);
		}
	}
	expect_true("there is no tier 0", !open::quality_damage_multiplier(0).has_value());
	expect_true("and no tier 8", !open::quality_damage_multiplier(8).has_value());
	expect_true("a huge tier is refused, not wrapped",
				!open::quality_damage_multiplier(4294967295U).has_value());
	// ×100 is the locked top tier, kept deliberately: smoothing it here would be a
	// balance change.
	const std::optional<double> seventh = open::quality_damage_multiplier(7);
	const std::optional<double> sixth = open::quality_damage_multiplier(6);
	expect_true("tier 7 is one hundred times tier 1, not a third more than tier 6",
				seventh.has_value() && sixth.has_value() && *seventh == 100.0 && *sixth == 32.0);
}

void a_shortfall_is_squared_and_the_worst_one_wins() {
	// min(actual / required, 1) squared: half gives a quarter.
	expect_close("half the requirement is a quarter", *open::requirement_effectiveness(5.0, 10.0), 0.25f,
				 1e-6f);
	expect_close("meeting it is whole", *open::requirement_effectiveness(10.0, 10.0), 1.0f, 1e-6f);
	expect_close("exceeding it is still whole", *open::requirement_effectiveness(900.0, 10.0), 1.0f,
				 1e-6f);
	expect_close("three quarters of it", *open::requirement_effectiveness(75.0, 100.0), 0.5625f, 1e-6f);
	expect_close("nothing is zero", *open::requirement_effectiveness(0.0, 10.0), 0.0f, 1e-6f);
	expect_close("one third squared", *open::requirement_effectiveness(1.0, 3.0), 0.1111111f, 1e-5f);

	expect_true("a non-positive requirement is not a requirement",
				!open::requirement_effectiveness(5.0, 0.0).has_value() &&
					!open::requirement_effectiveness(5.0, -1.0).has_value());
	expect_true("a negative actual is nonsense",
				!open::requirement_effectiveness(-1.0, 10.0).has_value());
	expect_true("non-finite is refused rather than compared",
				!open::requirement_effectiveness(kNan, 10.0).has_value() &&
					!open::requirement_effectiveness(kInf, 10.0).has_value() &&
					!open::requirement_effectiveness(5.0, kNan).has_value() &&
					!open::requirement_effectiveness(5.0, kInf).has_value());
}

void weapon_effectiveness_takes_the_minimum_of_every_requirement() {
	open::Character character = make_character();

	// One requirement, exactly met: a level 1 sheet with Dexterity base 5 is
	// effective 5.
	open::WeaponStats stats = sword_like();
	character.set_base(open::Attribute::Dexterity, 5.0f);
	const std::optional<double> met = open::weapon_effectiveness(character, stats, kSwordJob);
	expect_true("a met requirement set is fully effective", met.has_value());
	if (met) {
		expect_close("meeting every requirement is 1", *met, 1.0f, 1e-6f);
	}

	// The same sword in a weaker hand: (1/5)^2.
	open::Character weak = make_character();
	const std::optional<double> shortfall = open::weapon_effectiveness(weak, stats, kSwordJob);
	expect_true("a shortfall is reported", shortfall.has_value());
	if (shortfall) {
		expect_close("one fifth squared", *shortfall, 0.04f, 1e-6f);
	}

	// Requirements check the effective attribute: level multiplies it, and a
	// blessing on that attribute multiplies it. Neither is a separate requirement.
	open::Character leveled = make_character(2);  // x2
	leveled.set_base(open::Attribute::Dexterity, 3.0f);
	const std::optional<double> doubled = open::weapon_effectiveness(leveled, stats, kSwordJob);
	expect_true("level is inside the attribute, so it is not its own gate", doubled.has_value());
	if (doubled) {
		expect_close("base 3 at level 2 is effective 6, which meets 5", *doubled, 1.0f, 1e-6f);
	}
	open::Character blessed = make_character(1, open::Attribute::Dexterity);
	blessed.set_base(open::Attribute::Dexterity, 4.0f);  // 4 x 1.25 = 5
	const std::optional<double> blessed_effect = open::weapon_effectiveness(blessed, stats, kSwordJob);
	expect_true("the blessing is inside the attribute too", blessed_effect.has_value());
	if (blessed_effect) {
		expect_close("base 4 blessed is effective 5", *blessed_effect, 1.0f, 1e-6f);
	}

	// The trained job is a requirement of its own, on the trained rank.
	open::WeaponStats hardened = sword_like();
	hardened.required_trained_rank = 4;
	open::Character novice = make_character();
	novice.set_base(open::Attribute::Dexterity, 5.0f);
	const std::optional<double> novice_effect = open::weapon_effectiveness(novice, hardened, kSwordJob);
	expect_true("an untrained job is a shortfall", novice_effect.has_value());
	if (novice_effect) {
		expect_close("one quarter of the required rank is one sixteenth", *novice_effect, 0.0625f, 1e-6f);
	}
	open::Character trained = novice;
	// Rank 1 -> 2 costs 8 x 1 and 2 -> 3 costs 8 x 2, so 26 successful uses.
	train(trained, open::BasicSkill::OneHandedWeapons, 26);
	expect_true("the fixture really is rank 3",
				trained.resolve_skill(open::BasicSkill::OneHandedWeapons, std::nullopt).rank == 3u);
	const std::optional<double> trained_effect = open::weapon_effectiveness(trained, hardened, kSwordJob);
	expect_true("training moves it", trained_effect.has_value());
	if (trained_effect) {
		expect_close("three quarters squared", *trained_effect, 0.5625f, 1e-6f);
	}

	// The worst shortfall limits the weapon; shortfalls never multiply together.
	open::WeaponStats both = sword_like();
	both.requirements = {{open::Attribute::Dexterity, 10.0}};  // effective 5 of 10 -> 0.25
	both.required_trained_rank = 4;
	open::Character limited = make_character();
	limited.set_base(open::Attribute::Dexterity, 5.0f);
	train(limited, open::BasicSkill::OneHandedWeapons, 26);  // rank 3 of 4 -> 0.5625
	const std::optional<double> minimum = open::weapon_effectiveness(limited, both, kSwordJob);
	expect_true("both shortfalls are considered", minimum.has_value());
	if (minimum) {
		expect_close("the minimum is taken, not the product", *minimum, 0.25f, 1e-6f);
	}

	// trained / 200 is a separate damage multiplier, never folded in here: rank 1
	// meeting a rank 1 requirement is fully effective, not 0.005 effective.
	open::Character fresh = make_character();
	fresh.set_base(open::Attribute::Dexterity, 5.0f);
	const std::optional<double> untrained_but_met = open::weapon_effectiveness(fresh, stats, kSwordJob);
	expect_true("meeting a rank-1 requirement at rank 1 is whole", untrained_but_met.has_value());
	if (untrained_but_met) {
		expect_close("trained/200 is not part of effectiveness", *untrained_but_met, 1.0f, 1e-6f);
	}

	// An absent requirement is omitted, not zero: a weapon with none is always
	// fully effective, and the unarmed fallback lives here.
	open::WeaponStats bare = sword_like();
	bare.requirements.clear();
	bare.required_trained_rank = 1;
	const std::optional<double> bare_effect =
		open::weapon_effectiveness(weak, bare, {open::BasicSkill::UnarmedCombat, std::nullopt});
	expect_true("no requirements means no penalty", bare_effect.has_value());
	if (bare_effect) {
		expect_close("an unrequirement is 1", *bare_effect, 1.0f, 1e-6f);
	}

	// A declared zero rank would divide by zero; the catalog forbids it and so does
	// this helper.
	open::WeaponStats broken = sword_like();
	broken.required_trained_rank = 0;
	expect_true("a zero required rank is refused",
				!open::weapon_effectiveness(weak, broken, kSwordJob).has_value());
	open::WeaponStats broken_need = sword_like();
	broken_need.requirements = {{open::Attribute::Dexterity, 0.0}};
	expect_true("a zero requirement value is refused",
				!open::weapon_effectiveness(weak, broken_need, kSwordJob).has_value());
}

void a_weaker_swing_costs_more_stamina() {
	// cost = base x (2 - effectiveness): whole costs base, nothing costs double.
	expect_close("full effectiveness costs the base", open::weapon_attack_cost(8.0, 1.0), 8.0f, 1e-6f);
	expect_close("a quarter effective costs 1.75x", open::weapon_attack_cost(8.0, 0.25), 14.0f, 1e-6f);
	expect_close("zero effective costs double", open::weapon_attack_cost(8.0, 0.0), 16.0f, 1e-6f);
	expect_close("the dagger's cheaper swing scales the same way",
				 open::weapon_attack_cost(6.0, 0.5), 9.0f, 1e-6f);
	expect_close("a fractional cost stays fractional", open::weapon_attack_cost(0.5, 0.5), 0.75f, 1e-6f);
	expect_close("a free action stays free", open::weapon_attack_cost(0.0, 0.0), 0.0f, 1e-6f);
}

void the_cooldown_stays_in_band_and_never_divides_by_zero() {
	// rate = (0.2 + 2.8 x trained/200) x speed x effectiveness, cooldown = 1/rate,
	// clamped to [1/3, 5]; exhausted doubles and reclamps.
	expect_close("rank 0 is the formula floor", open::weapon_cooldown(0, 1.0, 1.0, false), 5.0f, 1e-4f);
	expect_close("rank 1", open::weapon_cooldown(1, 1.0, 1.0, false), 4.6728972f, 1e-4f);
	expect_close("rank 100", open::weapon_cooldown(100, 1.0, 1.0, false), 0.625f, 1e-5f);
	expect_close("rank 200 is clamped at the fastest", open::weapon_cooldown(200, 1.0, 1.0, false),
				 0.3333333f, 1e-5f);

	// Weapon speed multiplies the rate, and the band still binds at both ends.
	expect_close("a fast weapon cannot beat the floor", open::weapon_cooldown(200, 2.0, 1.0, false),
				 0.3333333f, 1e-5f);
	expect_close("the slowest legal multiplier", open::weapon_cooldown(200, 0.1, 1.0, false), 3.3333333f,
				 1e-4f);
	expect_close("a slow multiplier on a novice hits the ceiling",
				 open::weapon_cooldown(1, 0.1, 1.0, false), 5.0f, 1e-4f);
	expect_close("effectiveness scales the rate", open::weapon_cooldown(100, 1.0, 0.25, false), 2.5f,
				 1e-5f);

	// Zero effectiveness is the slowest permitted cooldown, not an infinity.
	expect_close("no effectiveness swings as slowly as the game allows",
				 open::weapon_cooldown(100, 1.0, 0.0, false), 5.0f, 1e-5f);
	expect_close("and exhaustion cannot push it past the ceiling",
				 open::weapon_cooldown(100, 1.0, 0.0, true), 5.0f, 1e-5f);

	// Exhaustion doubles, then reclamps: both ends of the band are reachable.
	expect_close("exhausted at rank 100", open::weapon_cooldown(100, 1.0, 1.0, true), 1.25f, 1e-5f);
	expect_close("exhausted at rank 0 reclamps to 5", open::weapon_cooldown(0, 1.0, 1.0, true), 5.0f,
				 1e-5f);
	expect_close("exhausted at rank 200 leaves the floor", open::weapon_cooldown(200, 1.0, 1.0, true),
				 0.6666667f, 1e-5f);
	// docs/combat.md section 4 fixes the order: clamp, then double, then clamp
	// again. A fast weapon's clamped floor therefore becomes two thirds, not a
	// third: doubling the unclamped rate first would be the bug this catches.
	expect_close("exhaustion doubles the clamped cooldown, not the raw rate",
				 open::weapon_cooldown(200, 2.0, 1.0, true), 0.6666667f, 1e-5f);

	// Nonsense cannot produce a nonsense clock: the slowest legal value is the safe
	// answer, and it is never infinite or NaN.
	const float nonsense = open::weapon_cooldown(100, kNan, 1.0, false);
	expect_true("a NaN multiplier cannot make an infinite or NaN clock",
				std::isfinite(nonsense) && nonsense == 5.0f);
	const float negative = open::weapon_cooldown(100, 1.0, -1.0, false);
	expect_true("a negative effectiveness cannot make a negative clock",
				std::isfinite(negative) && negative == 5.0f);
}

void an_attack_pays_when_it_can_and_nothing_when_it_cannot() {
	open::Character character = make_character();
	const float full = character.pool(open::PoolKind::Stamina).current;
	expect_true("the sheet starts with a full bar", full == 100.0f);

	expect_true("zero is a legal cost and spends nothing", character.spend_stamina(0.0f));
	expect_close("nothing was spent", character.pool(open::PoolKind::Stamina).current, 100.0f, 1e-5f);

	expect_true("an affordable cost is paid", character.spend_stamina(8.0f));
	expect_close("exactly once", character.pool(open::PoolKind::Stamina).current, 92.0f, 1e-5f);

	// Fractional costs are real: the requirement formula produces them.
	expect_true("a fractional cost is paid", character.spend_stamina(2.5f));
	expect_close("to the last tenth", character.pool(open::PoolKind::Stamina).current, 89.5f, 1e-4f);

	// All-or-nothing: short by a hair pays nothing at all.
	while (character.pool(open::PoolKind::Stamina).current > 8.0f) {
		character.spend_stamina(1.0f);
	}
	const float before = character.pool(open::PoolKind::Stamina).current;
	expect_true("8.01 cannot be paid from 8.00", !character.spend_stamina(before + 0.01f));
	expect_close("and nothing left the bar", character.pool(open::PoolKind::Stamina).current, before,
				 1e-6f);
	expect_true("the exact amount is affordable", character.spend_stamina(before));
	expect_close("leaving zero", character.pool(open::PoolKind::Stamina).current, 0.0f, 1e-6f);
	expect_true("an empty bar pays nothing", !character.spend_stamina(0.5f));

	// Invalid amounts never touch the pool.
	open::Character pristine = make_character();
	expect_true("a negative cost is refused", !pristine.spend_stamina(-1.0f));
	expect_true("NaN is refused", !pristine.spend_stamina(std::numeric_limits<float>::quiet_NaN()));
	expect_true("infinity is refused", !pristine.spend_stamina(std::numeric_limits<float>::infinity()));
	expect_close("the bar is untouched after three refusals",
				 pristine.pool(open::PoolKind::Stamina).current, 100.0f, 1e-6f);

	// Death gates spending, exactly as the flat swing cost does.
	open::Character dead = make_character();
	dead.damage(open::BodyPart::Torso, 1.0e30f);
	expect_true("the sheet is dead", !dead.alive());
	expect_true("a dead character pays nothing", !dead.spend_stamina(1.0f));
	expect_true("not even zero", !dead.spend_stamina(0.0f));

	// The flat per-use API is unchanged: a swing is still one swing, and bulk
	// purchase stays refused.
	open::Character legacy = make_character();
	expect_true("one swing still buys", legacy.spend(open::ResourceUse::MeleeSwing, 1.0f));
	expect_true("two swings are not one purchase", !legacy.spend(open::ResourceUse::MeleeSwing, 2.0f));
	expect_true("and a negative quantity is refused", !legacy.spend(open::ResourceUse::MeleeSwing, -1.0f));
}

}  // namespace

int main() {
	quality_is_a_seven_tier_table();
	a_shortfall_is_squared_and_the_worst_one_wins();
	weapon_effectiveness_takes_the_minimum_of_every_requirement();
	a_weaker_swing_costs_more_stamina();
	the_cooldown_stays_in_band_and_never_divides_by_zero();
	an_attack_pays_when_it_can_and_nothing_when_it_cannot();
	return test_summary();
}
