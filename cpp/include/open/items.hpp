#pragma once

#include "open/character_rules.hpp"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace open {

// What an item *is*: an immutable description, built once and looked up by id.
// What a character *owns* — quality, current durability, enchantments — is the
// instance in item_instance.hpp and is never a field here. These records carry
// no RNG, no timers and no knowledge of Godot; the simulation reads them.

using DefinitionId = std::uint64_t;
using MaterialId = std::uint64_t;
using TechniqueId = std::uint64_t;

// The three offensive components, and the same shape for the three armour
// resistances and the three penetrations. Units differ by field and are stated
// there: damage points, percentage points, percentage points.
struct DamageComponents {
	double cut = 0.0;
	double pierce = 0.0;
	double blunt = 0.0;
};

// A skill job is a family plus the specialisation that matches it. The default
// is deliberately out of range so an unset job is rejected by the catalog rather
// than quietly resolving to whichever skill sorts first.
struct SkillJob {
	BasicSkill basic = static_cast<BasicSkill>(static_cast<int>(BasicSkill::Count));
	std::optional<AdvancedSkill> option;
};

// One declared requirement. Every declared value is positive: an inapplicable
// requirement is absent from the list, never zero.
struct Requirement {
	Attribute attribute = static_cast<Attribute>(static_cast<int>(Attribute::Count));
	double value = 0.0;
};

// Heavy weapons require Strength, light weapons Dexterity.
enum class WeightClass {
	Light,
	Heavy,
	Count,
};

bool valid(WeightClass classification);

// Everything a weapon does, as declared numbers. Quality scaling, requirement
// effectiveness and wear live in the rules, not in these values.
struct WeaponStats {
	DamageComponents damage;         // at least one component above zero
	DamageComponents penetration;    // percentage points 0..100 off the matching resistance
	double min_reach = 0.0;          // 0..max_reach; strictly inside it halves the offense
	double max_reach = 0.0;          // positive, horizontal, Y ignored
	double speed = 1.0;              // 0.1..2, multiplies the skill-based rate
	double stamina_cost = 0.0;       // paid per attempt, scaled by effectiveness
	double block_percent = 0.0;      // 0..100 mitigation when this weapon blocks
	double weight = 0.0;             // for the future encumbrance formula, no handling penalty
	WeightClass weight_class = WeightClass::Light;
	std::vector<Requirement> requirements;
	unsigned required_trained_rank = 0;  // positive: the trained job must reach it
};

// A weapon is a job, the stats it attacks with, and the technique ids it supports.
// Handedness is placement and lives on the item, so a one-handed polearm is legal.
struct WeaponDefinition {
	SkillJob job;
	WeaponStats stats;
	std::vector<TechniqueId> supported_techniques;  // link-checked when techniques exist
};

// A shield blocks; it never attacks, so it has no offensive stats to misuse.
struct ShieldDefinition {
	SkillJob job;
	double block_percent = 0.0;  // 0..100 mitigation
	double weight = 0.0;
};

// Armour resists each component by a percentage. Playable armour is deferred;
// the mechanics are exercised with fixtures.
struct ArmourDefinition {
	DamageComponents resistance_percent;  // 0..100 per component
	double weight = 0.0;
};

struct ItemDefinition {
	DefinitionId id = 0;
	std::string model_key;  // presentation only; never gameplay
	std::bitset<static_cast<std::size_t>(Slot::Count)> allowed_slots;
	HandUse hand_use = HandUse::None;
	std::optional<WeaponDefinition> weapon;
	std::optional<ShieldDefinition> shield;
	std::optional<ArmourDefinition> armour;
};

// A material's maximum durability at each of the seven quality tiers, declared
// per tier rather than derived, and never a current value.
struct MaterialDefinition {
	MaterialId id = 0;
	std::vector<unsigned> max_durability_by_quality;  // exactly seven, each positive
};

// The validated set of definitions. Build it once, keep it alive as long as any
// character holds pointers into it.
class ItemCatalog {
public:
	// Validates everything and keeps immutable copies. Any single bad entry
	// rejects the whole input: there is no partially valid catalog to observe.
	static std::optional<ItemCatalog> create(std::vector<ItemDefinition> items,
											 std::vector<MaterialDefinition> materials);

	// Non-owning lookups, stable for the catalog's lifetime. Zero and unknown ids
	// answer nullptr, and the two id domains never resolve as each other.
	const ItemDefinition *item(DefinitionId id) const;
	const MaterialDefinition *material(MaterialId id) const;

private:
	std::unordered_map<DefinitionId, ItemDefinition> items_;
	std::unordered_map<MaterialId, MaterialDefinition> materials_;
};

}  // namespace open
