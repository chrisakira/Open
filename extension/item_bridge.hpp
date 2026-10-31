#pragma once

#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"
#include "open/weapon_rules.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// A read-only, Godot-independent view of what one character owns, and of what holding a
// particular item would mean for it, for the equipment and technique popups. It lives with
// the bridge because it exists only to feed presentation, and the bridge is the only
// consumer - the same arrangement as `character_skill_snapshot.hpp`.
//
// Every number is read from the sheet, the instance it owns and the catalog that sheet was
// made against. Nothing here decides a rule: effectiveness and the resolved attack cost
// come from the same `weapon_rules` functions an attack uses, so what the popup shows and
// what the swing does cannot drift apart. Nothing here mutates anything either: the rows are
// copies, and a script holding one cannot reach the item it was drawn from.

namespace open {

// One attribute a wielder must have, with -1 for "nothing asked" so a script never has to
// tell an absent requirement from a requirement on attribute 0.
struct RequirementSnapshotRow {
	int attribute = -1;
	double value = 0.0;
};

// An enchantment fixed to an item, as the bench made it: the values were captured when it
// was applied, so this is a record of what the item does, not of what the current wielder
// would give it.
struct EnchantmentSnapshotRow {
	std::uint64_t definition = 0;
	double added_blunt = 0.0;
	int requirement_attribute = -1;
	double requirement_value = 0.0;
};

// One owned instance: what it is, where it is, how good and how worn, and what it asks of
// the sheet that owns it.
struct EquipmentSnapshotRow {
	std::uint64_t item = 0;
	std::uint64_t definition = 0;  // 0 for a placement-only record
	std::string model_key;         // empty when the record has no definition to name

	// The slots this record occupies, in Slot order: two for a two-handed item, and empty
	// while it is carried. The primary-hand bookkeeping stays in C++; a popup just draws
	// what is held where.
	std::vector<int> slots;
	// Where the definition allows it to go, which is what decides whether an Equip control
	// is offered at all.
	std::vector<int> allowed_slots;
	int hand_use = 0;

	unsigned quality = 0;
	unsigned durability = 0;
	unsigned max_durability = 0;
	unsigned enchantments_used = 0;
	unsigned enchantment_capacity = 0;
	std::vector<EnchantmentSnapshotRow> enchantments;

	// What the definition is and whether the body still works the grip: read from the
	// catalog, and from C++'s own limb gate for the item's actual held state. Carried
	// answers false for usability - out of the hand there is no grip to speak of - and
	// nothing here is a claim about durability.
	bool is_weapon = false;
	bool is_shield = false;
	bool held_usable = false;

	// This sheet's whole workload for this item: its own requirements plus the
	// enchantments' aggregated ones, exactly as the effectiveness rule adds them, so the
	// popup never sums anything itself.
	std::vector<RequirementSnapshotRow> requirements;
};

// One row per owned instance: the carried ones in the container's own order, then each
// occupied slot in Slot order. A two-handed item is one row naming both hands, because it
// is one item.
inline std::vector<EquipmentSnapshotRow> equipment_snapshot(const Character &character) {
	std::vector<EquipmentSnapshotRow> rows;
	const std::shared_ptr<const ItemCatalog> catalog = character.catalog();

	auto describe = [&](const ItemInstance &instance) {
		EquipmentSnapshotRow row;
		row.item = instance.ref.id;
		row.definition = instance.definition;
		row.hand_use = static_cast<int>(instance.ref.hand_use);
		for (std::size_t slot = 0; slot < instance.ref.allowed_slots.size(); ++slot) {
			if (instance.ref.allowed_slots.test(slot)) {
				row.allowed_slots.push_back(static_cast<int>(slot));
			}
		}
		if (catalog != nullptr && instance.definition != 0) {
			const ItemDefinition *definition = catalog->item(instance.definition);
			if (definition != nullptr) {
				row.model_key = definition->model_key;
				row.is_weapon = definition->weapon.has_value();
				row.is_shield = definition->shield.has_value();
			}
		}
		// Every read is fresh: an arm falls between snapshots and the answer has to move
		// with it, so usability is never kept anywhere this row could be caching.
		row.held_usable = character.held_usable(instance.ref.id);
		row.quality = instance.quality;
		row.durability = instance.durability;
		row.max_durability = instance.max_durability;
		row.enchantments_used = static_cast<unsigned>(instance.enchantments.size());
		row.enchantment_capacity = instance.enchantment_slots;
		for (const AppliedEnchantment &enchantment : instance.enchantments) {
			EnchantmentSnapshotRow applied;
			applied.definition = enchantment.definition;
			applied.added_blunt = enchantment.added_blunt;
			if (enchantment.requirement.has_value()) {
				applied.requirement_attribute =
					static_cast<int>(enchantment.requirement->attribute);
				applied.requirement_value = enchantment.requirement->value;
			}
			row.enchantments.push_back(applied);
		}
		if (catalog != nullptr && instance.definition != 0) {
			const ItemDefinition *definition = catalog->item(instance.definition);
			if (definition != nullptr && definition->weapon.has_value()) {
				for (const Requirement &requirement : definition->weapon->stats.requirements) {
					row.requirements.push_back(
						RequirementSnapshotRow{static_cast<int>(requirement.attribute),
											  requirement.value});
				}
			}
		}
		for (const Requirement &requirement : enchantment_requirements(instance)) {
			row.requirements.push_back(
				RequirementSnapshotRow{static_cast<int>(requirement.attribute), requirement.value});
		}
		return row;
	};

	for (const ItemId id : character.backpack_items()) {
		const std::optional<ItemInstance> instance = character.instance(id);
		if (instance.has_value()) {
			rows.push_back(describe(*instance));
		}
	}
	for (std::size_t slot = 0; slot < static_cast<std::size_t>(Slot::Count); ++slot) {
		const std::optional<ItemId> worn = character.equipped(static_cast<Slot>(slot));
		if (!worn.has_value()) {
			continue;
		}
		// A two-handed item is one record that both hands report, so it is one row: the
		// first hand in Slot order opens it and the second joins the same list.
		EquipmentSnapshotRow *held = nullptr;
		for (EquipmentSnapshotRow &row : rows) {
			if (row.item == *worn) {
				held = &row;
				break;
			}
		}
		if (held != nullptr) {
			held->slots.push_back(static_cast<int>(slot));
			continue;
		}
		const std::optional<ItemInstance> instance = character.instance(*worn);
		if (instance.has_value()) {
			EquipmentSnapshotRow row = describe(*instance);
			row.slots.push_back(static_cast<int>(slot));
			rows.push_back(std::move(row));
		}
	}
	return rows;
}

// One weapon, resolved in this sheet's hands: what it is worth, what swinging it costs,
// what it reaches, and which techniques it offers. Whether anyone is holding it makes no
// difference - the popup has to be able to say what equipping it would do.
struct WeaponSnapshot {
	std::uint64_t item = 0;
	std::uint64_t definition = 0;
	std::string model_key;

	// The job this weapon resolves to for this sheet and the trained rank it reads: the
	// chosen matching specialisation when there is one, the frozen parent otherwise. The
	// names come from the locked rule tables.
	std::string job_name;
	std::string option_name;  // empty when nothing is chosen
	unsigned trained = 0;
	double effectiveness = 0.0;

	DamageComponents damage;       // the weapon's own components
	DamageComponents penetration;  // percentage points
	double min_reach = 0.0;
	double max_reach = 0.0;
	double speed = 0.0;
	double stamina_cost = 0.0;  // the weapon's own
	double attack_cost = 0.0;   // what one attempt by this sheet actually costs
	double block_percent = 0.0;
	double weight = 0.0;
	int weight_class = 0;
	unsigned required_trained_rank = 0;

	unsigned quality = 0;
	unsigned durability = 0;
	unsigned max_durability = 0;
	std::vector<std::uint64_t> techniques;  // what this weapon supports, in its own order
	std::vector<EnchantmentSnapshotRow> enchantments;
	std::vector<RequirementSnapshotRow> requirements;
};

// Nothing at all for an id this sheet does not own, a record with no definition, a thing
// that is not a weapon, a sheet with no catalog, or arithmetic that would not say a sensible
// number: a popup is shown either the whole truth about a weapon or nothing.
inline std::optional<WeaponSnapshot> weapon_snapshot(const Character &character, ItemId item) {
	if (item == 0) {
		return std::nullopt;
	}
	const std::shared_ptr<const ItemCatalog> catalog = character.catalog();
	if (catalog == nullptr) {
		return std::nullopt;
	}
	const std::optional<ItemInstance> instance = character.instance(item);
	if (!instance.has_value() || instance->definition == 0) {
		return std::nullopt;
	}
	const ItemDefinition *definition = catalog->item(instance->definition);
	if (definition == nullptr || !definition->weapon.has_value()) {
		return std::nullopt;
	}
	const WeaponDefinition &weapon = *definition->weapon;
	const std::optional<double> effectiveness =
		weapon_effectiveness(character, weapon.stats, weapon.job, instance->enchantments);
	if (!effectiveness.has_value()) {
		return std::nullopt;
	}

	WeaponSnapshot snapshot;
	snapshot.item = instance->ref.id;
	snapshot.definition = instance->definition;
	snapshot.model_key = definition->model_key;
	snapshot.job_name = skill_rule(weapon.job.basic).name;
	if (weapon.job.option.has_value()) {
		snapshot.option_name = advanced_rule(*weapon.job.option).name;
	}
	snapshot.trained = character.resolve_skill(weapon.job.basic, weapon.job.option).rank;
	snapshot.effectiveness = *effectiveness;
	snapshot.damage = weapon.stats.damage;
	snapshot.penetration = weapon.stats.penetration;
	snapshot.min_reach = weapon.stats.min_reach;
	snapshot.max_reach = weapon.stats.max_reach;
	snapshot.speed = weapon.stats.speed;
	snapshot.stamina_cost = weapon.stats.stamina_cost;
	snapshot.attack_cost = weapon_attack_cost(weapon.stats.stamina_cost, *effectiveness);
	snapshot.block_percent = weapon.stats.block_percent;
	snapshot.weight = weapon.stats.weight;
	snapshot.weight_class = static_cast<int>(weapon.stats.weight_class);
	snapshot.required_trained_rank = weapon.stats.required_trained_rank;
	snapshot.quality = instance->quality;
	snapshot.durability = instance->durability;
	snapshot.max_durability = instance->max_durability;
	snapshot.techniques.assign(weapon.supported_techniques.begin(),
							   weapon.supported_techniques.end());
	for (const AppliedEnchantment &enchantment : instance->enchantments) {
		EnchantmentSnapshotRow applied;
		applied.definition = enchantment.definition;
		applied.added_blunt = enchantment.added_blunt;
		if (enchantment.requirement.has_value()) {
			applied.requirement_attribute = static_cast<int>(enchantment.requirement->attribute);
			applied.requirement_value = enchantment.requirement->value;
		}
		snapshot.enchantments.push_back(applied);
	}
	for (const Requirement &requirement : weapon.stats.requirements) {
		snapshot.requirements.push_back(
			RequirementSnapshotRow{static_cast<int>(requirement.attribute), requirement.value});
	}
	for (const Requirement &requirement : enchantment_requirements(*instance)) {
		snapshot.requirements.push_back(
			RequirementSnapshotRow{static_cast<int>(requirement.attribute), requirement.value});
	}
	return snapshot;
}

}  // namespace open
