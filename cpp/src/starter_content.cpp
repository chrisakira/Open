#include "open/starter_content.hpp"

#include "open/character_rules.hpp"
#include "open/techniques.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace open {
namespace {

// The temporary starter material: one table, a hundred points at each of the seven quality
// tiers. docs/weapons.md calls this out as an explicit placeholder, not a quality-scaling
// rule, and the durability a starter item starts with is what makes "whole" assertable.
constexpr unsigned kStarterDurability = 100u;

ItemDefinition held(DefinitionId id, std::string model, HandUse hands) {
	ItemDefinition item;
	item.id = id;
	item.model_key = std::move(model);
	item.allowed_slots.set(static_cast<std::size_t>(Slot::LeftHand));
	item.allowed_slots.set(static_cast<std::size_t>(Slot::RightHand));
	item.hand_use = hands;
	return item;
}

// One weapon row of the locked table. Speed is 1 and the three penetrations are 0 for every
// starter weapon, so they are written here once rather than five times.
ItemDefinition weapon(DefinitionId id, std::string model, SkillJob job, HandUse hands,
					  WeightClass classification, DamageComponents damage, double min_reach,
					  double max_reach, double block_percent, double stamina_cost, double weight,
					  Requirement requirement, std::vector<TechniqueId> techniques) {
	ItemDefinition item = held(id, std::move(model), hands);
	WeaponDefinition record;
	record.job = job;
	record.stats.damage = damage;
	record.stats.penetration = DamageComponents{0.0, 0.0, 0.0};
	record.stats.min_reach = min_reach;
	record.stats.max_reach = max_reach;
	record.stats.speed = 1.0;
	record.stats.stamina_cost = stamina_cost;
	record.stats.block_percent = block_percent;
	record.stats.weight = weight;
	record.stats.weight_class = classification;
	record.stats.requirements = {requirement};
	record.stats.required_trained_rank = 1u;
	record.supported_techniques = std::move(techniques);
	item.weapon = std::move(record);
	return item;
}

std::shared_ptr<const ItemCatalog> build_starter_catalog() {
	const SkillJob sword_job{BasicSkill::OneHandedWeapons, AdvancedSkill::Sword};
	const SkillJob dagger_job{BasicSkill::OneHandedWeapons, AdvancedSkill::Dagger};
	const SkillJob spear_job{BasicSkill::Polearms, AdvancedSkill::Spear};
	const SkillJob mace_job{BasicSkill::OneHandedWeapons, AdvancedSkill::Mace};
	const Requirement dexterity{Attribute::Dexterity, 5.0};
	const Requirement strength{Attribute::Strength, 5.0};

	std::vector<ItemDefinition> items;
	items.push_back(weapon(kStarterSword, "Sword", sword_job, HandUse::OneHanded,
						   WeightClass::Light, DamageComponents{6.0, 2.0, 0.0}, 0.0, 1.5, 25.0, 8.0,
						   2.0, dexterity, {1, 2}));
	items.push_back(weapon(kStarterDagger, "Dagger", dagger_job, HandUse::OneHanded,
						   WeightClass::Light, DamageComponents{2.0, 5.0, 0.0}, 0.0, 1.0, 15.0, 6.0,
						   1.0, dexterity, {1, 2}));
	// The spear is the two-handed item of the slice, and it is still a Polearms weapon:
	// handedness and skill family are separate facts.
	items.push_back(weapon(kStarterSpear, "Spear", spear_job, HandUse::TwoHanded,
						   WeightClass::Light, DamageComponents{0.0, 8.0, 0.0}, 0.6, 2.0, 20.0, 8.0,
						   3.0, dexterity, {2}));
	items.push_back(weapon(kStarterHammer, "Hammer_Small", mace_job, HandUse::OneHanded,
						   WeightClass::Heavy, DamageComponents{0.0, 0.0, 8.0}, 0.0, 1.3, 20.0, 8.0,
						   3.0, strength, {3}));

	ItemDefinition shield = held(kStarterShield, "Shield_Round", HandUse::OneHanded);
	ShieldDefinition board;
	board.job = SkillJob{BasicSkill::Blocking, AdvancedSkill::SmallShields};
	board.block_percent = 60.0;
	board.weight = 3.0;
	shield.shield = std::move(board);
	items.push_back(std::move(shield));

	MaterialDefinition material;
	material.id = kStarterMaterial;
	material.max_durability_by_quality.assign(kQualityTierCount, kStarterDurability);

	// Rune dust is only ever spent, never worn or built from; the durability table it has
	// to carry to be a material is the same placeholder every starter tier uses.
	MaterialDefinition dust;
	dust.id = kRuneDustMaterial;
	dust.max_durability_by_quality.assign(kQualityTierCount, kStarterDurability);

	// docs/weapons.md, "Starter workbenches and materials": forge any of 101-104 for two
	// scrap, repair for one scrap, enchant for one rune dust. No shield recipe exists, and
	// the catalog will not accept one being added to it.
	std::vector<CraftRecipe> recipes;
	const std::pair<RecipeId, DefinitionId> forges[] = {
		{kStarterForgeSword, kStarterSword},   {kStarterForgeDagger, kStarterDagger},
		{kStarterForgeSpear, kStarterSpear},   {kStarterForgeHammer, kStarterHammer},
	};
	for (const auto &[recipe_id, output] : forges) {
		CraftRecipe recipe;
		recipe.id = recipe_id;
		recipe.kind = WorkKind::CraftWeapon;
		recipe.bench = BenchType::Smithing;
		recipe.output = output;
		recipe.material = kStarterMaterial;
		recipe.cost = {{kStarterMaterial, 2u}};
		recipes.push_back(recipe);
	}
	CraftRecipe repair;
	repair.id = kStarterRepair;
	repair.kind = WorkKind::Repair;
	repair.bench = BenchType::Smithing;
	repair.cost = {{kStarterMaterial, 1u}};
	recipes.push_back(repair);
	CraftRecipe enchant;
	enchant.id = kStarterEnchant;
	enchant.kind = WorkKind::EnchantWeapon;
	enchant.bench = BenchType::Enchanting;
	enchant.cost = {{kRuneDustMaterial, 1u}};
	recipes.push_back(enchant);

	const std::optional<ItemCatalog> catalog = ItemCatalog::create(
		std::move(items), {material, dust}, {}, starter_techniques(), std::move(recipes));
	if (!catalog.has_value()) {
		return nullptr;  // the locked table itself would have to be wrong
	}
	return std::make_shared<const ItemCatalog>(std::move(catalog).value());
}

bool owns_definition(const Character &character, DefinitionId definition) {
	const auto same_definition = [&character, definition](ItemId id) {
		if (id == 0) {
			return false;
		}
		const std::optional<ItemInstance> instance = character.instance(id);
		return instance.has_value() && instance->definition == definition;
	};
	for (const ItemId id : character.backpack_items()) {
		if (same_definition(id)) {
			return true;
		}
	}
	// Worn records are not in the backpack: a character wearing its only sword owns that
	// definition, and setting up again must not hand out a second one.
	for (std::size_t slot = 0; slot < static_cast<std::size_t>(Slot::Count); ++slot) {
		if (same_definition(character.equipped(static_cast<Slot>(slot)).value_or(0))) {
			return true;
		}
	}
	return false;
}

}  // namespace

std::vector<DefinitionId> starter_definitions() {
	return {kStarterSword, kStarterDagger, kStarterSpear, kStarterHammer, kStarterShield};
}

std::shared_ptr<const ItemCatalog> make_starter_catalog() {
	// Built once and shared: the definitions are immutable, and every sheet that resolves an
	// item id has to be reading the same table as the one that minted it. A function-local
	// static is the language's own one-time initialisation - thread-safe, and with nothing here
	// able to hand out a half-built catalog - so "once" is a rule rather than a habit.
	static const std::shared_ptr<const ItemCatalog> catalog = [] {
		return build_starter_catalog();
	}();
	return catalog;
}

bool initialize_starter_inventory(Character &character, ItemIdAllocator &allocator,
								  MaterialLedger &materials) {
	const std::shared_ptr<const ItemCatalog> catalog = character.catalog();
	if (catalog == nullptr) {
		return false;  // nothing to describe the items from
	}
	// Carrying is the whole precondition, so it is checked before a single id is spent: a dead
	// or broken sheet gets nothing and, more importantly, nothing is half-added to it.
	if (!character.alive() || !character.can_carry()) {
		return false;
	}

	std::vector<DefinitionId> missing;
	for (const DefinitionId definition : starter_definitions()) {
		if (!owns_definition(character, definition)) {
			missing.push_back(definition);
		}
	}
	if (missing.empty()) {
		// already the starter set: idempotent, and it mints nothing - the supply below is
		// still owed its one look, and the marker is what answers it
	} else {

	// Reserve every id up front, then describe every instance, and only then add. The two
	// loops that can fail are both ahead of the one that cannot be undone, which is what makes
	// a refusal leave the backpack exactly as it was.
	std::vector<ItemInstance> made;
	made.reserve(missing.size());
	for (const DefinitionId definition : missing) {
		std::optional<ItemId> id;
		while (!id.has_value()) {
			const std::optional<ItemId> next = allocator.allocate();
			if (!next.has_value()) {
				return false;  // no ids left: nothing has been added
			}
			// An allocator that does not know what the character already carries must not
			// become a second owner of an id. The spent id stays spent - a gap is cheap and a
			// collision is not - and the next one is asked for instead.
			if (character.instance(*next).has_value()) {
				continue;
			}
			id = next;
		}
		const std::optional<ItemInstance> instance =
			make_item_instance(*catalog, definition, kStarterMaterial, *id, 1u);
		if (!instance.has_value()) {
			return false;  // the catalog and the table disagree; nothing has been added
		}
		made.push_back(*instance);
	}
		for (const ItemInstance &instance : made) {
			if (!character.add_instance(instance)) {
				return false;
			}
		}
	}

	// The supply, once. The marker is the memory that it happened, so a spent stack stays
	// spent through every future setup: this fills a starting kit and never runs a shop.
	if (!materials.was_granted(character.id())) {
		if (!materials.grant(character.id(), kStarterMaterial, kStarterScrap)
			|| !materials.grant(character.id(), kRuneDustMaterial, kStarterRuneDust)) {
			return false;
		}
		materials.mark_granted(character.id());
	}
	return true;
}

}  // namespace open
