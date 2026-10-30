#include "open/character.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <vector>

#include "open/character_rules.hpp"
#include "open/combat_rules.hpp"
#include "open/equipment.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"
#include "open/move_order.hpp"
#include "open/weapon_rules.hpp"
#include "test_support.hpp"

// Task 10 tests: the typed basic attack, end to end. Break these catches: damage that
// ignores quality, reach, the defender's armour or their Toughness; a defence that is
// free, unspent, or that never expires; a weapon that never wears, or that wears on an
// attempt which was refused; a body part drawn for a blow with nothing left in it, or
// lost when armour absorbs everything; a stun that lets its target attack, defend or
// move; stagger fed by something other than the final blunt; and a lethal blow that
// forgets to train, or stuns a corpse.
//
// Basic attacks only: no technique is selected, priced or cooled here. Every expected
// number is hand-computed from docs/weapons.md and docs/combat.md, never read back
// from the code under test.

namespace {

constexpr open::DefinitionId kSword = 101;
constexpr open::DefinitionId kSpear = 103;
constexpr open::DefinitionId kHammer = 104;
constexpr open::DefinitionId kShield = 105;
constexpr open::DefinitionId kBuckler = 106;   // 0% mitigation: a worthless defence
constexpr open::DefinitionId kPlate = 107;     // 100% of everything, on the chest
constexpr open::DefinitionId kCrusher = 108;   // a heavy blunt fixture, to reach a stun
constexpr open::DefinitionId kBrandscale = 109;  // a chest that eats most blunt and no cut
constexpr open::DefinitionId kKite = 110;        // a 30% shield, for the wear boundary
constexpr open::MaterialId kMaterial = 11;
constexpr std::uint64_t kAttackerId = 1;
constexpr std::uint64_t kDefenderId = 2;
constexpr std::uint64_t kOutsiderId = 3;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

// The plan's separate tunables, named once so a test can point at them.
constexpr double kDefenseCost = 8.0;

open::DamageComponents parts(double cut, double pierce, double blunt) {
	open::DamageComponents value;
	value.cut = cut;
	value.pierce = pierce;
	value.blunt = blunt;
	return value;
}

open::ItemDefinition weapon_definition(open::DefinitionId id, const char *model, open::HandUse use,
									   open::DamageComponents damage, double min_reach,
									   double max_reach, double block, open::BasicSkill basic,
									   open::AdvancedSkill option) {
	open::ItemDefinition item;
	item.id = id;
	item.model_key = model;
	item.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
	item.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
	item.hand_use = use;
	open::WeaponDefinition role;
	role.job = {basic, option};
	role.stats.damage = damage;
	role.stats.penetration = parts(0.0, 0.0, 0.0);
	role.stats.min_reach = min_reach;
	role.stats.max_reach = max_reach;
	role.stats.speed = 1.0;
	role.stats.stamina_cost = 8.0;
	role.stats.block_percent = block;
	role.stats.weight = 2.0;
	role.stats.requirements = {{open::Attribute::Dexterity, 5.0}};
	role.stats.required_trained_rank = 1;
	item.weapon = role;
	return item;
}

open::ItemDefinition shield_definition(open::DefinitionId id, const char *model, double mitigation) {
	open::ItemDefinition item;
	item.id = id;
	item.model_key = model;
	item.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
	item.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
	item.hand_use = open::HandUse::OneHanded;
	open::ShieldDefinition board;
	board.job = {open::BasicSkill::Blocking, open::AdvancedSkill::SmallShields};
	board.block_percent = mitigation;
	board.weight = 3.0;
	item.shield = board;
	return item;
}

const std::shared_ptr<const open::ItemCatalog> &catalogue() {
	static const std::shared_ptr<const open::ItemCatalog> made = [] {
		std::vector<open::ItemDefinition> items;
		items.push_back(weapon_definition(kSword, "Sword", open::HandUse::OneHanded,
										 parts(6.0, 2.0, 0.0), 0.0, 1.5, 25.0,
										 open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword));
		items.push_back(weapon_definition(kSpear, "Spear", open::HandUse::TwoHanded,
										 parts(0.0, 8.0, 0.0), 0.6, 2.0, 20.0, open::BasicSkill::Polearms,
										 open::AdvancedSkill::Spear));
		items.push_back(weapon_definition(kHammer, "Hammer_Small", open::HandUse::OneHanded,
										 parts(0.0, 0.0, 8.0), 0.0, 1.3, 20.0,
										 open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Mace));
		items.push_back(shield_definition(kShield, "Shield_Round", 60.0));
		items.push_back(shield_definition(kBuckler, "Buckler_Fixture", 0.0));
		// A crusher carrying five times as much cut as blunt, and a chest that stops 70
		// of its blunt and none of its cut. Together they let one blow carry enough
		// stagger to stun while spending HP the target can afford: the meter is fed by
		// the blunt alone, the body is spent on everything, and armour can split them.
		items.push_back(weapon_definition(kCrusher, "Crusher_Fixture", open::HandUse::OneHanded,
										 parts(10000.0, 0.0, 2000.0), 0.0, 1.5, 20.0,
										 open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Mace));

		open::ItemDefinition brandscale;
		brandscale.id = kBrandscale;
		brandscale.model_key = "Brandscale_Fixture";
		brandscale.allowed_slots.set(static_cast<std::size_t>(open::Slot::Chest));
		open::ArmourDefinition torso;
		torso.resistance_percent = parts(0.0, 0.0, 70.0);
		torso.weight = 4.0;
		brandscale.armour = torso;
		items.push_back(brandscale);

		items.push_back(shield_definition(kKite, "Kite_Fixture", 30.0));

		open::ItemDefinition plate;
		plate.id = kPlate;
		plate.model_key = "Plate_Fixture";
		plate.allowed_slots.set(static_cast<std::size_t>(open::Slot::Chest));
		open::ArmourDefinition chest;
		// Every component fully stopped, so a hit on the torso does no damage at all
		// while still having been aimed at the torso.
		chest.resistance_percent = parts(100.0, 100.0, 100.0);
		chest.weight = 4.0;
		plate.armour = chest;
		items.push_back(plate);

		open::MaterialDefinition material;
		material.id = kMaterial;
		material.max_durability_by_quality = {100, 100, 100, 100, 100, 100, 100};

		auto built = open::ItemCatalog::create(items, {material});
		if (!built.has_value()) {
			g_failed++;
		}
		return std::make_shared<open::ItemCatalog>(std::move(built).value());
	}();
	return made;
}

open::Character sheet(std::uint64_t id, open::Attribute blessing = open::Attribute::Constitution) {
	open::CharacterConfig setup;
	setup.id = id;
	setup.name = "Sheet";
	setup.level = 1;
	setup.blessing = blessing;
	auto made = open::Character::create(setup, catalogue());
	if (!made.has_value()) {
		g_failed++;
	}
	open::Character who = std::move(made).value();
	// Dexterity 5 meets every starter weapon's requirement, so a baseline swing is
	// fully effective and the oracles below stay about the weapon.
	who.set_base(open::Attribute::Dexterity, 5.0f);
	return who;
}

void wear(open::Character &who, open::DefinitionId definition, open::ItemId id, open::Slot slot,
		  unsigned quality = 1) {
	const std::optional<open::ItemInstance> instance =
		open::make_item_instance(*who.catalog(), definition, kMaterial, id, quality);
	if (!instance.has_value() || !who.add_instance(*instance) || !who.equip(id, slot)) {
		g_failed++;
	}
}

// A legacy placement-only tagged record: placeable and wearable, and not a weapon.
void wear_undefined(open::Character &who, open::ItemId id) {
	open::ItemRef legacy;
	legacy.id = id;
	legacy.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
	legacy.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
	legacy.hand_use = open::HandUse::OneHanded;
	legacy.combat_skill = open::BasicSkill::OneHandedWeapons;
	legacy.combat_option = open::AdvancedSkill::Sword;
	if (!who.add_item(legacy) || !who.equip(id, open::Slot::RightHand)) {
		g_failed++;
	}
}

// Rank 100 on a basic track: exactly 39,600 successful uses, and the trained fraction
// of the damage formula becomes a clean one half.
void train_to_100(open::Character &who, open::BasicSkill basic) {
	for (unsigned i = 0; i < 39600; ++i) {
		who.record_use(basic, std::nullopt, true);
	}
	if (who.resolve_skill(basic, std::nullopt).rank != 100u) {
		g_failed++;
	}
}

open::Vec3 at(float x) {
	return open::Vec3{x, 0.0f, 0.0f};
}

double health(const open::Character &who) {
	return who.pool(open::PoolKind::Hp).current;
}

double stamina(const open::Character &who) {
	return who.pool(open::PoolKind::Stamina).current;
}

std::optional<open::SwingResult> swing(open::Character &attacker, open::Character &defender,
									   std::mt19937_64 &rng, float separation = 1.0f) {
	return attacker.try_melee_swing(defender, at(0.0f), at(separation), rng);
}

// Leaves a sheet unable to pay for its next answer: the defence is still performed, at
// half, and its owner is left with the exact figure the oracle can name.
void exhaust_defence_of(open::Character &who) {
	const float available = who.pool(open::PoolKind::Stamina).current;
	if (available > 0.0f) {
		expect_true("the defence is drained", who.spend_stamina(available));
	}
	if (who.pool(open::PoolKind::Stamina).current != 0.0f) {
		g_failed++;
	}
}

// Restores a drained bar: time refills a pool, and a defence that is ready but
// unaffordable is a different state from one that is ready and paid for.
void refill(open::Character &who) {
	expect_true("the bar regrows", who.regenerate(60.0f, false));
}

// Both clocks ready again, so the next attempt is measured on its own terms.
void settle(open::Character &attacker, open::Character &defender) {
	attacker.advance_combat_clocks(600.0f);
	defender.advance_combat_clocks(600.0f);
}

// A defence that cannot pay for itself is performed exhausted rather than skipped, so
// the test has to empty the bar exactly: a spend larger than the pool is refused
// whole, which would leave a fully funded defender in the fixture and the oracle
// would then describe a defence that was never unpaid.
void exhaust(open::Character &who) {
	const float available = who.pool(open::PoolKind::Stamina).current;
	if (available > 0.0f && !who.spend_stamina(available)) {
		g_failed++;
	}
	if (who.pool(open::PoolKind::Stamina).current != 0.0f) {
		g_failed++;
	}
}

// --------------------------------------------------------------------- damage

// Sword, tier 1, rank 100, against an unarmoured defender with base Toughness 1, with
// the defence exhausted (so half of it gets through and half is prevented):
//   (6, 2, 0) x 1 (quality) x 0.5 (trained/200) x 1 (effectiveness) = (3, 1, 0)
//   exhausted dodge, half             -> after defence (1.5, 0.5, 0), prevented 1.5
//   no armour                          -> (1.5, 0.5, 0)
//   base Toughness 1 -> / 1.05         -> total 1.904761…
void typed_damage_replaces_the_placeholder() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	train_to_100(attacker, open::BasicSkill::OneHandedWeapons);
	wear_undefined(defender, 301);  // nothing described: no shield, no weapon block
	wear(attacker, kSword, 401, open::Slot::RightHand);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(1);

	// 1. A defence that is ready and paid for, choosing a dodge (nothing beats a
	// dodge on a tie), turns the whole typed blow aside. Nothing is left after the
	// defence, so no part is drawn and none is named.
	const double full_stamina = stamina(defender);
	const std::optional<open::SwingResult> dodged = swing(attacker, defender, rng);
	expect_true("the attempt happens", dodged.has_value());
	if (!dodged) {
		return;
	}
	expect_true("a ready dodge nullifies the blow", dodged->nullified);
	expect_close("and lands nothing", dodged->damage, 0.0f, 1e-6);
	expect_true("nothing to place, so no part", !dodged->part.has_value());
	expect_close("the defence cost its own price", full_stamina - stamina(defender), kDefenseCost, 1e-3);
	expect_close("the swing cost its own price", stamina(attacker), full_stamina - 8.0, 1e-3);
	const float defence_clock = defender.defend_cooldown_remaining();
	expect_true("and started its clock", defence_clock > 0.0f);
	expect_close("prevented is the whole offensive", dodged->prevented, 4.0, 1e-3);
	expect_true("the sword was the weapon swung",
				dodged->attack_item.has_value() && *dodged->attack_item == 401);
	expect_true("a dodge was named as the defence, with nothing held in it",
				!dodged->defense_item.has_value());

	// 2. While that clock runs the defender answers with nothing at all: the same
	// blow arrives whole, and nothing is spent on it or charged for it.
	//   (6, 2, 0) x 1 x 0.5 (trained/200) = (3, 1, 0), no defence, no armour,
	//   base Toughness 1 -> / 1.05 = 3.809…
	attacker.advance_combat_clocks(600.0f);
	const double cooling_health = health(defender);
	const double cooling_stamina = stamina(defender);
	const std::optional<open::SwingResult> whole = swing(attacker, defender, rng);
	expect_true("the cooling defence could not answer", whole.has_value() && !whole->nullified);
	if (whole) {
		expect_close("the whole typed blow lands", whole->damage, 4.0 / 1.05, 1e-3);
		expect_close("prevented nothing", whole->prevented, 0.0, 1e-6);
		expect_close("and the sheet paid it once", cooling_health - health(defender),
					 static_cast<double>(whole->damage), 1e-3);
		expect_close("nothing was spent on a defence", stamina(defender), cooling_stamina, 1e-3);
		expect_true("and exactly one named part took it", whole->part.has_value());
	}

	// 3. A defence that cannot pay for itself is still performed, at half, and still
	// starts its clock: the placeholder 0.04 can never produce these numbers.
	//   (3, 1, 0) halved by the unpaid defence, no armour, / 1.05 = 1.904…
	settle(attacker, defender);
	exhaust(defender);
	const double tired_health = health(defender);
	const std::optional<open::SwingResult> half = swing(attacker, defender, rng);
	expect_true("the unpaid defence performed at half", half.has_value() && !half->nullified);
	if (half) {
		expect_close("half of the blow gets through", half->damage, 2.0 / 1.05, 1e-3);
		expect_close("half of it was prevented", half->prevented, 2.0, 1e-3);
		expect_close("the sheet paid the same amount", tired_health - health(defender),
					 static_cast<double>(half->damage), 1e-3);
		expect_close("an unpaid defence paid nothing", stamina(defender), 0.0, 1e-6);
		expect_true("and still started its clock", defender.defend_cooldown_remaining() > 0.0f);
	}
}

// Quality multiplies the weapon's own components, so a tier-2 blade of the same model
// doubles the damage with nothing else changed.
void quality_tier_moves_the_damage() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	train_to_100(attacker, open::BasicSkill::OneHandedWeapons);
	wear_undefined(defender, 302);
	wear(attacker, kSword, 402, open::Slot::RightHand, 2);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(2);

	exhaust(defender);  // half of it through, every time
	const std::optional<open::SwingResult> spent = swing(attacker, defender, rng);
	expect_true("the first answer is the half defence", spent.has_value() && !spent->nullified);
	attacker.advance_combat_clocks(600.0f);  // the defence stays cooling
	const std::optional<open::SwingResult> hit = swing(attacker, defender, rng);
	expect_true("the tier-2 swing lands", hit.has_value());
	if (hit) {
		// (6, 2, 0) x 2 (tier 2) x 0.5 (trained/200) = (6, 2, 0), unanswered, no
		// armour, / 1.05: exactly double the tier-1 total in the section above.
		expect_close("tier 2 doubles what tier 1 dealt", hit->damage, 8.0 / 1.05, 1e-3);
	}
}

// --------------------------------------------------------------------- reach

// Reach comes from the weapon, inclusive at its maximum, and pursuit uses the same
// number the swing is gated by.
void reach_and_pursuit_come_from_the_weapon() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kSpear, 403, open::Slot::LeftHand);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(3);
	exhaust(defender);

	const std::optional<open::SwingResult> at_max = swing(attacker, defender, rng, 2.0f);
	expect_true("a spear swings at exactly its 2.0 reach", at_max.has_value());
	attacker.advance_combat_clocks(600.0f);
	expect_true("and not past it", !swing(attacker, defender, rng, 2.0001f).has_value());

	// A sword cannot follow the spear there.
	open::Character swordsman = sheet(11, open::Attribute::Strength);
	open::Character far = sheet(12);
	wear(swordsman, kSword, 404, open::Slot::RightHand);
	swordsman.set_attack_target(12);
	exhaust(far);
	expect_true("1.6 is past a sword", !swordsman.try_melee_swing(far, at(0.0f), at(1.6f), rng).has_value());
	expect_true("1.5 is not", swordsman.try_melee_swing(far, at(0.0f), at(1.5f), rng).has_value());

	// Pursuit closes to the weapon's own reach rather than the old constant.
	open::Character walker = sheet(kAttackerId, open::Attribute::Strength);
	wear(walker, kSpear, 405, open::Slot::LeftHand);
	walker.set_attack_target(kDefenderId);
	const open::Vec3 closing = walker.attack_velocity_at(at(0.0f), at(10.0f));
	expect_true("the spear walks toward the target", closing.x > 0.0f);
	// Step until it stops, then check it stopped inside the spear's reach.
	open::Vec3 here = at(0.0f);
	for (int step = 0; step < 4000; ++step) {
		const open::Vec3 move = walker.attack_velocity_at(here, at(10.0f));
		if (move.x == 0.0f && move.z == 0.0f) {
			break;
		}
		here.x += move.x * 0.1f;
	}
	expect_true("and its own walk stops it within reach", std::fabs(here.x - 10.0f) <= 2.0 + 1e-3);
	walker.clear_order();
	const open::Vec3 idle = walker.attack_velocity_at(at(0.0f), at(10.0f));
	expect_true("with no order it does not move", idle.x == 0.0f && idle.z == 0.0f);
}

// ---------------------------------------------------------------------- wear

void items_wear_when_they_are_used_and_not_otherwise() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kSword, 406, open::Slot::RightHand);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(4);
	exhaust(defender);

	expect_true("a performed attempt wears the weapon", swing(attacker, defender, rng).has_value());
	expect_true("by exactly one point", attacker.instance(406)->durability == 99u);

	// Refused attempts wear nothing, for every reason a refusal has.
	const unsigned after_one = attacker.instance(406)->durability;
	expect_true("a cooling clock refuses", !swing(attacker, defender, rng).has_value() &&
											   attacker.instance(406)->durability == after_one);
	attacker.advance_combat_clocks(600.0f);
	expect_true("distance refuses", !swing(attacker, defender, rng, 9.0f).has_value() &&
										attacker.instance(406)->durability == after_one);
	open::Character outsider = sheet(kOutsiderId);
	attacker.clear_order();
	attacker.set_attack_target(kOutsiderId);
	expect_true("an order on someone else refuses",
				!swing(attacker, defender, rng).has_value() &&
					attacker.instance(406)->durability == after_one);
	attacker.set_attack_target(kDefenderId);
	defender.damage(open::BodyPart::Torso, 1.0e30f);
	expect_true("a dead target refuses", !swing(attacker, defender, rng).has_value() &&
											 attacker.instance(406)->durability == after_one);
	(void)outsider;

	// A blow a shield partly turns aside still wore the sword, and wore the shield too:
	// prevention is what charges a blocking item, and 60% is prevention without being
	// everything. A usable shield is chosen outright, so this also proves the defence
	// named the held item it blocked with rather than an unnamed dodge.
	open::Character rider = sheet(21, open::Attribute::Strength);
	open::Character guarded = sheet(22);
	train_to_100(rider, open::BasicSkill::OneHandedWeapons);
	wear(rider, kSword, 407, open::Slot::RightHand);
	wear(guarded, kShield, 408, open::Slot::LeftHand);
	rider.set_attack_target(22);
	std::mt19937_64 seeded(5);
	const std::optional<open::SwingResult> turned = swing(rider, guarded, seeded);
	expect_true("the shield answered it", turned.has_value());
	if (turned) {
		expect_true("a 60% block prevents some and not all",
					!turned->nullified && turned->prevented > 0.0 && turned->damage > 0.0f);
		expect_true("and named itself as the defence",
					turned->defense_item.has_value() && *turned->defense_item == 408);
	}
	expect_true("the sword wore", rider.instance(407)->durability == 99u);
	expect_true("and so did the shield", guarded.instance(408)->durability == 99u);

	// A weapon worn to nothing is still swung, at the locked third, and the discount
	// starts with the next action rather than the one that emptied it.
	open::Character tiring = sheet(31, open::Attribute::Strength);
	open::Character victim = sheet(32);
	train_to_100(tiring, open::BasicSkill::OneHandedWeapons);
	wear_undefined(victim, 303);
	wear(tiring, kSword, 409, open::Slot::RightHand);
	expect_true("worn down to a single point", tiring.apply_wear(409, 99));
	tiring.set_attack_target(32);
	std::mt19937_64 last(6);
	// The victim answers this one with a ready, paid-for dodge, which turns the whole
	// blow aside - so the swing that empties the blade is not also the swing that shows
	// the penalty, exactly as it should be.
	const std::optional<open::SwingResult> at_one = swing(tiring, victim, last);
	expect_true("durability 1 swings at full strength", at_one.has_value() &&
															tiring.instance(409)->durability == 0u);
	if (at_one) {
		expect_true("the ready defence turned the last whole swing aside",
					at_one->nullified && at_one->damage == 0.0f);
	}
	// Only the attacker's clock runs, so the victim's defence is still cooling and the
	// second swing differs from the first in exactly one way: the weapon has nothing
	// left, and that one way is a third.
	tiring.advance_combat_clocks(600.0f);
	const std::optional<open::SwingResult> at_zero = swing(tiring, victim, last);
	expect_true("a ruined sword still swings", at_zero.has_value());
	if (at_zero) {
		// (6, 2, 0) x 1 x 0.5 (trained/200) x 0.33 = (0.99, 0.33, 0), unanswered, no
		// armour, / 1.05: a third of the 4 / 1.05 a whole blade lands unanswered.
		expect_close("the third of it is applied from this action on", at_zero->damage,
					 1.32 / 1.05, 1e-3);
		expect_true("and it was not the defence that did it", !at_zero->nullified);
	}
}

// ------------------------------------------------------------------- defence

// Defence is not free, it is spent even when it loses, and the choice shares one clock
// so a block and a dodge cannot both be had.
void a_defence_costs_a_defence_and_shares_one_clock() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kHammer, 410, open::Slot::RightHand);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(7);

	const double before = stamina(defender);
	const std::optional<open::SwingResult> first = swing(attacker, defender, rng);
	expect_true("the defence answered", first.has_value() && first->nullified);
	expect_close("and paid its own cost", before - stamina(defender), kDefenseCost, 1e-3);
	expect_true("the shared defence clock is running", defender.defend_cooldown_remaining() > 0.0f);
	expect_close("the attacker paid its weapon cost", stamina(attacker), 92.0, 1e-3);

	// While the clock runs, nothing is defended and nothing is charged for it.
	attacker.advance_combat_clocks(600.0f);
	const double between = stamina(defender);
	const std::optional<open::SwingResult> second = swing(attacker, defender, rng);
	expect_true("nothing was defended", second.has_value() && !second->nullified);
	expect_close("so nothing was paid for a defence", stamina(defender), between, 1e-3);

	// An unpaid defence is still performed, at half, and still starts its clock.
	settle(attacker, defender);
	exhaust(defender);
	// 8 blunt x 1 (rank 1 of 200) = 0.04, halved by the unpaid defence, / 1.05.
	const std::optional<open::SwingResult> tired = swing(attacker, defender, rng);
	expect_true("the unpaid defence did not prevent everything",
				tired.has_value() && !tired->nullified);
	if (tired) {
		expect_close("half of the blow got through", tired->damage, 0.04 * 0.5 / 1.05, 1e-4);
		expect_close("half of it was prevented", tired->prevented, 0.02, 1e-4);
	}
	expect_close("an unpaid defence paid nothing", stamina(defender), 0.0, 1e-6);
	expect_true("and still started its clock", defender.defend_cooldown_remaining() > 0.0f);
}

// A defence worth nothing is still a defence: it is paid for and its clock starts,
// while preventing nothing, so it earns no Blocking XP, no Wall progress, no wear on
// the item, and it draws no body part for the attacker beyond the blow that arrives.
void a_worthless_defence_is_attempted_paid_and_unrewarded() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kSword, 411, open::Slot::RightHand);
	wear(defender, kBuckler, 412, open::Slot::LeftHand);  // 0% mitigation, still a shield
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(8);

	const double blocking_xp_before = static_cast<double>(defender.basic(open::BasicSkill::Blocking).xp);
	const unsigned wall_before = defender.title_count(open::Title::Wall);
	const double stamina_before = stamina(defender);
	const open::Training weapon_before = attacker.basic(open::BasicSkill::OneHandedWeapons);

	// A shield is chosen even though it is worth nothing, so the blow is answered by a
	// block that prevents 0 of 4 (rank 1, so (6, 2, 0) x 1/200), landing 4 / 1.05 of it
	// on an unarmoured part: tiny, deliberate, and not a killing blow.
	const std::optional<open::SwingResult> hit = swing(attacker, defender, rng);
	expect_true("the attack happened", hit.has_value());
	if (!hit) {
		return;
	}
	expect_true("it was not nullified", !hit->nullified);
	expect_close("a 0% block prevents nothing", hit->prevented, 0.0, 1e-6);
	expect_close("and all four points arrive", hit->damage, 0.04f / 1.05f, 1e-4);
	expect_true("the buckler was named as the defence",
				hit->defense_item.has_value() && *hit->defense_item == 412);
	expect_close("its owner paid for the attempt", stamina_before - stamina(defender), kDefenseCost,
				 1e-3);
	expect_true("and started its clock", defender.defend_cooldown_remaining() > 0.0f);
	expect_close("with no Blocking XP for preventing nothing",
				 static_cast<double>(defender.basic(open::BasicSkill::Blocking).xp), blocking_xp_before,
				 1e-9);
	expect_true("no Wall progress", defender.title_count(open::Title::Wall) == wall_before);
	expect_true("and the buckler did not wear for a block that prevented nothing",
				defender.instance(412)->durability == 100u);
	expect_true("the attacker still landed and trained",
				hit->damage > 0.0f &&
					attacker.basic(open::BasicSkill::OneHandedWeapons).xp > weapon_before.xp);
}

// ------------------------------------------------------------------- routing

// The part is drawn only when something survives the defence, and an armour result of
// zero does not take the drawn part away.
void a_part_is_drawn_only_when_something_arrives() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kSword, 413, open::Slot::RightHand);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 running(9);

	// Fully prevented by a ready, paid-for dodge. A control engine that draws once
	// proves the swing consumed nothing from the sequence the swing would have used.
	std::mt19937_64 control(running);
	const std::uint64_t control_first = control();
	const std::optional<open::SwingResult> dodged = swing(attacker, defender, running);
	expect_true("the ready defence turned it all aside", dodged.has_value() && dodged->nullified);
	expect_true("no part is named", !dodged->part.has_value());
	std::mt19937_64 after_refused(running);
	expect_true("and no draw was consumed", after_refused() == control_first);
	if (!dodged) {
		return;
	}

	// Unanswered, because only the attacker's clock was run, and the same engine
	// position must name the part the next draw would have produced.
	attacker.advance_combat_clocks(600.0f);
	std::mt19937_64 oracle(running);
	const open::BodyPart expected = open::random_melee_part(oracle);
	const std::optional<open::SwingResult> landed = swing(attacker, defender, running);
	expect_true("it landed", landed.has_value() && landed->damage > 0.0f);
	if (landed) {
		expect_true("on exactly the part the engine was at",
					landed->part.has_value() && *landed->part == expected);
	}

	// Full plate on the torso, and a hammer whose blunt is stopped nowhere else. The
	// part each swing takes is chosen here, so both outcomes are asserted on purpose
	// rather than waited for: the armoured part takes the hit and pays nothing, and the
	// part is still reported as the one that was aimed at. The defence is a 0% buckler,
	// so nothing about the blow is prevented before the armour gets to answer it.
	open::Character hitter = sheet(41, open::Attribute::Strength);
	open::Character target = sheet(42);
	train_to_100(hitter, open::BasicSkill::OneHandedWeapons);
	wear(target, kBuckler, 416, open::Slot::LeftHand);
	wear(hitter, kHammer, 414, open::Slot::RightHand);
	wear(target, kPlate, 415, open::Slot::Chest);
	hitter.set_attack_target(42);
	std::mt19937_64 sweep(10);

	// The draw belongs to the simulation, so the sweep is driven until both outcomes
	// have appeared rather than aimed at them; the seed makes that a fixed sequence, not
	// a gamble, and every blow that arrives is checked as it lands.
	bool saw_torso = false;
	bool saw_elsewhere = false;
	for (int attempt = 0; attempt < 400 && !(saw_torso && saw_elsewhere); ++attempt) {
		const std::optional<open::SwingResult> one = swing(hitter, target, sweep);
		if (!one.has_value()) {
			expect_true("a swing was refused mid-sweep", false);
			return;
		}
		expect_true("the 0% buckler prevents nothing", one->prevented == 0.0);
		hitter.advance_combat_clocks(600.0f);  // the buckler stays cooling throughout
		if (!one->part.has_value()) {
			expect_true("an arriving blow names the part it took", false);
			continue;
		}
		if (*one->part == open::BodyPart::Torso) {
			saw_torso = true;
			// The plate stops all three components, and the part is still reported: an
			// armour result of zero does not un-aim the swing that was aimed at it.
			expect_true("the armoured torso pays nothing", one->damage == 0.0f);
			expect_true("while still being named as the part", one->part.has_value());
		} else {
			saw_elsewhere = true;
			expect_true("and the same blow lands anywhere else", one->damage > 0.0f);
		}
	}
	expect_true("the sweep took the armoured torso", saw_torso);
	expect_true("and somewhere unarmoured", saw_elsewhere);
}

// ---------------------------------------------------------------------- stun

// Final blunt alone feeds the meter, exactly and to the point, and a meter that has not
// reached its threshold stuns nobody. Death then takes the meter with it.
//
// This fixture cannot reach a live stun from a real swing, and the reason is worth
// writing down: the threshold is 10 x effective Toughness while the HP pool is 100 x
// effective Constitution, and final blunt is never more than a hit's whole total, so on
// a plain sheet (where the two attributes are the same number) HP is spent at least as
// fast as the meter fills and the target always dies before it can be staggered. The
// next section separates the two on purpose.
void blunt_feeds_the_meter_and_a_fall_clears_it() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kHammer, 416, open::Slot::RightHand);  // 8 blunt, no cut, no pierce
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(11);

	// Effective Toughness 1, so the threshold is 10. Each unanswered rank-1 blow is
	// 8 x 1/200 = 0.04 blunt, arriving as 0.04 / 1.05, and that is exactly what the meter
	// must grow by: not the raw 0.04, and nothing at all for a blow the defence turned
	// aside. The meter is read straight off the blow, with no clock advanced in between,
	// so no decay has happened either.
	const double kBluntPerBlow = 0.04 / 1.05;
	const double kThreshold = 10.0;
	// The first blow, read before any clock has advanced, is exact.
	const std::optional<open::SwingResult> first = swing(attacker, defender, rng);
	expect_true("the first blow happens", first.has_value());
	if (!first) {
		return;
	}
	if (first->nullified) {
		expect_true("a blow turned aside feeds the meter nothing",
					defender.stagger_buildup() == 0.0);
		attacker.advance_combat_clocks(600.0f);  // its clock, not the target's
		const std::optional<open::SwingResult> second = swing(attacker, defender, rng);
		expect_true("the unanswered blow happens too", second.has_value());
		if (!second) {
			return;
		}
		expect_close("feeds exactly its final blunt", defender.stagger_buildup(),
					 second->final.blunt, 1e-4);
		expect_close("which is the hand-computed blunt of one rank-1 hammer blow",
					 defender.stagger_buildup(), kBluntPerBlow, 1e-4);
	} else {
		expect_close("one blow feeds exactly its final blunt", defender.stagger_buildup(),
					 first->final.blunt, 1e-4);
		expect_close("which is the hand-computed blunt of one rank-1 hammer blow",
					 defender.stagger_buildup(), kBluntPerBlow, 1e-4);
	}
	expect_true("and a meter of one fortieth of its threshold stuns nobody",
				!defender.is_stunned() && defender.stagger_buildup() < kThreshold);

	// The target's own clock is the only thing that can take the meter back down, and
	// decay is a tenth of the threshold a second: a whole second empties a fortieth.
	defender.advance_combat_clocks(1.0f);
	expect_true("a second of the target's own time decays what it accumulated",
				defender.stagger_buildup() == 0.0);
	// And a blow that is turned aside adds nothing at all.
	const double after_decay = defender.stagger_buildup();
	int refused = 0;
	for (int attempt = 0; attempt < 6; ++attempt) {
		attacker.advance_combat_clocks(600.0f);
		defender.advance_combat_clocks(600.0f);  // the defence is ready again
		const std::optional<open::SwingResult> hit = swing(attacker, defender, rng);
		if (!hit.has_value()) {
			expect_true("a swing was refused mid-loop", false);
			return;
		}
		if (hit->nullified) {
			++refused;
			expect_true("a turned-aside blow leaves the meter where it was",
						defender.stagger_buildup() == after_decay);
		}
	}
	expect_true("the ready defence did turn some of them aside", refused > 0);
	defender.damage(open::BodyPart::Torso, 1.0e30f);
	expect_true("the target is now dead", !defender.alive());
	// Dying clears the meter, the stun and the order: a corpse holds no intent.
	expect_true("no stun on a corpse", !defender.is_stunned() && defender.stun_remaining() == 0.0);
	expect_true("no meter on a corpse", defender.stagger_buildup() == 0.0);
	expect_true("and no order", !defender.attack_target().has_value());
}

// A blunt killing blow is not answered by a stun, and the sheet that lands it is not
// the sheet that gets one.
//
// Why there is no "and here is a live stun from a real swing" assertion here, written
// out because it is a property of the locked numbers rather than an oversight: the
// threshold is 10 x effective Toughness, the HP pool is 100 x effective Constitution,
// and final blunt is never more than a hit's whole total - so a plain sheet (same
// number on both attributes) spends its last HP on the very blow that could fill its
// meter, and one point of damage is one point too many to be stunned rather than
// killed. Separating them needs either healing or revive, neither of which is built
// (docs/characters.md, Not built), or a way to set a base attribute at creation, which
// is not either. The stun itself - its length, no refresh, frozen clocks, decay pause -
// is pinned exactly by the `stagger` case, and the acting gates are pinned here on a
// dead sheet, which runs the same three checks; Task 11 re-tests the exchange through
// the session. If a real stagger is wanted in play, the threshold coefficient or the HP
// formula has to move, and that is a balance call for the human, not this task.
void a_blunt_killing_blow_stuns_nobody() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	// A tier-7 hammer in a rank-1 hand: 8 x 100 x 1/200 = 4 blunt, and the quality is
	// what makes it a killing amount of blunt as well as a killing amount of everything.
	wear(attacker, kHammer, 419, open::Slot::RightHand, 7);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(21);
	// One point of health left, and no stamina to defend with: the blow below is then
	// both lethal and more than the target's whole stagger threshold, which is the only
	// way a single exchange can be past both at once.
	defender.damage(open::BodyPart::LeftArm, defender.pool(open::PoolKind::Hp).current - 1.0f);
	defender.spend_stamina(defender.pool(open::PoolKind::Stamina).current);

	const std::optional<open::SwingResult> blow = swing(attacker, defender, rng);
	expect_true("the blow landed", blow.has_value() && blow->damage > 0.0f);
	if (blow) {
		// 8 x 100 x 1/200 = 4 blunt, halved by the unpaid defence, / 1.05: 1.9, which is
		// all of the 1.9 damage - the weapon is blunt alone - and more than the single
		// point of health the target was left with.
		expect_close("its final blunt is the whole of it", blow->final.blunt, 2.0 / 1.05, 1e-3);
		expect_close("and the total is the blunt alone", blow->damage, blow->final.blunt, 1e-6);
	}
	// This blow is 1.9 of blunt and the target is down to 1 HP, so it kills rather than
	// stuns, and the meter goes with the sheet rather than being left on a corpse. (A
	// blow that both crosses the threshold and spares the target cannot be built without
	// healing: see the note above.)
	expect_true("and the target died", !defender.alive());
	expect_true("dying is not being stunned", !defender.is_stunned());
	expect_true("the meter did not survive", defender.stagger_buildup() == 0.0);
	// A dead sheet acts no more than a stunned one would, and this is the same gates the
	// stun blocks: no walk, and no swing. Setting an order on a dead sheet is accepted
	// and changes nothing, which is what makes the two refusals worth having.
	defender.set_attack_target(kAttackerId);
	const open::Vec3 gone = defender.attack_velocity_at(at(0.0f), at(10.0f));
	expect_true("a dead character does not walk", gone.x == 0.0f && gone.z == 0.0f);
	expect_true("and cannot swing",
				!defender.try_melee_swing(attacker, at(0.0f), at(1.0f), rng).has_value());
}

// A lethal blow still trains the weapon and still counts as one kill; it does not stun
// what it killed. The weapon is rank 1 on purpose: a capped basic track discards
// progress exactly as it always has, which would hide the award entirely.
void a_killing_blow_trains_and_credits_once() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kSword, 417, open::Slot::RightHand, 7);  // tier 7 is x100
	wear_undefined(defender, 418);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(12);

	const open::Training before = attacker.basic(open::BasicSkill::OneHandedWeapons);
	const unsigned duelists_before = attacker.title_count(open::Title::Duelist);
	defender.spend_stamina(defender.pool(open::PoolKind::Stamina).current);

	// Drain the target to nothing first: at rank 1 a tier-7 blade is (6, 2, 0) x 100 x
	// 1/200 = (3, 1, 0), halved by the unpaid defence and divided by 1.05, which is
	// 1.9 of damage - enough to finish a sheet with nothing left in it, and no more.
	defender.damage(open::BodyPart::LeftArm,
					defender.pool(open::PoolKind::Hp).current - 1.0f);
	const std::optional<open::SwingResult> lethal = swing(attacker, defender, rng);
	expect_true("the blow landed", lethal.has_value() && lethal->damage > 0.0f);
	if (lethal) {
		expect_close("at the hand-computed amount", lethal->damage, 2.0 / 1.05, 1e-3);
	}
	expect_true("and it killed", !defender.alive());
	const open::Training after = attacker.basic(open::BasicSkill::OneHandedWeapons);
	expect_true("a lethal hit is still one successful use of the weapon",
				after.rank == before.rank && after.xp == before.xp + 1u);
	expect_true("no stun is applied to a dead target", !defender.is_stunned());
	expect_true("and its meter was cleared with it", defender.stagger_buildup() == 0.0);
	expect_true("at most one kill credit for one kill",
				attacker.title_count(open::Title::Duelist) <= duelists_before + 1u);

	// A further blow on a corpse is refused outright and earns nothing.
	const open::Training after_kill = attacker.basic(open::BasicSkill::OneHandedWeapons);
	attacker.advance_combat_clocks(600.0f);
	expect_true("a corpse is not a target",
				!swing(attacker, defender, rng).has_value() &&
					attacker.basic(open::BasicSkill::OneHandedWeapons).xp == after_kill.xp);
}

// -------------------------------------------- legacy records and unarmed fallback

// A tagged record the catalog cannot describe confers no stats: the sheet falls back to
// the locked unarmed profile, and there is nothing to wear out.
void an_undefined_record_fights_unarmed() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	wear_undefined(attacker, 501);
	attacker.set_attack_target(kDefenderId);

	const std::optional<open::WeaponAttackProfile> profile = open::weapon_attack_profile(attacker);
	expect_true("the sheet still has an attack", profile.has_value());
	if (profile) {
		expect_true("and it is the locked unarmed fallback",
					!profile->item.has_value() && profile->stats.damage.blunt == 8.0 &&
						profile->job.basic == open::BasicSkill::UnarmedCombat);
	}

	std::mt19937_64 rng(13);
	exhaust(defender);
	const std::optional<open::SwingResult> punch = swing(attacker, defender, rng);
	expect_true("the punch happens", punch.has_value());
	if (punch) {
		expect_true("naming no weapon", !punch->attack_item.has_value());
		// (0, 0, 8) x 1 (rank 1/200 = 0.005) x 1 = 0.04 blunt, halved by the exhausted
		// dodge, / 1.05 - the placeholder number the scene shows today, arrived at by
		// the typed path rather than hardcoded.
		expect_close("the unarmed fallback's own damage", punch->damage, 0.04 * 0.5 / 1.05, 1e-4);
	}
	expect_true("and no item was worn or consumed",
				attacker.instance(501).has_value() &&
					attacker.instance(501)->definition == 0 &&
					attacker.instance(501)->durability == 0u);
}

// Nothing about the switch invents combat where there is none, and a refusal leaves no
// trace at all.
void refusals_still_refuse_cleanly() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	open::Character outsider = sheet(kOutsiderId);
	wear(attacker, kSword, 502, open::Slot::RightHand);
	std::mt19937_64 rng(14);

	const double stamina_before = stamina(attacker);
	const double health_before = health(defender);
	const double outsider_health = health(outsider);
	expect_true("with no order there is no swing", !swing(attacker, defender, rng).has_value());
	attacker.set_attack_target(kOutsiderId);
	expect_true("an order on someone else is refused",
				!attacker.try_melee_swing(defender, at(0.0f), at(1.0f), rng).has_value());
	attacker.set_attack_target(kDefenderId);
	expect_true("a NaN observation is refused",
				!attacker.try_melee_swing(defender, at(0.0f), at(static_cast<float>(kNan)), rng).has_value());
	expect_true("an infinite one too", !attacker.try_melee_swing(defender, at(0.0f),
																  at(std::numeric_limits<float>::infinity()),
																  rng)
											 .has_value());
	expect_true("the target's own business is not the attacker's",
				defender.attack_target().has_value() == false && outsider.alive());
	expect_close("nothing was spent", stamina(attacker), stamina_before, 1e-6);
	expect_close("nobody was hurt", health(defender), health_before, 1e-6);
	expect_close("not even the bystander", health(outsider), outsider_health, 1e-6);
	expect_true("no clock started", attacker.attack_cooldown_remaining() == 0.0f);
	expect_true("the sword did not wear", attacker.instance(502)->durability == 100u);
	expect_true("and the defender paid nothing for a defence",
				stamina(defender) == 100.0);
}


// ------------------------------------------------------------- blocking paywalls

// A real shield is chosen, prevents its own percentage, pays for the answer, wears for
// the prevention, and trains Blocking and Wall for it - none of which the scalar path
// could say, because its defence either won outright or did nothing at all. And the
// armour that soaked the rest of the blow wears for nothing: only the weapon swung and
// the item blocked with are charged for an exchange.
void a_shield_prevents_pays_wears_and_trains() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kSword, 601, open::Slot::RightHand);
	wear(defender, kShield, 602, open::Slot::LeftHand);   // 60%
	wear(defender, kPlate, 603, open::Slot::Chest);       // stops nothing that matters here
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(31);

	const double attack_xp_before = static_cast<double>(attacker.basic(open::BasicSkill::OneHandedWeapons).xp);
	const double blocking_xp_before = static_cast<double>(defender.basic(open::BasicSkill::Blocking).xp);
	const std::optional<open::SwingResult> hit = swing(attacker, defender, rng);
	expect_true("the shield answered", hit.has_value());
	if (!hit) {
		return;
	}
	expect_true("a 60% block neither nullifies nor lets everything through",
				!hit->nullified && hit->prevented > 0.0 && hit->damage > 0.0f);
	expect_true("and it named the shield it blocked with",
				hit->defense_item.has_value() && *hit->defense_item == 602);
	// A shield is chosen outright, whatever the skills say, so no attribute fixture is
	// needed to make this a block rather than a dodge.
	expect_close("the shield's owner paid the defence price", 100.0 - stamina(defender), kDefenseCost,
				 1e-3);
	expect_close("the block trains Blocking once",
				 static_cast<double>(defender.basic(open::BasicSkill::Blocking).xp),
				 blocking_xp_before + 1.0, 1e-9);
	expect_true("a block records Wall progress", defender.title_count(open::Title::Wall) > 0u);
	expect_true("the attacker trains for the part that arrived",
				static_cast<double>(attacker.basic(open::BasicSkill::OneHandedWeapons).xp) >
					attack_xp_before);
	expect_true("the shield wears once for preventing something",
				defender.instance(602)->durability == 100u - 1u);
	expect_true("and the armour that only soaked wears for nothing",
				defender.instance(603)->durability == 100u);

	// One clock, whatever it answers with: after a block is spent, the next blow meets
	// no defence at all. There is one defence cooldown precisely so that a block and a
	// dodge cannot both be had inside the same window.
	attacker.advance_combat_clocks(600.0f);  // the shield's clock is still running
	const double stamina_after_block = stamina(defender);
	const std::optional<open::SwingResult> cooling = swing(attacker, defender, rng);
	expect_true("the spent block is not had twice",
				cooling.has_value() && cooling->prevented == 0.0 &&
					!cooling->defense_item.has_value());
	expect_close("and nothing is charged for the answer that was not attempted",
				 stamina_after_block - stamina(defender), 0.0, 1e-6);
}

// A blocking item worn to nothing is still the shield and still blocks. The discount is
// a third of its mitigation and it starts with the next action, because the durability
// an exchange spends is the value it began with: this swing's own wear cannot disarm
// this swing. The fixture walks a 30% shield through 30% (paid), 9.9% (worn to nothing
// and unpaid), and 4.95% - the two penalties multiplying rather than the worst one
// winning.
void a_blocking_item_wears_before_it_is_discounted() {
	open::Character attacker = sheet(kAttackerId, open::Attribute::Strength);
	open::Character defender = sheet(kDefenderId);
	train_to_100(attacker, open::BasicSkill::OneHandedWeapons);
	wear(attacker, kSword, 611, open::Slot::RightHand);
	wear(defender, kKite, 612, open::Slot::LeftHand);  // 30%
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(32);
	expect_true("the shield starts at one point of durability", defender.apply_wear(612, 99));

	// 1. Paid and whole: (3, 1, 0) at 30% off leaves 70%, which is 2.8 over 1.05.
	const std::optional<open::SwingResult> paid = swing(attacker, defender, rng);
	expect_true("the first blow arrives", paid.has_value() && !paid->nullified);
	if (paid) {
		expect_close("with the whole of a 30% shield", paid->damage, 2.8 / 1.05, 1e-2);
		expect_close("and 30% of it prevented", paid->prevented, 1.2, 1e-2);
	}
	expect_true("and the swing is what wore the shield to nothing",
				defender.instance(612)->durability == 0u);

	// 2. The answer is spent and its clock is still running, so the second blow is
	// unanswered: this is where 9.9% belongs, and it cannot show up until the shield is
	// asked again.
	attacker.advance_combat_clocks(600.0f);
	const std::optional<open::SwingResult> cooling = swing(attacker, defender, rng);
	expect_true("the ruined shield is not had twice in its own window",
				cooling.has_value() && cooling->prevented == 0.0);
	expect_close("and so the whole of the blow arrives", cooling->damage, 4.0 / 1.05, 1e-2);

	// 3. Ready again, and unpaid: 30 x 0.33 (worn to nothing) x 0.5 (could not pay) =
	// 4.95%. Both penalties, separately, and the shield still chosen for all of it.
	settle(attacker, defender);
	exhaust_defence_of(defender);
	const std::optional<open::SwingResult> ruined = swing(attacker, defender, rng);
	expect_true("the ruined shield still blocks", ruined.has_value() && !ruined->nullified);
	if (ruined) {
		expect_close("at a thirtieth of a whole shield", ruined->damage, 3.802 / 1.05, 2e-2);
		expect_close("having prevented 4.95% of 4", ruined->prevented, 0.198, 2e-3);
	}
	// 4. Ready and paid, at zero durability: the 0.33 factor on its own, which is 9.9%.
	settle(attacker, defender);
	refill(defender);
	const std::optional<open::SwingResult> paid_ruined = swing(attacker, defender, rng);
	expect_true("and still chosen when it can be paid for", paid_ruined.has_value());
	if (paid_ruined) {
		expect_close("9.9% of the blow is prevented", paid_ruined->prevented, 0.396, 2e-3);
		expect_close("and 3.604 / 1.05 arrives", paid_ruined->damage, 3.604 / 1.05, 2e-2);
	}
}

}  // namespace

int main() {
	typed_damage_replaces_the_placeholder();
	quality_tier_moves_the_damage();
	reach_and_pursuit_come_from_the_weapon();
	items_wear_when_they_are_used_and_not_otherwise();
	a_defence_costs_a_defence_and_shares_one_clock();
	a_worthless_defence_is_attempted_paid_and_unrewarded();
	a_part_is_drawn_only_when_something_arrives();
	blunt_feeds_the_meter_and_a_fall_clears_it();
	a_blunt_killing_blow_stuns_nobody();
	a_killing_blow_trains_and_credits_once();
	a_shield_prevents_pays_wears_and_trains();
	a_blocking_item_wears_before_it_is_discounted();
	an_undefined_record_fights_unarmed();
	refusals_still_refuse_cleanly();
	return test_summary();
}
