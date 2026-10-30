#include "open/items.hpp"

#include <bitset>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "open/character_rules.hpp"
#include "open/equipment.hpp"
#include "test_support.hpp"

// Task 2 tests. Break these catches: a definition that also carries owned
// instance state, a catalog that accepts a record it can never resolve later, a
// catalog that is half-built when one entry is bad, and a placement-only ItemRef
// quietly gaining combat stats it never declared.
//
// Every fixture below is test data. The production starter tables arrive in
// Task 15; nothing here is balance.

namespace {

constexpr open::DefinitionId kWeapon = 101;
constexpr open::DefinitionId kShield = 105;
constexpr open::DefinitionId kArmour = 201;
constexpr open::DefinitionId kPlain = 301;
constexpr open::MaterialId kMaterial = 1;
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

using Slots = std::bitset<static_cast<std::size_t>(open::Slot::Count)>;

// Definitions are descriptions. Quality, current durability and enchantments
// belong to the owned instance (Task 3); a material declares a maximum per tier
// and never a current value.
template <class T, class = void>
struct carries_item_state : std::false_type {};
template <class T>
struct carries_item_state<
	T, std::void_t<decltype(std::declval<const T &>().quality),
				  decltype(std::declval<const T &>().durability)>> : std::true_type {};

static_assert(!carries_item_state<open::ItemDefinition>::value,
			  "a definition must not gain owned-instance state; that is ItemInstance");
static_assert(!carries_item_state<open::WeaponStats>::value,
			  "weapon stats are an immutable description, not a worn item");
static_assert(!carries_item_state<open::ShieldDefinition>::value,
			  "a shield definition is not a shield instance");
static_assert(!carries_item_state<open::MaterialDefinition>::value,
			  "a material declares maximum durability, never current durability");

Slots hands() {
	Slots slots;
	slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
	slots.set(static_cast<std::size_t>(open::Slot::RightHand));
	return slots;
}

Slots only(open::Slot slot) {
	Slots slots;
	slots.set(static_cast<std::size_t>(slot));
	return slots;
}

open::WeaponStats good_weapon_stats() {
	open::WeaponStats stats;
	stats.damage = {6.0, 2.0, 0.0};
	stats.penetration = {0.0, 0.0, 0.0};
	stats.min_reach = 0.0;
	stats.max_reach = 1.5;
	stats.speed = 1.0;
	stats.stamina_cost = 8.0;
	stats.block_percent = 25.0;
	stats.weight = 2.0;
	stats.weight_class = open::WeightClass::Light;
	stats.requirements = {{open::Attribute::Dexterity, 5.0}};
	stats.required_trained_rank = 1;
	return stats;
}

open::ItemDefinition good_weapon() {
	open::ItemDefinition item;
	item.id = kWeapon;
	item.model_key = "Sword";
	item.allowed_slots = hands();
	item.hand_use = open::HandUse::OneHanded;
	item.weapon = open::WeaponDefinition{};
	item.weapon->job = {open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword};
	item.weapon->stats = good_weapon_stats();
	item.weapon->supported_techniques = {1, 2};
	return item;
}

open::ItemDefinition good_shield() {
	open::ItemDefinition item;
	item.id = kShield;
	item.model_key = "Shield_Round";
	item.allowed_slots = hands();
	item.hand_use = open::HandUse::OneHanded;
	item.shield = open::ShieldDefinition{};
	item.shield->job = {open::BasicSkill::Blocking, open::AdvancedSkill::SmallShields};
	item.shield->block_percent = 60.0;
	item.shield->weight = 3.0;
	return item;
}

open::ItemDefinition good_armour() {
	open::ItemDefinition item;
	item.id = kArmour;
	item.model_key = "Chest_Fixture";
	item.allowed_slots = only(open::Slot::Chest);
	item.hand_use = open::HandUse::None;
	item.armour = open::ArmourDefinition{};
	item.armour->resistance_percent = {50.0, 25.0, 10.0};
	item.armour->weight = 4.0;
	return item;
}

open::ItemDefinition good_plain() {
	open::ItemDefinition item;
	item.id = kPlain;
	item.model_key = "Rock_Fixture";
	item.allowed_slots = only(open::Slot::Backpack);
	item.hand_use = open::HandUse::None;
	return item;
}

open::MaterialDefinition good_material() {
	open::MaterialDefinition material;
	material.id = kMaterial;
	material.max_durability_by_quality = {100, 100, 100, 100, 100, 100, 100};
	return material;
}

std::vector<open::MaterialDefinition> good_materials() {
	return {good_material()};
}

// The four roles, in a fixed order so a test can name the entry it breaks.
std::vector<open::ItemDefinition> good_items() {
	return {good_weapon(), good_shield(), good_armour(), good_plain()};
}

bool accepted(std::vector<open::ItemDefinition> items,
			  std::vector<open::MaterialDefinition> materials = good_materials()) {
	return open::ItemCatalog::create(std::move(items), std::move(materials)).has_value();
}

// One field wrong, everything else already valid.
bool item_rejected(const std::function<void(open::ItemDefinition &)> &tweak) {
	auto items = good_items();
	tweak(items[0]);
	return !accepted(std::move(items));
}

bool weapon_rejected(const std::function<void(open::WeaponStats &)> &tweak) {
	return item_rejected([&](open::ItemDefinition &item) { tweak(item.weapon->stats); });
}

bool items_rejected(const std::function<void(std::vector<open::ItemDefinition> &)> &tweak) {
	auto items = good_items();
	tweak(items);
	return !accepted(std::move(items));
}

bool materials_rejected(const std::function<void(std::vector<open::MaterialDefinition> &)> &tweak) {
	auto materials = good_materials();
	tweak(materials);
	return !accepted(good_items(), std::move(materials));
}

void definition_not_instance() {
	auto catalog = open::ItemCatalog::create(good_items(), good_materials());
	expect_true("a valid catalog is built", catalog.has_value());
	if (!catalog) {
		return;
	}

	const open::ItemDefinition *weapon = catalog->item(kWeapon);
	expect_true("a definition is found by id", weapon != nullptr);
	if (weapon) {
		expect_true("and it reports its own identity", weapon->id == kWeapon);
		expect_true("with its presentation key attached", weapon->model_key == "Sword");
		expect_true("its job is a family plus a matching specialisation",
					weapon->weapon->job.basic == open::BasicSkill::OneHandedWeapons &&
						weapon->weapon->job.option.has_value() &&
						*weapon->weapon->job.option == open::AdvancedSkill::Sword);
		expect_true("its stats are the declared description",
					weapon->weapon->stats.damage.cut == 6.0 &&
						weapon->weapon->stats.damage.pierce == 2.0 &&
						weapon->weapon->stats.damage.blunt == 0.0 &&
						weapon->weapon->stats.max_reach == 1.5);
		expect_true("and it names the techniques it supports",
					weapon->weapon->supported_techniques.size() == 2);
	}

	const open::ItemDefinition *shield = catalog->item(kShield);
	expect_true("a shield resolves", shield != nullptr && shield->shield.has_value());
	expect_true("and a shield is not a weapon", shield && !shield->weapon.has_value());
	const open::ItemDefinition *armour = catalog->item(kArmour);
	expect_true("armour resolves as neither weapon nor shield",
				armour != nullptr && armour->armour.has_value() && !armour->weapon.has_value() &&
					!armour->shield.has_value());
	const open::ItemDefinition *plain = catalog->item(kPlain);
	expect_true("a plain record has no combat role at all",
				plain != nullptr && !plain->weapon.has_value() && !plain->shield.has_value() &&
					!plain->armour.has_value());

	const open::MaterialDefinition *material = catalog->material(kMaterial);
	expect_true("a material is found by id", material != nullptr);
	expect_true("its table is the seven-tier maximum, not a current value",
				material != nullptr && material->max_durability_by_quality.size() == 7 &&
					material->max_durability_by_quality[0] == 100 &&
					material->max_durability_by_quality[6] == 100);

	// Identity domains are separate.
	expect_true("an item id never resolves as a material", catalog->material(kWeapon) == nullptr);
	expect_true("a material id never resolves as an item", catalog->item(kMaterial) == nullptr);
	expect_true("zero is never a definition", catalog->item(0) == nullptr);
	expect_true("an unknown id is empty", catalog->item(9999) == nullptr);

	// A placement-only record from the legacy path stays placement-only: an
	// ItemRef naming a combat tag implies no definition, so it grants no stats.
	open::ItemRef legacy;
	legacy.id = 707;
	legacy.allowed_slots = hands();
	legacy.hand_use = open::HandUse::OneHanded;
	legacy.combat_skill = open::BasicSkill::OneHandedWeapons;
	expect_true("a tagged ItemRef confers no definition", catalog->item(legacy.id) == nullptr);

	// Same input, same description: no hidden identity and no shared mutable state.
	auto again = open::ItemCatalog::create(good_items(), good_materials());
	const open::ItemDefinition *other = again ? again->item(kWeapon) : nullptr;
	expect_true("the same input builds an equal definition",
				other != nullptr && weapon != nullptr && other->id == weapon->id &&
					other->weapon->job.basic == weapon->weapon->job.basic &&
					other->weapon->stats.block_percent == weapon->weapon->stats.block_percent &&
					other != weapon);
}

void catalog_atomic_validation() {
	// Identity.
	expect_true("a zero item id is rejected", item_rejected([](open::ItemDefinition &item) {
		item.id = 0;
	}));
	expect_true("a duplicate item id is rejected", items_rejected([](std::vector<open::ItemDefinition> &items) {
		items[1].id = items[0].id;
	}));
	expect_true("a zero material id is rejected",
				materials_rejected([](std::vector<open::MaterialDefinition> &materials) {
					materials[0].id = 0;
				}));
	expect_true("a duplicate material id is rejected",
				materials_rejected([](std::vector<open::MaterialDefinition> &materials) {
					materials.push_back(good_material());
				}));
	expect_true("the same numeric id in both domains is fine",
				accepted(good_items(), [] {
					auto materials = good_materials();
					materials[0].id = kWeapon;
					return materials;
				}()));
	expect_true("an unset job is rejected, not defaulted into the catalog",
				item_rejected([](open::ItemDefinition &item) {
					item.weapon->job = open::SkillJob{};
				}));

	// Enums and links.
	expect_true("an out-of-range hand use is rejected",
				item_rejected([](open::ItemDefinition &item) {
					item.hand_use = static_cast<open::HandUse>(static_cast<int>(open::HandUse::Count));
				}));
	expect_true("an out-of-range weapon family is rejected",
				item_rejected([](open::ItemDefinition &item) {
					item.weapon->job.basic =
						static_cast<open::BasicSkill>(static_cast<int>(open::BasicSkill::Count));
				}));
	expect_true("a specialisation from a different parent is rejected",
				item_rejected([](open::ItemDefinition &item) {
					item.weapon->job.option = open::AdvancedSkill::Greatsword;
				}));
	expect_true("an out-of-range specialisation is rejected",
				item_rejected([](open::ItemDefinition &item) {
					item.weapon->job.option =
						static_cast<open::AdvancedSkill>(static_cast<int>(open::AdvancedSkill::Count));
				}));
	expect_true("a shield specialisation must belong to Blocking",
				items_rejected([](std::vector<open::ItemDefinition> &items) {
					items[1].shield->job.option = open::AdvancedSkill::Sword;
				}));
	expect_true("a requirement names one of the ten attributes",
				weapon_rejected([](open::WeaponStats &stats) {
					stats.requirements[0].attribute =
						static_cast<open::Attribute>(static_cast<int>(open::Attribute::Count));
				}));
	expect_true("one attribute is not required twice by one definition",
				weapon_rejected([](open::WeaponStats &stats) {
					stats.requirements.push_back({open::Attribute::Dexterity, 3.0});
				}));
	expect_true("a job may carry no specialisation at all",
				!item_rejected([](open::ItemDefinition &item) { item.weapon->job.option = std::nullopt; }));

	// Offensive components.
	expect_true("NaN damage is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.damage.cut = kNan; }));
	expect_true("negative damage is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.damage.pierce = -1.0; }));
	expect_true("infinite damage is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.damage.blunt = kInf; }));
	expect_true("an all-zero weapon is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.damage = {0.0, 0.0, 0.0}; }));
	expect_true("one positive component is enough",
				!weapon_rejected([](open::WeaponStats &stats) { stats.damage = {0.0, 0.0, 0.001}; }));
	expect_true("penetration must be finite and nonnegative",
				weapon_rejected([](open::WeaponStats &stats) { stats.penetration.cut = -0.5; }));
	expect_true("penetration is a percentage point in 0..100",
				weapon_rejected([](open::WeaponStats &stats) { stats.penetration.pierce = 100.5; }));
	expect_true("100 points of penetration is legal",
				!weapon_rejected([](open::WeaponStats &stats) {
					stats.penetration = {100.0, 100.0, 100.0};
				}));

	// Percentages and the speed band.
	expect_true("block mitigation below 0 is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.block_percent = -1.0; }));
	expect_true("block mitigation above 100 is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.block_percent = 100.0001; }));
	expect_true("block mitigation 0 is legal",
				!weapon_rejected([](open::WeaponStats &stats) { stats.block_percent = 0.0; }));
	expect_true("speed below 0.1 is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.speed = 0.0999999; }));
	expect_true("speed above 2 is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.speed = 2.0000001; }));
	expect_true("the speed band is inclusive at both ends",
				!weapon_rejected([](open::WeaponStats &stats) { stats.speed = 0.1; }) &&
					!weapon_rejected([](open::WeaponStats &stats) { stats.speed = 2.0; }));
	expect_true("non-finite speed is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.speed = kNan; }));

	// Reach.
	expect_true("a zero maximum reach is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.max_reach = 0.0; }));
	expect_true("a negative maximum reach is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.max_reach = -1.0; }));
	expect_true("a minimum above the maximum is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.min_reach = 1.6; }));
	expect_true("a negative minimum is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.min_reach = -0.1; }));
	expect_true("minimum equal to maximum is legal",
				!weapon_rejected([](open::WeaponStats &stats) {
					stats.min_reach = 1.5;
					stats.max_reach = 1.5;
				}));
	expect_true("non-finite reach is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.max_reach = kInf; }));

	// Costs, weights and requirements.
	expect_true("a non-finite stamina cost is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.stamina_cost = kInf; }));
	expect_true("a negative stamina cost is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.stamina_cost = -1.0; }));
	expect_true("a negative weight is rejected",
				weapon_rejected([](open::WeaponStats &stats) { stats.weight = -1.0; }));
	expect_true("zero weight is legal",
				!weapon_rejected([](open::WeaponStats &stats) { stats.weight = 0.0; }));
	expect_true("a declared requirement must be positive",
				weapon_rejected([](open::WeaponStats &stats) { stats.requirements[0].value = 0.0; }));
	expect_true("a requirement may be absent rather than zero",
				!weapon_rejected([](open::WeaponStats &stats) { stats.requirements.clear(); }));
	expect_true("a required trained rank must be positive",
				weapon_rejected([](open::WeaponStats &stats) { stats.required_trained_rank = 0; }));
	expect_true("an out-of-range weight class is rejected",
				weapon_rejected([](open::WeaponStats &stats) {
					stats.weight_class =
						static_cast<open::WeightClass>(static_cast<int>(open::WeightClass::Count));
				}));

	// Technique support is stored now; its links are checked in Task 12.
	expect_true("a zero technique id is rejected",
				item_rejected([](open::ItemDefinition &item) {
					item.weapon->supported_techniques = {0};
				}));
	expect_true("duplicate technique support is rejected",
				item_rejected([](open::ItemDefinition &item) {
					item.weapon->supported_techniques = {1, 1};
				}));
	expect_true("an unknown technique id is not rejected here",
				!item_rejected([](open::ItemDefinition &item) {
					item.weapon->supported_techniques = {9001};
				}));

	// Roles and placement.
	expect_true("one record cannot be both a weapon and a shield",
				item_rejected([](open::ItemDefinition &item) { item.shield = *good_shield().shield; }));
	expect_true("one record cannot be both a weapon and armour",
				item_rejected([](open::ItemDefinition &item) { item.armour = *good_armour().armour; }));
	expect_true("a shield cannot also claim to be armour",
				items_rejected([](std::vector<open::ItemDefinition> &items) {
					items[1].armour = *good_armour().armour;
				}));
	expect_true("a held item must name both held slots",
				item_rejected([](open::ItemDefinition &item) {
					item.allowed_slots = only(open::Slot::LeftHand);
				}));
	expect_true("a held item must declare a hand use",
				item_rejected([](open::ItemDefinition &item) { item.hand_use = open::HandUse::None; }));
	expect_true("an unheld item must not claim the hands",
				items_rejected([](std::vector<open::ItemDefinition> &items) {
					items[3].allowed_slots = hands();
				}));
	expect_true("an item must be placeable somewhere",
				item_rejected([](open::ItemDefinition &item) { item.allowed_slots = Slots(); }));
	expect_true("armour must not go in the hands",
				items_rejected([](std::vector<open::ItemDefinition> &items) {
					items[2].allowed_slots = hands();
				}));
	expect_true("gloves in the Hands slot are wearable armour",
				accepted([] {
					auto items = good_items();
					items[2].allowed_slots = only(open::Slot::Hands);
					return items;
				}()));
	expect_true("armour resistance is a 0..100 percentage",
				items_rejected([](std::vector<open::ItemDefinition> &items) {
					items[2].armour->resistance_percent.cut = 101.0;
				}));
	expect_true("a shield's mitigation is a 0..100 percentage",
				items_rejected([](std::vector<open::ItemDefinition> &items) {
					items[1].shield->block_percent = -0.5;
				}));
	expect_true("a two-handed tag is preserved, not silently narrowed",
				accepted([] {
					auto items = good_items();
					items[0].hand_use = open::HandUse::TwoHanded;
					return items;
				}()));
	expect_true("handedness is independent of the skill family",
				accepted([] {
					auto items = good_items();
					items[0].hand_use = open::HandUse::OneHanded;
					items[0].weapon->job = {open::BasicSkill::Polearms, open::AdvancedSkill::Spear};
					items[0].weapon->supported_techniques = {2};
					return items;
				}()));

	// Material durability tables.
	expect_true("a durability table must have seven tiers",
				materials_rejected([](std::vector<open::MaterialDefinition> &materials) {
					materials[0].max_durability_by_quality.pop_back();
				}));
	expect_true("every tier must declare a positive maximum",
				materials_rejected([](std::vector<open::MaterialDefinition> &materials) {
					materials[0].max_durability_by_quality[3] = 0;
				}));
	expect_true("a flat seven-tier table is legal", accepted(good_items(), good_materials()));

	// Atomicity: no bad entry survives alongside good ones, and an empty catalog
	// is a valid catalog rather than a failure.
	expect_true("one bad entry rejects the whole catalog", items_rejected([](std::vector<open::ItemDefinition> &items) {
		auto bad = good_weapon();
		bad.id = 102;
		bad.weapon->stats.max_reach = 0.0;
		items.push_back(bad);
	}));
	expect_true("a good item list is still rejected by a bad material",
				materials_rejected([](std::vector<open::MaterialDefinition> &materials) {
					materials.push_back(open::MaterialDefinition{});
				}));
	auto empty = open::ItemCatalog::create({}, {});
	expect_true("an empty catalog is accepted", empty.has_value());
	expect_true("and resolves nothing",
				empty && empty->item(kWeapon) == nullptr && empty->material(kMaterial) == nullptr);
}

}  // namespace

int main() {
	definition_not_instance();
	catalog_atomic_validation();
	return test_summary();
}
