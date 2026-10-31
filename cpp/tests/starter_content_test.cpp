#include "open/character.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "open/character_rules.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"
#include "open/starter_content.hpp"
#include "open/techniques.hpp"
#include "open/weapon_rules.hpp"
#include "open/work.hpp"
#include "test_support.hpp"

// Task 15: the starter content, which is production data and a production command - not a
// test fixture. Break these catches: a starter row whose numbers drifted from the locked
// table in docs/weapons.md, an instance minted with the wrong tier or a worn blade, a setup
// that equips anything, a second call that duplicates what the first gave, an id handed out
// twice, a failure that leaves half a backpack behind, an armour record or a fourth
// technique appearing, and any way for a script to ask for a sixth thing.
//
// The table is asserted field by field rather than by a summary hash because each of these
// numbers is a locked design value, and a diff that says "expected 6, got 7" on a sword's cut
// damage is the point of the case.

namespace {

constexpr open::MaterialId kMaterial = 100;
constexpr std::size_t kStarterItemCount = 5;

open::Character sheet(std::uint64_t id, const std::shared_ptr<const open::ItemCatalog> &catalog) {
	open::CharacterConfig config;
	config.id = id;
	config.name = "";
	config.faction = open::Faction::Friendly;
	config.level = 1;
	config.blessing = open::Attribute::Strength;
	const std::optional<open::Character> made = open::Character::create(config, catalog);
	expect_true("the starter fixture's sheet is created", made.has_value());
	return *made;
}

// Every held slot, in the order the starter table means by "one hand".
bool held_in_both_hands(const open::ItemDefinition &item) {
	return item.allowed_slots.test(static_cast<std::size_t>(open::Slot::LeftHand)) &&
		   item.allowed_slots.test(static_cast<std::size_t>(open::Slot::RightHand)) &&
		   item.allowed_slots.count() == 2;
}

void expect_damage(const char *what, const open::DamageComponents &got, double cut, double pierce,
				   double blunt) {
	const std::string label = std::string(what);
	expect_close((label + " cut").c_str(), static_cast<float>(got.cut), static_cast<float>(cut));
	expect_close((label + " pierce").c_str(), static_cast<float>(got.pierce),
				 static_cast<float>(pierce));
	expect_close((label + " blunt").c_str(), static_cast<float>(got.blunt),
				 static_cast<float>(blunt));
}

// ------------------------------------------------------------------ the table

// The five rows of docs/weapons.md "Starter playable equipment", field by field.
void the_catalog_is_the_locked_starter_table() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	expect_true("the starter catalog is built and validated", catalog != nullptr);
	if (catalog == nullptr) {
		return;
	}

	struct row {
		open::DefinitionId id;
		const char *model;
		open::BasicSkill basic;
		open::AdvancedSkill option;
		open::HandUse hands;
		open::WeightClass classification;
		double cut;
		double pierce;
		double blunt;
		double min_reach;
		double max_reach;
		double block_percent;
		double stamina_cost;
		double weight;
		open::Attribute requirement;
		double requirement_value;
		std::vector<open::TechniqueId> techniques;
	};

	const std::vector<row> table = {
		{101, "Sword", open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword,
		 open::HandUse::OneHanded, open::WeightClass::Light, 6.0, 2.0, 0.0, 0.0, 1.5, 25.0, 8.0,
		 2.0, open::Attribute::Dexterity, 5.0, {1, 2}},
		{102, "Dagger", open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Dagger,
		 open::HandUse::OneHanded, open::WeightClass::Light, 2.0, 5.0, 0.0, 0.0, 1.0, 15.0, 6.0,
		 1.0, open::Attribute::Dexterity, 5.0, {1, 2}},
		{103, "Spear", open::BasicSkill::Polearms, open::AdvancedSkill::Spear,
		 open::HandUse::TwoHanded, open::WeightClass::Light, 0.0, 8.0, 0.0, 0.6, 2.0, 20.0, 8.0,
		 3.0, open::Attribute::Dexterity, 5.0, {2}},
		{104, "Hammer_Small", open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Mace,
		 open::HandUse::OneHanded, open::WeightClass::Heavy, 0.0, 0.0, 8.0, 0.0, 1.3, 20.0, 8.0,
		 3.0, open::Attribute::Strength, 5.0, {3}},
	};

	for (const row &want : table) {
		const open::ItemDefinition *item = catalog->item(want.id);
		expect_true("the starter definition exists", item != nullptr);
		if (item == nullptr) {
			continue;
		}
		const std::string label = "def " + std::to_string(want.id);
		expect_true((label + " model key").c_str(), item->model_key == want.model);
		expect_true((label + " is held in either hand").c_str(), held_in_both_hands(*item));
		expect_true((label + " handedness").c_str(), item->hand_use == want.hands);
		expect_true((label + " is a weapon and nothing else").c_str(),
					item->weapon.has_value() && !item->shield.has_value() &&
						!item->armour.has_value());
		if (!item->weapon.has_value()) {
			continue;
		}
		const open::WeaponDefinition &weapon = *item->weapon;
		expect_true((label + " skill family").c_str(), weapon.job.basic == want.basic);
		expect_true((label + " matching specialization").c_str(),
					weapon.job.option ==
						std::optional<open::AdvancedSkill>(want.option));
		expect_damage((label + " damage").c_str(), weapon.stats.damage, want.cut, want.pierce,
					  want.blunt);
		expect_damage((label + " penetration is the placeholder zero").c_str(),
					  weapon.stats.penetration, 0.0, 0.0, 0.0);
		expect_close((label + " minimum reach").c_str(), weapon.stats.min_reach, want.min_reach,
					 1e-9);
		expect_close((label + " maximum reach").c_str(), weapon.stats.max_reach, want.max_reach,
					 1e-9);
		expect_close((label + " speed is the locked 1").c_str(), weapon.stats.speed, 1.0, 1e-9);
		expect_close((label + " block mitigation").c_str(), weapon.stats.block_percent,
					 want.block_percent, 1e-9);
		expect_close((label + " base stamina").c_str(), weapon.stats.stamina_cost,
					 want.stamina_cost, 1e-9);
		expect_close((label + " weight").c_str(), weapon.stats.weight, want.weight, 1e-9);
		expect_true((label + " classification").c_str(),
					weapon.stats.weight_class == want.classification);
		expect_true((label + " has exactly its one attribute requirement").c_str(),
					weapon.stats.requirements.size() == 1u);
		if (weapon.stats.requirements.size() == 1u) {
			expect_true((label + " requirement attribute").c_str(),
						weapon.stats.requirements[0].attribute == want.requirement);
			expect_close((label + " requirement value").c_str(),
						 weapon.stats.requirements[0].value, want.requirement_value, 1e-9);
		}
		expect_true((label + " asks only for a trained job of 1").c_str(),
					weapon.stats.required_trained_rank == 1u);
		expect_true((label + " supports exactly its techniques").c_str(),
					weapon.supported_techniques == want.techniques);
	}

	// The shield: Blocking / Small Shields, 60%, weight 3, the same durability table, and no
	// weapon record at all - which is what stops it having damage, reach, cost or a
	// requirement it was never given.
	const open::ItemDefinition *shield = catalog->item(105);
	expect_true("the starter shield exists", shield != nullptr);
	if (shield != nullptr) {
		expect_true("the shield is the round model", shield->model_key == "Shield_Round");
		expect_true("the shield is held in either hand", held_in_both_hands(*shield));
		expect_true("the shield is one-handed", shield->hand_use == open::HandUse::OneHanded);
		expect_true("the shield is a shield and nothing else",
					shield->shield.has_value() && !shield->weapon.has_value() &&
						!shield->armour.has_value());
		if (shield->shield.has_value()) {
			expect_true("the shield belongs to Blocking",
						shield->shield->job.basic == open::BasicSkill::Blocking);
			expect_true("and to Small Shields",
						shield->shield->job.option ==
							std::optional<open::AdvancedSkill>(open::AdvancedSkill::SmallShields));
			expect_close("the shield mitigates 60 percent", shield->shield->block_percent, 60.0,
						 1e-9);
			expect_close("the shield weighs 3", shield->shield->weight, 3.0, 1e-9);
		}
	}

	// Nothing else is in the slice: no armour, no sixth weapon, no stray ids.
	for (open::DefinitionId id : {open::DefinitionId{1}, open::DefinitionId{99},
								  open::DefinitionId{100}, open::DefinitionId{106},
								  open::DefinitionId{107}, open::DefinitionId{200}}) {
		expect_true("no other definition id resolves in the starter catalog",
					catalog->item(id) == nullptr);
	}
	for (open::DefinitionId id = 101; id <= 105; ++id) {
		const open::ItemDefinition *item = catalog->item(id);
		expect_true("and none of the five claims to be armour",
					item == nullptr || !item->armour.has_value());
	}
}

// The temporary material: one table, seven tiers, a hundred points each, and it is a material
// id rather than an item id.
void the_starter_material_is_the_locked_placeholder() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	if (catalog == nullptr) {
		return;
	}
	const open::MaterialDefinition *material = catalog->material(kMaterial);
	expect_true("the starter material exists", material != nullptr);
	if (material == nullptr) {
		return;
	}
	expect_true("with exactly one row per quality tier",
				material->max_durability_by_quality.size() == open::kQualityTierCount);
	for (const unsigned tier : material->max_durability_by_quality) {
		expect_true("and every tier is the placeholder hundred", tier == 100u);
	}
	// Task 16 added one more material to the slice - rune dust, the enchanting cost - so
	// "unused" now means a number neither of them answers.
	expect_true("rune dust joined the table at its own id",
				catalog->material(open::kRuneDustMaterial) != nullptr
					&& catalog->material(open::kRuneDustMaterial) != material);
	expect_true("and an unused material id answers nothing", catalog->material(999) == nullptr);
}

// Three techniques, the locked rows, with nothing granted by a title and no cost of their own
// - the starter slice charges the weapon.
void the_starter_techniques_are_the_locked_three() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	if (catalog == nullptr) {
		return;
	}
	const std::vector<open::TechniqueDefinition> starters = open::starter_techniques();
	for (const open::TechniqueDefinition &want : starters) {
		const open::TechniqueDefinition *found = catalog->technique(want.id);
		expect_true("every starter technique is in the catalog", found != nullptr);
		if (found == nullptr) {
			continue;
		}
		expect_true("the same values the definition carries",
					found->factors.cut == want.factors.cut &&
						found->factors.pierce == want.factors.pierce &&
						found->factors.blunt == want.factors.blunt &&
						found->cooldown == want.cooldown);
		expect_true("no starter technique is granted by a title",
					found->granting_titles.empty());
		expect_true("and none charges more than the weapon does",
					!found->stamina_cost.has_value());
	}
	expect_true("and there is no fourth technique yet", catalog->technique(4) == nullptr);
}

// Building the catalog is not cheap and there is exactly one of it: every sheet shares the
// same immutable definitions, so two calls must hand back the same object.
void the_starter_catalog_is_built_once() {
	const std::shared_ptr<const open::ItemCatalog> first = open::make_starter_catalog();
	const std::shared_ptr<const open::ItemCatalog> second = open::make_starter_catalog();
	expect_true("the starter catalog survives its readers", first != nullptr && second != nullptr);
	expect_true("and a second call is the same catalog, not a second copy",
				first.get() == second.get());
}

// ------------------------------------------------------------------- the setup

// One distinct owned instance of each definition, at tier 1 and whole, and nothing worn: the
// player starts with a backpack, not a loadout.
void setup_gives_one_distinct_owned_instance_of_each() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	if (catalog == nullptr) {
		return;
	}
	open::Character player = sheet(1, catalog);
	open::ItemIdAllocator allocator;
	open::MaterialLedger supplies;
	expect_true("the starter inventory is given", open::initialize_starter_inventory(player, allocator, supplies));

	std::vector<open::ItemId> owned = player.backpack_items();
	expect_true("five items, no more", owned.size() == kStarterItemCount);
	for (std::size_t i = 0; i < owned.size(); ++i) {
		for (std::size_t j = i + 1; j < owned.size(); ++j) {
			expect_true("every instance id is distinct", owned[i] != owned[j]);
		}
	}
	for (const open::Slot slot : {open::Slot::LeftHand, open::Slot::RightHand, open::Slot::Chest,
								  open::Slot::Head, open::Slot::Backpack}) {
		expect_true("and nothing is equipped: setup mints, it does not dress",
					!player.equipped(slot).has_value());
	}

	for (open::DefinitionId id = 101; id <= 105; ++id) {
		int matches = 0;
		for (const open::ItemId instance_id : owned) {
			const std::optional<open::ItemInstance> instance = player.instance(instance_id);
			if (instance.has_value() && instance->definition == id) {
				++matches;
				const std::string label = "starter def " + std::to_string(id);
				expect_true((label + " is tier 1").c_str(), instance->quality == 1u);
				expect_true((label + " is whole").c_str(),
							instance->durability == 100u && instance->max_durability == 100u);
				expect_true((label + " has no enchantments").c_str(),
							instance->enchantments.empty());
				expect_true((label + " carries the starter material").c_str(),
							instance->ref.id == instance_id && instance->material == kMaterial);
				// One slot per quality tier for a weapon, and none for a shield: the tier is
				// the capacity, and the shield is not enchantable in this slice.
				expect_true((label + " has the enchantment capacity its kind and tier imply")
								.c_str(),
							instance->enchantment_slots == (id == 105 ? 0u : 1u));
			}
		}
		expect_true("exactly one instance of each starter definition", matches == 1);
	}
}

// The command is idempotent: asking twice cannot mint a second sword, and it cannot disturb
// what the first call (or the player) left behind.
void setup_twice_adds_nothing() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	if (catalog == nullptr) {
		return;
	}
	open::Character player = sheet(2, catalog);
	open::ItemIdAllocator allocator;
	open::MaterialLedger supplies;
	expect_true("the first call gives the backpack",
				open::initialize_starter_inventory(player, allocator, supplies));
	const std::vector<open::ItemId> before = player.backpack_items();
	const open::Training sword_before =
		player.basic(open::BasicSkill::OneHandedWeapons);

	expect_true("and a second call is accepted as having nothing to do",
				open::initialize_starter_inventory(player, allocator, supplies));
	const std::vector<open::ItemId> after = player.backpack_items();
	expect_true("with exactly the same items, in the same order", before == after);
	expect_true("and no training or clock moved on the way",
				player.basic(open::BasicSkill::OneHandedWeapons).xp == sword_before.xp &&
					player.attack_cooldown_remaining() == 0.0f);

	// A third call after the player has started using the backpack - one weapon worn - is
	// still not a way to mint anything.
	const std::optional<open::ItemId> sword = [player]() -> std::optional<open::ItemId> {
		for (const open::ItemId id : player.backpack_items()) {
			const std::optional<open::ItemInstance> instance = player.instance(id);
			if (instance.has_value() && instance->definition == 101) {
				return id;
			}
		}
		return std::nullopt;
	}();
	expect_true("the sword is among them", sword.has_value());
	if (sword.has_value()) {
		expect_true("and it can be worn", player.equip(*sword, open::Slot::RightHand));
		const std::vector<open::ItemId> carried = player.backpack_items();
		expect_true("a third call still adds nothing",
					open::initialize_starter_inventory(player, allocator, supplies));
		expect_true("with the same records carried and the sword still held",
					player.backpack_items() == carried &&
						player.equipped(open::Slot::RightHand) == *sword);
	}
}

// A setup that cannot finish must leave nothing behind: no half a backpack, no item worn, no
// training spent. These are the two ways it cannot finish - a sheet that cannot hold anything,
// and an allocator with nowhere left to go.
void setup_refuses_to_finish_when_it_cannot_start() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	if (catalog == nullptr) {
		return;
	}

	open::Character fallen = sheet(3, catalog);
	expect_true("the sheet dies", fallen.damage(open::BodyPart::Torso, 1.0e30f));
	open::ItemIdAllocator fresh;
	open::MaterialLedger supplies;
	expect_true("a dead sheet is refused",
				!open::initialize_starter_inventory(fallen, fresh, supplies));
	expect_true("and owns nothing at all", fallen.backpack_items().empty());

	// An allocator at the end of its range cannot reserve the five ids, and a mint that starts
	// with an id it cannot give away must not add any of the others either.
	open::Character last = sheet(4, catalog);
	open::ItemIdAllocator spent(std::vector<open::ItemId>{std::numeric_limits<open::ItemId>::max()});
	open::MaterialLedger supplies2;
	expect_true("an exhausted allocator refuses the setup",
				!open::initialize_starter_inventory(last, spent, supplies2));
	expect_true("with the backpack exactly as it was", last.backpack_items().empty());
	expect_true("and the purse exactly as it was", supplies2.quantity(4, open::kStarterMaterial) == 0);
}

// Ids are unique owners: setup adopts what the sheet already holds rather than minting a
// second record for an id some other work already gave away.
void setup_never_becomes_a_second_owner_of_an_id() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	if (catalog == nullptr) {
		return;
	}
	open::Character player = sheet(5, catalog);

	// A record the sheet already owns, from a source with no idea what setup will want.
	const std::optional<open::ItemInstance> prior =
		open::make_item_instance(*catalog, 101, kMaterial, 1u, 1u);
	expect_true("the prior instance can be described", prior.has_value());
	expect_true("and carried", prior.has_value() && player.add_instance(*prior));

	open::ItemIdAllocator allocator;
	open::MaterialLedger supplies;  // does not know about id 1
	expect_true("setup still succeeds", open::initialize_starter_inventory(player, allocator, supplies));
	const std::vector<open::ItemId> owned = player.backpack_items();
	expect_true("five records in total, one of them the prior sword",
				owned.size() == kStarterItemCount);
	int swords = 0;
	for (const open::ItemId id : owned) {
		const std::optional<open::ItemInstance> instance = player.instance(id);
		expect_true("every id resolves to exactly one record", instance.has_value());
		if (instance.has_value() && instance->definition == 101) {
			++swords;
			expect_true("and the sword the sheet already carried is the one it keeps", id == 1u);
		}
	}
	expect_true("a definition is minted at most once, whoever owns it already", swords == 1);
	expect_true("the prior record itself is untouched",
				player.instance(1u).has_value() && player.instance(1u)->durability == 100u);
}

// The minted items have to be the real thing: they equip under the same limb and hand rules
// as anything else, a two-hander reserves both hands, and a shield never becomes a weapon.
void what_setup_mints_behaves_like_real_equipment() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	if (catalog == nullptr) {
		return;
	}
	open::Character player = sheet(6, catalog);
	open::ItemIdAllocator allocator;
	open::MaterialLedger supplies;
	expect_true("the backpack is given", open::initialize_starter_inventory(player, allocator, supplies));

	auto of_definition = [&](open::DefinitionId definition) -> open::ItemId {
		for (const open::ItemId id : player.backpack_items()) {
			const std::optional<open::ItemInstance> instance = player.instance(id);
			if (instance.has_value() && instance->definition == definition) {
				return id;
			}
		}
		return 0;
	};

	const open::ItemId sword = of_definition(101);
	const open::ItemId spear = of_definition(103);
	const open::ItemId shield = of_definition(105);
	expect_true("the sword, spear and shield are all findable",
				sword != 0 && spear != 0 && shield != 0);

	expect_true("the sword goes in the right hand",
				player.equip(sword, open::Slot::RightHand));
	const std::optional<open::WeaponAttackProfile> armed = open::weapon_attack_profile(player);
	expect_true("and the attack resolves from it",
				armed.has_value() && armed->item == std::optional<open::ItemId>(sword));
	if (armed.has_value()) {
		expect_damage("the swing carries the sword's own components", armed->stats.damage, 6.0,
					  2.0, 0.0);
		expect_close("with the sword's reach", armed->stats.max_reach, 1.5, 1e-9);
		expect_true("and the techniques the table gives it",
					armed->techniques == std::vector<open::TechniqueId>({1, 2}));
	}
	expect_true("a two-handed spear cannot join a held sword",
				!player.equip(spear, open::Slot::RightHand));
	expect_true("nor can it be held in the free hand alone",
				!player.equip(spear, open::Slot::LeftHand));
	expect_true("the sword comes off first", player.unequip(open::Slot::RightHand));
	expect_true("and then the spear takes both hands",
				player.equip(spear, open::Slot::LeftHand));
	expect_true("with the sword locked out while it is held",
				!player.equip(sword, open::Slot::RightHand));
	expect_true("and the shield with it", !player.equip(shield, open::Slot::RightHand));
	const std::optional<open::WeaponAttackProfile> speared = open::weapon_attack_profile(player);
	expect_true("one spear, resolved once, not two weapons",
				speared.has_value() && speared->item == std::optional<open::ItemId>(spear) &&
					speared->stats.min_reach == 0.6);
	expect_true("the spear comes off too", player.unequip(open::Slot::LeftHand));

	// A shield is a defence and never an attack: held alone, the swing is still bare hands.
	expect_true("the shield goes in the left hand", player.equip(shield, open::Slot::LeftHand));
	const std::optional<open::WeaponAttackProfile> shielded = open::weapon_attack_profile(player);
	expect_true("and the attack is still the unarmed fallback",
				shielded.has_value() && !shielded->item.has_value());
	expect_true("with no techniques for a shield to offer",
				shielded.has_value() && shielded->techniques.empty());
	const std::optional<open::WeaponDefendProfile> guarding = open::weapon_defend_profile(player);
	expect_true("while the shield is what the sheet defends with",
				guarding.has_value() && guarding->item == std::optional<open::ItemId>(shield));
	expect_close("at the shield's own mitigation", guarding.has_value()
													  ? guarding->mitigation
													  : 0.0,
				 60.0, 1e-9);
}

}  // namespace

int main() {
	the_catalog_is_the_locked_starter_table();
	the_starter_material_is_the_locked_placeholder();
	the_starter_techniques_are_the_locked_three();
	the_starter_catalog_is_built_once();
	setup_gives_one_distinct_owned_instance_of_each();
	setup_twice_adds_nothing();
	setup_refuses_to_finish_when_it_cannot_start();
	setup_never_becomes_a_second_owner_of_an_id();
	what_setup_mints_behaves_like_real_equipment();
	return test_summary();
}
