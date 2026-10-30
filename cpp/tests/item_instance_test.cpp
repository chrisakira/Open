#include "open/item_instance.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "open/character.hpp"
#include "open/equipment.hpp"
#include "open/items.hpp"
#include "test_support.hpp"

// Task 3 tests. Break these catches: two views of one owned item wearing it
// twice, a copy of an instance quietly becoming a second owner, durability
// derived by multiplying quality instead of reading the material's table, an
// allocator that recycles or wraps an exhausted id, and a legacy placement-only
// record inventing combat stats it never had.

namespace {

constexpr open::DefinitionId kSword = 101;
constexpr open::DefinitionId kSpear = 103;
constexpr open::DefinitionId kShield = 105;
constexpr open::DefinitionId kRock = 301;
constexpr open::MaterialId kMaterial = 1;
constexpr std::uint64_t kPlayerId = 1;

open::ItemCatalog catalog() {
	auto built = open::ItemCatalog::create(
		[] {
			open::WeaponStats stats;
			stats.damage = {6.0, 2.0, 0.0};
			stats.max_reach = 1.5;
			stats.speed = 1.0;
			stats.stamina_cost = 8.0;
			stats.block_percent = 25.0;
			stats.weight = 2.0;
			stats.weight_class = open::WeightClass::Light;
			stats.requirements = {{open::Attribute::Dexterity, 5.0}};
			stats.required_trained_rank = 1;

			open::WeaponStats spear_stats = stats;
			spear_stats.damage = {0.0, 8.0, 0.0};
			spear_stats.min_reach = 0.6;
			spear_stats.max_reach = 2.0;
			spear_stats.weight = 3.0;

			std::vector<open::ItemDefinition> items;

			open::ItemDefinition sword;
			sword.id = kSword;
			sword.model_key = "Sword";
			sword.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
			sword.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
			sword.hand_use = open::HandUse::OneHanded;
			open::WeaponDefinition sword_weapon;
			sword_weapon.job = {open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword};
			sword_weapon.stats = stats;
			sword.weapon = sword_weapon;
			items.push_back(sword);

			open::ItemDefinition spear;
			spear.id = kSpear;
			spear.model_key = "Spear";
			spear.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
			spear.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
			spear.hand_use = open::HandUse::TwoHanded;
			open::WeaponDefinition spear_weapon;
			spear_weapon.job = {open::BasicSkill::Polearms, open::AdvancedSkill::Spear};
			spear_weapon.stats = spear_stats;
			spear.weapon = spear_weapon;
			items.push_back(spear);

			open::ItemDefinition shield;
			shield.id = kShield;
			shield.model_key = "Shield_Round";
			shield.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
			shield.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
			shield.hand_use = open::HandUse::OneHanded;
			open::ShieldDefinition shield_role;
			shield_role.job = {open::BasicSkill::Blocking, open::AdvancedSkill::SmallShields};
			shield_role.block_percent = 60.0;
			shield_role.weight = 3.0;
			shield.shield = shield_role;
			items.push_back(shield);

			// A carried thing with no combat role at all.
			open::ItemDefinition rock;
			rock.id = kRock;
			rock.model_key = "Rock_Fixture";
			rock.allowed_slots.set(static_cast<std::size_t>(open::Slot::Backpack));
			items.push_back(rock);

			return items;
		}(),
		[] {
			open::MaterialDefinition material;
			material.id = kMaterial;
			// Distinct per tier on purpose: a tier's maximum is read, never scaled.
			material.max_durability_by_quality = {10, 20, 30, 40, 50, 60, 70};
			return std::vector<open::MaterialDefinition>{material};
		}());
	return std::move(built).value();
}

open::CharacterConfig config(std::uint64_t id, open::Faction faction = open::Faction::Friendly) {
	open::CharacterConfig made;
	made.id = id;
	made.name = "Fixture";
	made.faction = faction;
	made.level = 1;
	made.blessing = open::Attribute::Strength;
	return made;
}

open::ItemInstance instance_of(const open::ItemCatalog &made, open::DefinitionId definition,
							   open::ItemId id, unsigned quality) {
	auto made_instance =
		open::make_item_instance(made, definition, kMaterial, id, quality);
	if (!made_instance.has_value()) {
		return open::ItemInstance{};
	}
	return *made_instance;
}

// Identity is the instance id, not the definition: two swords are two things.
void same_definition_instances_are_independent() {
	const open::ItemCatalog made = catalog();
	open::Equipment equipment;
	const open::ItemInstance first = instance_of(made, kSword, 501, 1);
	const open::ItemInstance second = instance_of(made, kSword, 502, 1);

	expect_true("both are stored",
				equipment.add_instance(first, true) && equipment.add_instance(second, true));
	expect_true("they are the same definition",
				first.definition == second.definition && first.ref.id != second.ref.id);
	expect_true("each starts at its material's full durability for its tier",
				first.max_durability == 10 && first.durability == 10);

	expect_true("one wears", equipment.apply_wear(501, 3));
	const std::optional<open::ItemInstance> worn = equipment.instance(501);
	const std::optional<open::ItemInstance> untouched = equipment.instance(502);
	expect_true("and its own durability moved",
				worn.has_value() && worn->durability == 7 && worn->max_durability == 10);
	expect_true("its twin is untouched",
				untouched.has_value() && untouched->durability == 10);

	// A tier's maximum comes from the material's table, not from quality times
	// anything: tier 7 of this material is 70, not 10 x 100.
	const open::ItemInstance tier7 = instance_of(made, kSword, 503, 7);
	expect_true("tier 7 reads the seventh entry",
				tier7.max_durability == 70 && tier7.durability == 70 && tier7.quality == 7);
	expect_true("and it is a distinct owned thing", equipment.add_instance(tier7, true));
	expect_true("the table is per instance, not per definition",
				equipment.instance(503)->max_durability == 70 &&
					equipment.instance(501)->max_durability == 10);
}

// One owned record, two held views: the two-handed alias must not double-charge.
void two_handed_aliases_share_one_record() {
	const open::ItemCatalog made = catalog();
	open::Equipment equipment;
	expect_true("a spear is owned", equipment.add_instance(instance_of(made, kSpear, 511, 2), true));
	expect_true("and worn in both hands", equipment.equip(511, open::Slot::LeftHand, true, true));

	const std::optional<open::ItemId> left = equipment.equipped(open::Slot::LeftHand);
	const std::optional<open::ItemId> right = equipment.equipped(open::Slot::RightHand);
	expect_true("both views name the same id",
				left.has_value() && right.has_value() && *left == *right && *left == 511);

	expect_true("wearing it once succeeds", equipment.apply_wear(511, 1));
	const std::optional<open::ItemInstance> spear = equipment.instance(511);
	expect_true("and the record moved by exactly one point, not one per view",
				spear.has_value() && spear->durability == 19 && spear->max_durability == 20);

	expect_true("the view through a read-only copy agrees",
				equipment.item(511).has_value() && equipment.item(511)->id == 511);
	expect_true("equipping the alias again is refused, not duplicated",
				!equipment.equip(511, open::Slot::RightHand, true, false));
}

// A returned instance is a report, not a handle.
void a_copy_cannot_mutate_its_owner() {
	const open::ItemCatalog made = catalog();
	open::Equipment equipment;
	equipment.add_instance(instance_of(made, kSword, 521, 3), true);

	std::optional<open::ItemInstance> copy = equipment.instance(521);
	expect_true("the copy says 30 of 30",
				copy.has_value() && copy->durability == 30 && copy->quality == 3);
	copy->durability = 1;
	copy->quality = 7;
	copy->ref.id = 999;

	const std::optional<open::ItemInstance> canonical = equipment.instance(521);
	expect_true("the owned record is unchanged by the copy",
				canonical.has_value() && canonical->durability == 30 && canonical->quality == 3 &&
					canonical->ref.id == 521);

	// Same for the placement view, which is a copy of the record's ref.
	std::optional<open::ItemRef> ref_copy = equipment.item(521);
	ref_copy->id = 0;
	expect_true("a mutated ref copy cannot rename the item",
				equipment.item(521).has_value() && equipment.item(521)->id == 521);
	expect_true("and the instance is still found by its own id",
				equipment.instance(521).has_value() && equipment.instance(521)->ref.id == 521);
}

// Construction refuses to build something the catalog cannot describe.
void instances_are_link_checked() {
	const open::ItemCatalog made = catalog();

	expect_true("an unknown definition is refused",
				!open::make_item_instance(made, 9999, kMaterial, 601, 1).has_value());
	expect_true("an unknown material is refused",
				!open::make_item_instance(made, kSword, 9999, 601, 1).has_value());
	expect_true("a zero instance id is refused",
				!open::make_item_instance(made, kSword, kMaterial, 0, 1).has_value());
	expect_true("quality 0 is not a tier",
				!open::make_item_instance(made, kSword, kMaterial, 601, 0).has_value());
	expect_true("quality 8 is not a tier",
				!open::make_item_instance(made, kSword, kMaterial, 601, 8).has_value());
	expect_true("an empty catalog describes nothing",
				!open::make_item_instance(*open::ItemCatalog::create({}, {}), kSword, kMaterial, 601, 1)
					 .has_value());

	const std::optional<open::ItemInstance> sword =
		open::make_item_instance(made, kSword, kMaterial, 601, 1);
	expect_true("a valid combination is built", sword.has_value());
	if (sword) {
		expect_true("and the placement view is filled from the definition",
					sword->ref.id == 601 && sword->ref.hand_use == open::HandUse::OneHanded &&
						sword->ref.allowed_slots.test(static_cast<std::size_t>(open::Slot::LeftHand)) &&
						sword->ref.allowed_slots.test(static_cast<std::size_t>(open::Slot::RightHand)));
		expect_true("the job the item is for is tagged once, from the definition",
					sword->ref.combat_skill.has_value() &&
						*sword->ref.combat_skill == open::BasicSkill::OneHandedWeapons &&
						sword->ref.combat_option.has_value() &&
						*sword->ref.combat_option == open::AdvancedSkill::Sword);
		expect_true("a shield tags its blocking job",
					instance_of(made, kShield, 602, 1).ref.combat_skill ==
						std::optional<open::BasicSkill>(open::BasicSkill::Blocking));
		expect_true("and a plain record tags nothing",
					   !instance_of(made, kRock, 603, 1).ref.combat_skill.has_value());
	}
	expect_true("a shield instance is owned like anything else",
				[&] {
					open::Equipment equipment;
					return equipment.add_instance(instance_of(made, kShield, 604, 1), true);
				}());
}

void duplicate_and_unknown_ids_are_refused() {
	const open::ItemCatalog made = catalog();
	open::Equipment equipment;
	const open::ItemInstance sword = instance_of(made, kSword, 531, 1);

	expect_true("the first copy is owned", equipment.add_instance(sword, true));
	expect_true("a second record with the same id is refused", !equipment.add_instance(sword, true));
	expect_true("and the owned record is still the first one",
				equipment.instance(531).has_value() && equipment.instance(531)->durability == 10);
	expect_true("a zero id is refused", !equipment.add_instance(open::ItemInstance{}, true));
	expect_true("an unknown id has no instance", equipment.instance(9999).has_value() == false);

	// A legacy placement-only record keeps working and stays statless.
	open::ItemRef legacy;
	legacy.id = 532;
	legacy.allowed_slots.set(static_cast<std::size_t>(open::Slot::Chest));
	expect_true("a bare record is still placeable", equipment.add(legacy, true));
	const std::optional<open::ItemInstance> bare = equipment.instance(532);
	expect_true("and it has no definition, so it has no durability to wear",
				bare.has_value() && bare->definition == 0 && bare->material == 0 &&
					bare->quality == 0 && bare->max_durability == 0 && bare->durability == 0);
	expect_true("wearing it is refused, not silently absorbed",
				!equipment.apply_wear(532, 1) && !equipment.restore_durability(532, 5));
	expect_true("and its durability did not appear",
				equipment.instance(532)->durability == 0);

	// A hand-built record cannot smuggle a durability the material never declared.
	open::ItemInstance stretched = instance_of(made, kSword, 533, 1);
	stretched.durability = stretched.max_durability + 1;
	expect_true("a record above its own maximum is refused", !equipment.add_instance(stretched, true));
	open::ItemInstance tierless = instance_of(made, kSword, 534, 1);
	tierless.quality = 0;
	expect_true("a defined record with no tier is refused", !equipment.add_instance(tierless, true));
	open::ItemInstance unmade = instance_of(made, kSword, 535, 1);
	unmade.material = 0;
	expect_true("a defined record with no material is refused", !equipment.add_instance(unmade, true));
	expect_true("and none of them became owned",
				!equipment.instance(533).has_value() && !equipment.instance(534).has_value() &&
					!equipment.instance(535).has_value());
}

void wear_and_repair_stay_in_band() {
	const open::ItemCatalog made = catalog();
	open::Equipment equipment;
	equipment.add_instance(instance_of(made, kSword, 541, 1), true);  // max 10

	expect_true("one wear goes 1 to 0", equipment.apply_wear(541, 10));
	expect_true("the record reads zero", equipment.instance(541)->durability == 0);
	expect_true("wearing an empty item does not fail or go negative",
				equipment.apply_wear(541, 5) && equipment.instance(541)->durability == 0);
	expect_true("a huge wear amount clamps at zero instead of wrapping",
				equipment.apply_wear(541, 4000000000U) && equipment.instance(541)->durability == 0);

	expect_true("repair brings it back", equipment.restore_durability(541, 4));
	expect_true("to 4", equipment.instance(541)->durability == 4);
	expect_true("over-repair is capped at the maximum, which never grew",
				equipment.restore_durability(541, 999) &&
					equipment.instance(541)->durability == 10 &&
					equipment.instance(541)->max_durability == 10);
	expect_true("quality is untouched by wear or repair",
				equipment.instance(541)->quality == 1);
	expect_true("an unknown id cannot be worn or repaired",
				!equipment.apply_wear(9999, 1) && !equipment.restore_durability(9999, 1));
	expect_true("a zero id cannot either", !equipment.apply_wear(0, 1) &&
											  !equipment.restore_durability(0, 1));
}

void equipping_preserves_the_record() {
	const open::ItemCatalog made = catalog();
	open::Equipment equipment;
	equipment.add_instance(instance_of(made, kSword, 551, 4), true);  // max 40
	equipment.apply_wear(551, 15);

	expect_true("it equips", equipment.equip(551, open::Slot::RightHand, true, true));
	const std::optional<open::ItemInstance> worn = equipment.instance(551);
	expect_true("wearing it keeps quality and durability",
				worn.has_value() && worn->quality == 4 && worn->durability == 25 &&
					worn->max_durability == 40 && worn->definition == kSword &&
					worn->material == kMaterial);
	expect_true("and it is still one record, not a fresh copy",
				!equipment.add_instance(instance_of(made, kSword, 551, 4), true));

	expect_true("it unequips", equipment.unequip(open::Slot::RightHand));
	const std::optional<open::ItemInstance> stored = equipment.instance(551);
	expect_true("storage keeps the same values",
				stored.has_value() && stored->quality == 4 && stored->durability == 25 &&
					stored->max_durability == 40);
	expect_true("it is carried again", equipment.backpack_items() == std::vector<open::ItemId>{551});
	expect_true("wear still reaches it there", equipment.apply_wear(551, 1) &&
													equipment.instance(551)->durability == 24);
}

void the_character_forwards_and_gates() {
	const std::shared_ptr<const open::ItemCatalog> made =
		std::make_shared<open::ItemCatalog>(catalog());
	auto owned = open::Character::create(config(kPlayerId), made);
	expect_true("a catalog-aware character is created", owned.has_value());
	if (!owned) {
		return;
	}
	open::Character &player = *owned;

	expect_true("it keeps the catalog it was made with", player.catalog() == made);
	expect_true("and resolves definitions through it",
				player.catalog()->item(kSword) != nullptr);

	const open::ItemInstance sword = instance_of(*made, kSword, 561, 2);
	expect_true("an instance can be picked up", player.add_instance(sword));
	expect_true("it is carried", player.backpack_items() == std::vector<open::ItemId>{561});
	expect_true("and read back as an instance",
				player.instance(561).has_value() && player.instance(561)->quality == 2);
	open::ItemRef pebble;
	pebble.id = 562;
	pebble.allowed_slots.set(static_cast<std::size_t>(open::Slot::Backpack));
	expect_true("a plain placement record still works alongside it", player.add_item(pebble));
	expect_true("wearing through the sheet reaches the one record",
				player.equip(561, open::Slot::RightHand) &&
					player.instance(561)->durability == 20);
	expect_true("wear forwards too", player.apply_wear(561, 5) && player.instance(561)->durability == 15);
	expect_true("and so does repair",
				player.restore_durability(561, 100) && player.instance(561)->durability == 20);

	// A second character must not see the first one's things.
	auto other = open::Character::create(config(kPlayerId + 1), made);
	expect_true("another character is created", other.has_value());
	expect_true("and does not own it",
				other && !other->instance(561).has_value() && other->backpack_items().empty());

	// Death gates pickup, never the record itself.
	player.damage(open::BodyPart::Torso, 1.0e30f);
	expect_true("the sheet is dead", !player.alive());
	expect_true("a dead character cannot pick up an instance",
				!player.add_instance(instance_of(*made, kSword, 563, 1)));
	expect_true("but its worn item is still there",
				player.instance(561).has_value() && player.instance(561)->durability == 20);
	expect_true("and unequipping still works", player.unequip(open::Slot::RightHand));
	expect_true("with the record intact",
				player.instance(561).has_value() && player.instance(561)->quality == 2);

	// Without a catalog the sheet still exists, and nothing about it is armed.
	auto unarmed = open::Character::create(config(9));
	expect_true("the plain factory still creates", unarmed.has_value());
	expect_true("and carries no catalog", unarmed && unarmed->catalog() == nullptr);
}

void the_allocator_never_reuses_or_wraps() {
	open::ItemIdAllocator fresh;
	std::optional<open::ItemId> first = fresh.allocate();
	std::optional<open::ItemId> second = fresh.allocate();
	expect_true("ids are handed out", first.has_value() && second.has_value());
	expect_true("and they are unique and nonzero",
				first.has_value() && second.has_value() && *first != *second && *first != 0);

	open::ItemIdAllocator seeded(std::vector<open::ItemId>{1, 2, 7});
	std::optional<open::ItemId> next = seeded.allocate();
	expect_true("an existing id is never handed out again",
				next.has_value() && *next != 1 && *next != 2 && *next != 7);
	expect_true("claiming an existing id is refused",
				!seeded.claim(7) && !seeded.claim(1));
	expect_true("claiming zero is refused", !seeded.claim(0));
	expect_true("a refused claim changes nothing: 7 is still only the old owner's",
				!seeded.claim(7));
	expect_true("a free id can be claimed", seeded.claim(500));
	expect_true("and then only once", !seeded.claim(500));
	std::optional<open::ItemId> after_claim = seeded.allocate();
	expect_true("allocation skips a claimed id instead of duplicating it",
				after_claim.has_value() && *after_claim != 500);

	open::ItemIdAllocator edge;
	expect_true("the top of the range is claimable",
				edge.claim(std::numeric_limits<open::ItemId>::max()));
	open::ItemIdAllocator exhausted(std::vector<open::ItemId>{std::numeric_limits<open::ItemId>::max()});
	std::optional<open::ItemId> none = exhausted.allocate();
	expect_true("an allocator with nowhere to go reports nothing", !none.has_value());
	expect_true("and it does not wrap to a small id", !none.has_value());
	std::optional<open::ItemId> again = exhausted.allocate();
	expect_true("failing twice reports the same nothing, never a recycled id",
				!again.has_value());

	open::ItemIdAllocator gaps;
	std::optional<open::ItemId> reserved = gaps.allocate();
	expect_true("a reservation is made before the work", reserved.has_value());
	std::optional<open::ItemId> following = gaps.allocate();
	expect_true("a later id skips it even if the first attempt failed",
				following.has_value() && reserved.has_value() && *following != *reserved);
}

// Regression cover for the container rules the instance work must not disturb.
void twenty_slots_and_no_auto_swap_survive() {
	const open::ItemCatalog made = catalog();
	open::Equipment equipment;
	equipment.add_instance(instance_of(made, kSword, 571, 1), true);
	equipment.add_instance(instance_of(made, kShield, 572, 1), true);
	equipment.add_instance(instance_of(made, kSpear, 573, 1), true);

	expect_true("a sword takes one hand", equipment.equip(571, open::Slot::LeftHand, true, true));
	expect_true("a shield cannot swap it out automatically",
				!equipment.equip(572, open::Slot::LeftHand, true, true));
	expect_true("it goes in the free hand instead",
				equipment.equip(572, open::Slot::RightHand, true, true));
	expect_true("a two-handed spear needs both hands free",
				!equipment.equip(573, open::Slot::LeftHand, true, true));
	expect_true("so it waits while both hands are occupied",
				equipment.instance(573).has_value() &&
					equipment.backpack_items() == std::vector<open::ItemId>{573});
	expect_true("held_usable still answers for the instance",
				equipment.held_usable(571, true, false));
	expect_true("and the spear is not usable while stored", !equipment.held_usable(573, true, true));

	// Every slot still exists; the rings are ten separate ones.
	open::Equipment full;
	int placed = 0;
	for (unsigned i = 0; i < static_cast<unsigned>(open::Slot::Count); ++i) {
		open::ItemRef ring;
		ring.id = 1000 + i;
		ring.allowed_slots.set(i);
		if (full.add(ring, true) && full.equip(ring.id, static_cast<open::Slot>(static_cast<int>(i)),
											   true, true)) {
			++placed;
		}
	}
	expect_true("an item can occupy each of the twenty slots", placed == 20);
	expect_true("the held slots do not double count as a twenty-first owner",
				full.instance(1005).has_value() || full.instance(1006).has_value());
}

}  // namespace

int main() {
	same_definition_instances_are_independent();
	two_handed_aliases_share_one_record();
	a_copy_cannot_mutate_its_owner();
	instances_are_link_checked();
	duplicate_and_unknown_ids_are_refused();
	wear_and_repair_stay_in_band();
	equipping_preserves_the_record();
	the_character_forwards_and_gates();
	the_allocator_never_reuses_or_wraps();
	twenty_slots_and_no_auto_swap_survive();
	return test_summary();
}
