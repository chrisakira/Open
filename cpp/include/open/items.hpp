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
using EnchantmentId = std::uint64_t;
using MaterialId = std::uint64_t;
using TechniqueId = std::uint64_t;
using RecipeId = std::uint64_t;
using WorkbenchId = std::uint64_t;

// The identity of one owned thing, which is not a definition id: two swords made
// from one definition are two items, and either can wear out alone.
using ItemId = std::uint64_t;

// Craft quality runs from tier 1 to tier 7; a tier has no name, only a number.
inline constexpr unsigned kQualityTierCount = 7;

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

// What one bench application does, described once for every weapon it is applied to.
// The starter slice has Blunt Edge only: fixed blunt added to the weapon, and an
// Intelligence requirement added to the wielder's workload, both scaled by the
// enchanter's trained rank *at the moment of application* (see AppliedEnchantment).
// An enchantment never expires, is never replaced, and does nothing except add.
struct EnchantmentDefinition {
	EnchantmentId id = 0;
	std::string model_key;  // presentation only
	// Blunt damage added per rank of the enchanting job: 0.01 for Blunt Edge.
	double blunt_per_trained_rank = 0.0;
	// The attribute the wielder must bring, and the divisor for it: Blunt Edge adds
	// ceil(trained / 20) Intelligence. A requirement is absent, never zero.
	std::optional<Attribute> requirement;
	unsigned requirement_divisor = 0;
};

// One enchantment as it was actually made. The values are captured, not derived, so
// the enchanter training further or a new wielder's attributes cannot rewrite a
// weapon that has already left the bench.
struct AppliedEnchantment {
	EnchantmentId definition = 0;
	double added_blunt = 0.0;
	std::optional<Requirement> requirement;  // fixed value for the attribute above
};

// The applied form of an enchantment at a captured rank: added blunt is the rank times
// the definition's rate, and the requirement is ceil(rank / divisor). Refused for rank
// 0, for a definition with no effect, and for arithmetic that would not stay finite.
std::optional<AppliedEnchantment> make_applied_enchantment(const EnchantmentDefinition &definition,
														   unsigned trained);

// One technique: the multipliers it lays on the weapon's own damage components, who may
// use it, and what performing it costs in seconds and stamina. A technique is not a
// weapon and carries no damage of its own — a 1.25 cut factor on a weapon with 0 cut is
// still 0 cut — so the only place its numbers make sense is beside a resolved attack
// profile, which is also where its job comes from.
//
// `required_rank` is a trained rank in the supporting weapon's resolved job (matching
// specialisation if one is chosen, the frozen parent otherwise), never an effective
// skill. `granting_titles` is the second, additive way in: a title on the list unlocks the
// technique whatever the rank says. The support itself always has to come from the weapon,
// from whichever route the technique was unlocked by.
struct TechniqueDefinition {
	TechniqueId id = 0;
	unsigned required_rank = 0;
	std::vector<Title> granting_titles;
	// Per-component multipliers. Every one is positive and finite: an absent effect is 1,
	// not 0, so "this technique says nothing about blunt" cannot read as "blunt is free".
	DamageComponents factors{1.0, 1.0, 1.0};
	// Absent means "whatever the weapon costs", which is the starter slice; a technique may
	// declare its own cost, and the effectiveness adjustment and exhaustion apply to it the
	// same way they apply to the weapon's.
	std::optional<double> stamina_cost;
	// Seconds this technique is unavailable for after it is performed. It belongs to the
	// character and to this technique alone: no other technique shares it, and swapping
	// weapons does not move it.
	double cooldown = 0.0;
};

// Where a record may go and how it is held. Identity only: what an item is worth
// in combat comes from its definition, and what it has left comes from its
// instance. A record with no definition is a placement-only legacy item and
// confers no stats.
struct ItemRef {
	ItemId id = 0;
	std::bitset<static_cast<std::size_t>(Slot::Count)> allowed_slots;
	HandUse hand_use = HandUse::None;
	// Which combat job this item is for, if it is a weapon or a shield at all.
	// OneHandedWeapons/TwoHandedWeapons/Polearms name a weapon, Blocking names a
	// shield, and the optional advanced skill is that item's own specialisation.
	// These are tags only: no damage, armour, reach or weight lives here, and an
	// untagged held record is still a legal item.
	std::optional<BasicSkill> combat_skill;
	std::optional<AdvancedSkill> combat_option;
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

// The two workbenches the starter slice builds: the Smithing bench forges and repairs,
// the Enchanting bench fixes enchantments. A bench type is a rule about which work may
// happen at it, so it is a domain, not a position: where a bench stands is an observation
// the caller feeds the session, never a fact the catalog holds.
enum class BenchType {
	Smithing,
	Enchanting,
	Count,
};

bool valid(BenchType type);

// The three bench actions the slice delivers, declared alongside the recipe that carries
// one: what a recipe *is* and what work it *does* are the same fact in two tables' worth
// of distance, so they live in the same header. There is no "other" and no extension
// point: anything a caller cannot name here is not work.
enum class WorkKind {
	CraftWeapon,
	Repair,
	EnchantWeapon,
	Count,
};

bool valid(WorkKind kind);

// One material holding of a recipe's cost: an id from the material domain and a strictly
// positive amount. Recipes list costs with these, the ledger pays with these, and the
// single type is what keeps a cost spelled the same way on both sides.
struct MaterialQuantity {
	MaterialId material = 0;
	std::uint64_t quantity = 0;
};

// Costs are declared data; comparing two of them is a table check, so the line gets an
// equality and the vectors of them inherit it.
bool operator==(const MaterialQuantity &left, const MaterialQuantity &right);

// One immutable bench recipe: which action it performs, which bench it belongs to, what
// it produces, and what it costs to start. Nothing here can say "sometimes free", "any
// bench", or "work without materials". A crafting recipe names the weapon definition it
// outputs and the material it is made of; the repair and enchanting kinds output nothing
// themselves - they act on a target the request names - so they carry no output, and the
// catalog holds at most one of each. The starter slice's table is exactly: forge each of
// weapon definitions 101-104 for 2 Scrap, repair for 1 Scrap, enchant for 1 Rune Dust.
struct CraftRecipe {
	RecipeId id = 0;
	WorkKind kind = WorkKind::CraftWeapon;
	BenchType bench = BenchType::Smithing;
	DefinitionId output = 0;   // craft: a weapon definition in the same catalog; else 0
	MaterialId material = 0;   // craft: what the output instance is made of; else 0
	std::vector<MaterialQuantity> cost;  // non-empty, known materials, positive amounts
};

// The validated set of definitions. Build it once, keep it alive as long as any
// character holds pointers into it.
class ItemCatalog {
public:
	// Validates everything and keeps immutable copies. Any single bad entry
	// rejects the whole input: there is no partially valid catalog to observe.
	// Techniques are validated before the items, because a weapon's support list is a
	// foreign key into them: an id that names no technique is a broken definition and
	// rejects the whole catalog rather than becoming a technique nobody can perform.
	// Recipes are validated last, because they are foreign keys into all of it: an
	// output that names no weapon, a material nobody declared, or a bench that is not
	// a bench rejects the whole catalog.
	static std::optional<ItemCatalog> create(std::vector<ItemDefinition> items,
											 std::vector<MaterialDefinition> materials,
											 std::vector<EnchantmentDefinition> enchantments = {},
											 std::vector<TechniqueDefinition> techniques = {},
											 std::vector<CraftRecipe> recipes = {});

	// Non-owning lookups, stable for the catalog's lifetime. Zero and unknown ids
	// answer nullptr, and the two id domains never resolve as each other.
	const ItemDefinition *item(DefinitionId id) const;
	const MaterialDefinition *material(MaterialId id) const;
	const EnchantmentDefinition *enchantment(EnchantmentId id) const;
	const TechniqueDefinition *technique(TechniqueId id) const;
	const CraftRecipe *recipe(RecipeId id) const;

	// Every recipe id, sorted: the popup enumerates what a bench can make without
	// guessing at ids, and a test can insist it enumerated all of them.
	std::vector<RecipeId> recipe_ids() const;

private:
	std::unordered_map<DefinitionId, ItemDefinition> items_;
	std::unordered_map<MaterialId, MaterialDefinition> materials_;
	std::unordered_map<EnchantmentId, EnchantmentDefinition> enchantments_;
	std::unordered_map<TechniqueId, TechniqueDefinition> techniques_;
	std::unordered_map<RecipeId, CraftRecipe> recipes_;
};

}  // namespace open
