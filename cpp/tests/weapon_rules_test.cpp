#include "open/weapon_rules.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/equipment.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"
#include "open/vitals.hpp"
#include "test_support.hpp"

#include <memory>
#include <vector>

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


// ---------- Task 5: the loadout a swing is resolved from ----------

constexpr open::DefinitionId kSwordDef = 501;
constexpr open::DefinitionId kDaggerDef = 502;
constexpr open::DefinitionId kSpearDef = 503;
constexpr open::DefinitionId kHammerDef = 504;
constexpr open::DefinitionId kShieldDef = 505;
constexpr open::MaterialId kStarterMaterial = 611;

open::WeaponStats wield(open::DamageComponents damage, double min_reach, double max_reach, double block,
						double cost = 8.0) {
	open::WeaponStats weapon;
	weapon.damage = damage;
	weapon.min_reach = min_reach;
	weapon.max_reach = max_reach;
	weapon.speed = 1.0;
	weapon.stamina_cost = cost;
	weapon.block_percent = block;
	weapon.weight = 2.0;
	weapon.required_trained_rank = 1;
	return weapon;
}

open::ItemDefinition hand_item(open::DefinitionId id, const char *model, open::HandUse use) {
	open::ItemDefinition item;
	item.id = id;
	item.model_key = model;
	item.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
	item.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
	item.hand_use = use;
	return item;
}

// One fixture catalog, two dials: shield mitigation and how demanding the sword's
// requirement is. Everything else is the starter shape.
std::shared_ptr<const open::ItemCatalog> loadout_catalog(double shield_block = 60.0,
														 double sword_dexterity = 5.0) {
	std::vector<open::ItemDefinition> items;

	open::ItemDefinition sword = hand_item(kSwordDef, "Sword", open::HandUse::OneHanded);
	open::WeaponDefinition sword_role;
	sword_role.job = {open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword};
	sword_role.stats = wield({6.0, 2.0, 0.0}, 0.0, 1.5, 25.0);
	sword_role.stats.requirements = {{open::Attribute::Dexterity, sword_dexterity}};
	sword.weapon = sword_role;
	items.push_back(sword);

	open::ItemDefinition dagger = hand_item(kDaggerDef, "Dagger", open::HandUse::OneHanded);
	open::WeaponDefinition dagger_role;
	dagger_role.job = {open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Dagger};
	dagger_role.stats = wield({2.0, 5.0, 0.0}, 0.0, 1.0, 15.0, 6.0);
	dagger.weapon = dagger_role;
	items.push_back(dagger);

	open::ItemDefinition spear = hand_item(kSpearDef, "Spear", open::HandUse::TwoHanded);
	open::WeaponDefinition spear_role;
	spear_role.job = {open::BasicSkill::Polearms, open::AdvancedSkill::Spear};
	spear_role.stats = wield({0.0, 8.0, 0.0}, 0.6, 2.0, 20.0);
	spear.weapon = spear_role;
	items.push_back(spear);

	open::ItemDefinition hammer = hand_item(kHammerDef, "Hammer_Small", open::HandUse::OneHanded);
	open::WeaponDefinition hammer_role;
	hammer_role.job = {open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Mace};
	hammer_role.stats = wield({0.0, 0.0, 8.0}, 0.0, 1.3, 20.0);
	hammer.weapon = hammer_role;
	items.push_back(hammer);

	open::ItemDefinition shield = hand_item(kShieldDef, "Shield_Round", open::HandUse::OneHanded);
	open::ShieldDefinition shield_role;
	shield_role.job = {open::BasicSkill::Blocking, open::AdvancedSkill::SmallShields};
	shield_role.block_percent = shield_block;
	shield_role.weight = 3.0;
	shield.shield = shield_role;
	items.push_back(shield);

	open::MaterialDefinition material;
	material.id = kStarterMaterial;
	material.max_durability_by_quality = {100, 100, 100, 100, 100, 100, 100};

	auto made = open::ItemCatalog::create(std::move(items), {material});
	if (!made.has_value()) {
		g_failed++;
		return nullptr;
	}
	return std::make_shared<open::ItemCatalog>(std::move(made).value());
}

// The blessing matters: it is inside an effective attribute, so a Strength blessing
// quietly makes Blocking the better defense. Tests about the comparison say which
// blessing the sheet has.
open::Character wielder(const open::ItemCatalog &made,
						open::Attribute blessing = open::Attribute::Strength) {
	open::CharacterConfig setup;
	setup.id = 1;
	setup.name = "Wielder";
	setup.level = 1;
	setup.blessing = blessing;
	auto armed = open::Character::create(setup, std::make_shared<open::ItemCatalog>(made));
	if (!armed.has_value()) {
		g_failed++;
	}
	return std::move(armed).value();
}

// Owns an instance and wears it. A fixture that silently failed to hold its weapon
// would test the wrong thing, so every step reports.
void wear(open::Character &who, open::DefinitionId what, open::ItemId which, open::Slot where,
		  unsigned quality = 1) {
	const std::shared_ptr<const open::ItemCatalog> made = who.catalog();
	if (made == nullptr) {
		expect_true("the fixture has a catalog to describe its item", false);
		return;
	}
	const std::optional<open::ItemInstance> instance =
		open::make_item_instance(*made, what, kStarterMaterial, which, quality);
	if (!instance.has_value()) {
		expect_true("the fixture can describe the item", false);
		return;
	}
	expect_true("the fixture can carry it", who.add_instance(*instance));
	expect_true("and wear it", who.equip(which, where));
}

// A tagged record with no definition behind it: placeable, worthless in combat.
void wear_undefined(open::Character &who, open::ItemId which) {
	open::ItemRef legacy;
	legacy.id = which;
	legacy.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
	legacy.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
	legacy.hand_use = open::HandUse::OneHanded;
	legacy.combat_skill = open::BasicSkill::OneHandedWeapons;
	legacy.combat_option = open::AdvancedSkill::Sword;
	expect_true("the undefined record is placeable", who.add_item(legacy));
	expect_true("and wearable", who.equip(which, open::Slot::RightHand));
}

// Basic to 100, the way the sheet trains.
void master(open::Character &who, open::BasicSkill skill) {
	while (who.resolve_skill(skill, std::nullopt).rank < 100u) {
		who.record_use(skill, std::nullopt, true);
	}
}

void the_right_hand_is_the_attack_and_a_shield_never_is() {
	const std::shared_ptr<const open::ItemCatalog> made = loadout_catalog();
	if (made == nullptr) {
		return;
	}

	// Nothing held: the locked unarmed fallback, with the numbers docs/weapons.md
	// gives it, and nothing of its own to wear out.
	open::Character empty = wielder(*made);
	const std::optional<open::WeaponAttackProfile> bare = open::weapon_attack_profile(empty);
	expect_true("an empty character still fights", bare.has_value());
	if (bare) {
		expect_true("unarmed names no item", !bare->item.has_value());
		expect_true("unarmed is Unarmed Combat", bare->job.basic == open::BasicSkill::UnarmedCombat);
		expect_true("0 cut, 0 pierce, 8 blunt", bare->stats.damage.cut == 0.0 &&
													bare->stats.damage.pierce == 0.0 &&
													bare->stats.damage.blunt == 8.0);
		expect_true("reach 0 to 1.5, cost 8, speed 1",
					bare->stats.min_reach == 0.0 && bare->stats.max_reach == 1.5 &&
						bare->stats.stamina_cost == 8.0 && bare->stats.speed == 1.0);
		expect_close("unarmed is always fully effective", bare->effectiveness, 1.0, 1e-9);
		expect_true("nothing to wear, and tier 1 so quality multiplies by 1",
					bare->durability == 0 && bare->quality == 1);
		expect_true("unarmed declares no requirements", bare->stats.requirements.empty() &&
														   bare->stats.required_trained_rank == 0);
	}

	// Two one-handed weapons: the right hand attacks.
	open::Character dual = wielder(*made);
	wear(dual, kDaggerDef, 701, open::Slot::LeftHand);
	wear(dual, kSwordDef, 702, open::Slot::RightHand);
	const std::optional<open::WeaponAttackProfile> right = open::weapon_attack_profile(dual);
	expect_true("the right hand attacks",
				right.has_value() && right->item.has_value() && *right->item == 702);
	if (right) {
		expect_close("with the sword's cut", right->stats.damage.cut, 6.0, 1e-9);
		expect_true("and the sword's own job",
					right->job.option.has_value() && *right->job.option == open::AdvancedSkill::Sword);
	}

	// A right-hand record that nothing describes is not a weapon: the scan falls
	// through to the defined item in the other hand instead of disabling the sheet.
	open::Character stale = wielder(*made);
	wear(stale, kDaggerDef, 703, open::Slot::LeftHand);
	wear_undefined(stale, 704);
	const std::optional<open::WeaponAttackProfile> fallback = open::weapon_attack_profile(stale);
	expect_true("the defined left hand is used",
				fallback.has_value() && fallback->item.has_value() && *fallback->item == 703);
	if (fallback) {
		expect_close("with the dagger's pierce", fallback->stats.damage.pierce, 5.0, 1e-9);
		expect_close("and the dagger's cheaper cost", fallback->stats.stamina_cost, 6.0, 1e-9);
	}

	// A shield never attacks, alone or beside a weapon.
	open::Character shield_only = wielder(*made);
	wear(shield_only, kShieldDef, 705, open::Slot::LeftHand);
	const std::optional<open::WeaponAttackProfile> shielded = open::weapon_attack_profile(shield_only);
	expect_true("a shield alone fights unarmed", shielded.has_value() && !shielded->item.has_value());

	open::Character both_hands = wielder(*made);
	wear(both_hands, kShieldDef, 706, open::Slot::LeftHand);
	wear(both_hands, kSwordDef, 707, open::Slot::RightHand);
	const std::optional<open::WeaponAttackProfile> mixed = open::weapon_attack_profile(both_hands);
	expect_true("the sword attacks while the shield guards",
				mixed.has_value() && mixed->item.has_value() && *mixed->item == 707);

	// A two-handed reservation names both hands and is one weapon, once.
	open::Character spearman = wielder(*made);
	wear(spearman, kSpearDef, 708, open::Slot::LeftHand);
	const std::optional<open::ItemId> left = spearman.equipped(open::Slot::LeftHand);
	const std::optional<open::ItemId> reserved = spearman.equipped(open::Slot::RightHand);
	expect_true("the spear reserves both hands",
				left.has_value() && reserved.has_value() && *left == *reserved);
	const std::optional<open::WeaponAttackProfile> spear = open::weapon_attack_profile(spearman);
	expect_true("and is read once",
				spear.has_value() && spear->item.has_value() && *spear->item == 708 &&
					spear->job.basic == open::BasicSkill::Polearms);
	const std::optional<open::WeaponDefendProfile> spear_guard = open::weapon_defend_profile(spearman);
	expect_true("one reservation blocks once, and does not double its mitigation",
				spear_guard.has_value() && spear_guard->action == open::DefendAction::Block &&
					spear_guard->mitigation == 20.0 && spear_guard->item.has_value() &&
					*spear_guard->item == 708);

	// Both arms gone: held gear stays on, and the swing is unarmed.
	open::Character maimed = wielder(*made);
	wear(maimed, kSwordDef, 709, open::Slot::RightHand);
	// Each arm to exactly zero. Damage also leaves main HP, so the amount is the
	// part's own maximum rather than an avalanche that would kill the sheet: the
	// debuff under test is a live character with nothing to hold.
	const float arm_max = maimed.body_part(open::BodyPart::LeftArm).maximum;
	expect_true("an empty arm is still a full hit", maimed.damage(open::BodyPart::LeftArm, arm_max));
	expect_true("and the other", maimed.damage(open::BodyPart::RightArm, arm_max));
	expect_true("the sheet survives its arms", maimed.alive());
	expect_true("the sword is still worn", maimed.equipped(open::Slot::RightHand).has_value());
	const std::optional<open::WeaponAttackProfile> helpless = open::weapon_attack_profile(maimed);
	expect_true("with both arms gone the swing is unarmed",
				helpless.has_value() && !helpless->item.has_value());
	const std::optional<open::WeaponDefendProfile> helpless_guard = open::weapon_defend_profile(maimed);
	expect_true("and the answer is a dodge",
				helpless_guard.has_value() && helpless_guard->action == open::DefendAction::Dodge);

	// Death has no loadout in either direction.
	open::Character corpse = wielder(*made);
	wear(corpse, kSwordDef, 710, open::Slot::RightHand);
	corpse.damage(open::BodyPart::Torso, 1.0e30f);
	expect_true("a dead character attacks with nothing",
				!open::weapon_attack_profile(corpse).has_value());
	expect_true("and defends with nothing", !open::weapon_defend_profile(corpse).has_value());
}

void the_profile_reads_the_definition_and_the_instance() {
	const std::shared_ptr<const open::ItemCatalog> made = loadout_catalog();
	if (made == nullptr) {
		return;
	}

	// A chosen matching specialisation is used from rank 1, not a frozen zero.
	open::Character swordsman = wielder(*made);
	swordsman.set_base(open::Attribute::Dexterity, 5.0f);  // meets the starter requirement
	master(swordsman, open::BasicSkill::OneHandedWeapons);
	expect_true("the parent froze at 100", swordsman.advance(open::AdvancedSkill::Sword));
	wear(swordsman, kSwordDef, 711, open::Slot::RightHand);
	const std::optional<open::WeaponAttackProfile> sword = open::weapon_attack_profile(swordsman);
	expect_true("the sword reads its own spec",
				sword.has_value() && sword->job.option.has_value() &&
					*sword->job.option == open::AdvancedSkill::Sword);
	if (sword) {
		expect_true("a fresh spec is rank 1", sword->trained == 1u);
		expect_true("the item id is the instance, not the definition", *sword->item == 711);
		expect_close("quality 1", static_cast<double>(sword->quality), 1.0, 1e-9);
		expect_close("full durability", static_cast<double>(sword->durability), 100.0, 1e-9);
		expect_close("meeting its own requirement is whole", sword->effectiveness, 1.0, 1e-9);
	}

	// The same sheet with a Mace they did not specialise in uses the frozen parent.
	open::Character mixed = swordsman;
	wear(mixed, kHammerDef, 712, open::Slot::LeftHand);
	expect_true("the mace moves to the attacking hand",
				mixed.unequip(open::Slot::LeftHand) && mixed.unequip(open::Slot::RightHand) &&
					mixed.equip(712, open::Slot::RightHand));
	const std::optional<open::WeaponAttackProfile> hammer = open::weapon_attack_profile(mixed);
	expect_true("a mace in a Sword specialist's hand is still a mace job",
				hammer.has_value() && hammer->job.option.has_value() &&
					*hammer->job.option == open::AdvancedSkill::Mace);
	if (hammer) {
		expect_true("so the frozen parent pays", hammer->trained == 100u);
		expect_close("and a rank-1 requirement is met", hammer->effectiveness, 1.0, 1e-9);
	}

	// Being under requirement changes effectiveness, not whether the weapon is used:
	// it is still swung, just badly.
	const std::shared_ptr<const open::ItemCatalog> demanding = loadout_catalog(60.0, 20.0);
	if (demanding == nullptr) {
		return;
	}
	open::Character puny = wielder(*demanding);
	wear(puny, kSwordDef, 713, open::Slot::RightHand);
	const std::optional<open::WeaponAttackProfile> struggling = open::weapon_attack_profile(puny);
	expect_true("under requirement is still a weapon", struggling.has_value());
	if (struggling) {
		expect_close("effective Dexterity 1 of 20 is one twentieth squared", struggling->effectiveness,
					 0.0025, 1e-6);
		expect_close("the cost rises with the shortfall",
					 open::weapon_attack_cost(struggling->stats.stamina_cost, struggling->effectiveness),
					 15.98, 0.01);
	}

	// Durability reported by the profile is the instance's, as of now, and wear
	// moves it without moving anything else.
	open::Character creaky = wielder(*made);
	wear(creaky, kSwordDef, 714, open::Slot::RightHand);
	const std::optional<open::WeaponAttackProfile> before = open::weapon_attack_profile(creaky);
	expect_true("it starts whole", before.has_value() && before->durability == 100u);
	expect_true("one hit wears it", creaky.apply_wear(714, 1));
	const std::optional<open::WeaponAttackProfile> after = open::weapon_attack_profile(creaky);
	expect_true("the profile reports 99 and the same maximum",
				after.has_value() && after->durability == 99u && after->max_durability == 100u);
	expect_true("and a quality-4 sword reads its own tier",
				[&] {
					open::Character fine = wielder(*made);
					wear(fine, kSwordDef, 715, open::Slot::RightHand, 4);
					const std::optional<open::WeaponAttackProfile> profile =
						open::weapon_attack_profile(fine);
					return profile.has_value() && profile->quality == 4u &&
						   profile->durability == 100u;
				}());

	// Capability is read at query time, so a profile does not vanish because a
	// cooldown is running or an order was cleared: gates stop the action, they do
	// not delete the loadout and cancel the order with it.
	open::Character waiting = wielder(*made);
	wear(waiting, kSwordDef, 716, open::Slot::RightHand);
	const std::optional<open::WeaponAttackProfile> ready = open::weapon_attack_profile(waiting);
	waiting.set_attack_target(2);
	waiting.advance_combat_clocks(2.0f);
	const std::optional<open::WeaponAttackProfile> cooling = open::weapon_attack_profile(waiting);
	expect_true("a running cooldown does not change the loadout",
				ready.has_value() && cooling.has_value() && cooling->item == ready->item &&
					cooling->trained == ready->trained && cooling->stats.max_reach == ready->stats.max_reach);
	waiting.clear_order();
	const std::optional<open::WeaponAttackProfile> unordered = open::weapon_attack_profile(waiting);
	expect_true("nor does cancelling the order", unordered.has_value() && unordered->item.has_value());
}

void defense_priority_is_shield_then_weapon_or_dodge() {
	const std::shared_ptr<const open::ItemCatalog> made = loadout_catalog();
	if (made == nullptr) {
		return;
	}

	// A usable shield wins, whatever the skills say.
	open::Character shielded = wielder(*made);
	wear(shielded, kShieldDef, 717, open::Slot::LeftHand);
	const std::optional<open::WeaponDefendProfile> guard = open::weapon_defend_profile(shielded);
	expect_true("the shield blocks",
				guard.has_value() && guard->action == open::DefendAction::Block &&
					guard->item.has_value() && *guard->item == 717);
	if (guard) {
		expect_close("at its declared mitigation", guard->mitigation, 60.0, 1e-9);
		expect_true("with the shield's own job",
					guard->job.basic == open::BasicSkill::Blocking && guard->job.option.has_value() &&
						*guard->job.option == open::AdvancedSkill::SmallShields);
		expect_close("its clock is the trained rank", static_cast<double>(guard->trained), 1.0, 1e-9);
	}

	// A shield worn to nothing is still the shield: reduced mitigation is not no
	// shield, and the profile reports the durability the caller must discount by.
	open::Character battered = wielder(*made);
	wear(battered, kShieldDef, 718, open::Slot::LeftHand);
	expect_true("it wears out", battered.apply_wear(718, 100));
	const std::optional<open::WeaponDefendProfile> broken_guard = open::weapon_defend_profile(battered);
	expect_true("and is still chosen, with its mitigation and durability reported",
				broken_guard.has_value() && broken_guard->action == open::DefendAction::Block &&
					broken_guard->durability == 0u && broken_guard->mitigation == 60.0);

	// A 0% shield is still a shield, and prevents nothing.
	const std::shared_ptr<const open::ItemCatalog> useless = loadout_catalog(0.0);
	if (useless != nullptr) {
		open::Character holder = wielder(*useless);
		wear(holder, kShieldDef, 719, open::Slot::LeftHand);
		const std::optional<open::WeaponDefendProfile> nothing = open::weapon_defend_profile(holder);
		expect_true("a 0% shield is chosen and mitigates nothing",
					nothing.has_value() && nothing->action == open::DefendAction::Block &&
						nothing->mitigation == 0.0);
	}

	// No shield, a usable weapon: tie goes to dodge, and a dodge is worth everything.
	// Blessed on neither skill's link, so both sides of the comparison really are equal.
	open::Character armed = wielder(*made, open::Attribute::Constitution);
	wear(armed, kSwordDef, 720, open::Slot::RightHand);
	const std::optional<open::WeaponDefendProfile> dodge = open::weapon_defend_profile(armed);
	expect_true("equal skills answer with a dodge",
				dodge.has_value() && dodge->action == open::DefendAction::Dodge &&
					!dodge->item.has_value());
	if (dodge) {
		expect_close("a dodge mitigates everything", dodge->mitigation, 100.0, 1e-9);
		expect_true("with the dodge job", dodge->job.basic == open::BasicSkill::Dodging);
		expect_close("and its trained rank for the clock", static_cast<double>(dodge->trained), 1.0, 1e-9);
	}

	// Better Blocking attributes than Dodging ones flip it to a weapon block, which
	// borrows no shield specialisation.
	open::Character blocker = wielder(*made);
	wear(blocker, kSwordDef, 721, open::Slot::RightHand);
	blocker.set_base(open::Attribute::Strength, 100.0f);
	blocker.set_base(open::Attribute::Toughness, 100.0f);
	const std::optional<open::WeaponDefendProfile> blocked = open::weapon_defend_profile(blocker);
	expect_true("a better blocker blocks with the weapon",
				blocked.has_value() && blocked->action == open::DefendAction::Block &&
					blocked->item.has_value() && *blocked->item == 721);
	if (blocked) {
		expect_close("with the weapon's own mitigation", blocked->mitigation, 25.0, 1e-9);
		expect_true("as the frozen basic Blocking",
					blocked->job.basic == open::BasicSkill::Blocking && !blocked->job.option.has_value());
		expect_close("and the durability of that weapon", static_cast<double>(blocked->durability), 100.0,
					 1e-9);
	}

	// The comparison is on effective skills and the clock is on the trained rank:
	// raising an attribute changes which defense is chosen and not what its clock
	// reads. Dodging links Agility alone, so an Agility blessing is enough to flip
	// the choice with no training at all.
	open::Character blessed = wielder(*made);
	wear(blessed, kSwordDef, 722, open::Slot::RightHand);
	blessed.set_base(open::Attribute::Agility, 500.0f);
	const std::optional<open::WeaponDefendProfile> dodging = open::weapon_defend_profile(blessed);
	expect_true("a dextrous sheet dodges",
				dodging.has_value() && dodging->action == open::DefendAction::Dodge && dodging->trained == 1u);

	// Unarmed answers with a dodge and no item.
	open::Character bare = wielder(*made);
	const std::optional<open::WeaponDefendProfile> unarmed_guard = open::weapon_defend_profile(bare);
	expect_true("unarmed dodges", unarmed_guard.has_value() && unarmed_guard->action == open::DefendAction::Dodge &&
									  !unarmed_guard->item.has_value() && unarmed_guard->durability == 0u);

	// A sheet with no catalog cannot describe a held record, so it has no defined
	// weapon: the locked unarmed fallback, not invented stats.
	open::CharacterConfig setup;
	setup.id = 2;
	setup.name = "Blind";
	setup.level = 1;
	setup.blessing = open::Attribute::Strength;
	auto blind_character = open::Character::create(setup);
	if (blind_character.has_value()) {
		open::Character blind = std::move(blind_character).value();
		wear_undefined(blind, 723);
		const std::optional<open::WeaponAttackProfile> undefined = open::weapon_attack_profile(blind);
		expect_true("with nothing described the swing is unarmed",
					undefined.has_value() && !undefined->item.has_value());
		const std::optional<open::WeaponDefendProfile> undefined_guard =
			open::weapon_defend_profile(blind);
		expect_true("and nothing is blocked with", undefined_guard.has_value() &&
													   undefined_guard->action == open::DefendAction::Dodge &&
													   !undefined_guard->item.has_value());
	}
}

// ---------- the unarmed fallback's own job ----------

// Bare hands are Unarmed Combat, and they follow the sheet's chosen unarmed track: this
// is the one job a swing can take that no item declares, so the profile has to read it
// off the sheet.
void the_unarmed_fallback_follows_the_unarmed_track() {
	const std::shared_ptr<const open::ItemCatalog> made = loadout_catalog();
	if (made == nullptr) {
		return;
	}

	open::Character bare = wielder(*made);
	const std::optional<open::WeaponAttackProfile> fresh = open::weapon_attack_profile(bare);
	expect_true("an unarmed character still has a swing", fresh.has_value());
	if (fresh) {
		expect_true("bare hands are Unarmed Combat",
					fresh->job.basic == open::BasicSkill::UnarmedCombat);
		expect_true("with no specialisation chosen", !fresh->job.option.has_value());
		expect_true("at the fresh basic rank", fresh->trained == 1u);
	}

	// Training the basic keeps moving the same number.
	open::Character trained_sheet = bare;
	while (trained_sheet.resolve_skill(open::BasicSkill::UnarmedCombat, std::nullopt).rank < 100u) {
		trained_sheet.record_use(open::BasicSkill::UnarmedCombat, std::nullopt, true);
	}
	const std::optional<open::WeaponAttackProfile> trained = open::weapon_attack_profile(trained_sheet);
	expect_true("the basic rank keeps rising while unadvanced",
				trained.has_value() && trained->trained == 100u);

	// A chosen Boxing track is used from rank 1, and then its own rank.
	expect_true("boxing is chosen", trained_sheet.advance(open::AdvancedSkill::Boxing));
	const std::optional<open::WeaponAttackProfile> chosen = open::weapon_attack_profile(trained_sheet);
	expect_true("the profile still names the basic",
				chosen.has_value() && chosen->job.basic == open::BasicSkill::UnarmedCombat);
	expect_true("and carries the chosen track",
				chosen.has_value() && chosen->job.option.has_value() &&
					*chosen->job.option == open::AdvancedSkill::Boxing);
	expect_true("a freshly chosen specialisation is rank 1", chosen.has_value() && chosen->trained == 1u);
	while (trained_sheet.resolve_skill(open::BasicSkill::UnarmedCombat, open::AdvancedSkill::Boxing).rank < 5u) {
		trained_sheet.record_use(open::BasicSkill::UnarmedCombat, open::AdvancedSkill::Boxing, true);
	}
	const std::optional<open::WeaponAttackProfile> boxing = open::weapon_attack_profile(trained_sheet);
	expect_true("bare hands now use the boxing rank", boxing.has_value() && boxing->trained == 5u);
	expect_true("the rank used is the one the sheet resolves",
				boxing.has_value() &&
					boxing->trained ==
						trained_sheet
							.resolve_skill(open::BasicSkill::UnarmedCombat, open::AdvancedSkill::Boxing)
							.rank);

	// A specialisation under another skill cannot leak into bare hands.
	open::Character dodger = wielder(*made);
	while (dodger.resolve_skill(open::BasicSkill::Dodging, std::nullopt).rank < 100u) {
		dodger.record_use(open::BasicSkill::Dodging, std::nullopt, true);
	}
	expect_true("footwork is chosen", dodger.advance(open::AdvancedSkill::Footwork));
	const std::optional<open::WeaponAttackProfile> unrelated = open::weapon_attack_profile(dodger);
	expect_true("an unrelated chosen track is not carried into the attack",
				unrelated.has_value() && !unrelated->job.option.has_value());
	expect_true("and the attack still uses the Unarmed Combat basic",
				unrelated.has_value() && unrelated->trained == 1u);
}

}  // namespace

int main() {
	quality_is_a_seven_tier_table();
	a_shortfall_is_squared_and_the_worst_one_wins();
	weapon_effectiveness_takes_the_minimum_of_every_requirement();
	a_weaker_swing_costs_more_stamina();
	the_cooldown_stays_in_band_and_never_divides_by_zero();
	an_attack_pays_when_it_can_and_nothing_when_it_cannot();
	the_right_hand_is_the_attack_and_a_shield_never_is();
	the_profile_reads_the_definition_and_the_instance();
	defense_priority_is_shield_then_weapon_or_dodge();
	the_unarmed_fallback_follows_the_unarmed_track();
	return test_summary();
}
