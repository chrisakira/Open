#include "open/items.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

#include "open/character_rules.hpp"

namespace open {

bool valid(WeightClass classification) {
	const int id = static_cast<int>(classification);
	return id >= 0 && id < static_cast<int>(WeightClass::Count);
}

bool valid(BenchType type) {
	const int id = static_cast<int>(type);
	return id >= 0 && id < static_cast<int>(BenchType::Count);
}

bool operator==(const MaterialQuantity &left, const MaterialQuantity &right) {
	return left.material == right.material && left.quantity == right.quantity;
}

namespace {

using SlotSet = std::bitset<static_cast<std::size_t>(Slot::Count)>;

constexpr double kPercentMax = 100.0;
constexpr double kSpeedMin = 0.1;
constexpr double kSpeedMax = 2.0;
// The tier count is the shared constant in items.hpp.

bool in_range(double value, double low, double high) {
	return std::isfinite(value) && value >= low && value <= high;
}

bool nonnegative(double value) {
	return std::isfinite(value) && value >= 0.0;
}

bool positive(double value) {
	return std::isfinite(value) && value > 0.0;
}

// The three-way fields are validated as a unit wherever all three share a bound.
bool percent_triple(const DamageComponents &values) {
	return in_range(values.cut, 0.0, kPercentMax) && in_range(values.pierce, 0.0, kPercentMax) &&
		   in_range(values.blunt, 0.0, kPercentMax);
}

// A family always; an option only when it is that family's own specialisation.
bool valid_job(const SkillJob &job) {
	if (!valid(job.basic)) {
		return false;
	}
	if (!job.option.has_value()) {
		return true;
	}
	const AdvancedSkill option = *job.option;
	return valid(option) && advanced_rule(option).parent == job.basic;
}

bool valid_requirements(const std::vector<Requirement> &requirements) {
	for (std::size_t i = 0; i < requirements.size(); ++i) {
		if (!valid(requirements[i].attribute) || !positive(requirements[i].value)) {
			return false;
		}
		// Two entries for one attribute would make the sum ambiguous; enchantments
		// add their own requirements later, never the base weapon twice.
		for (std::size_t j = i + 1; j < requirements.size(); ++j) {
			if (requirements[j].attribute == requirements[i].attribute) {
				return false;
			}
		}
	}
	return true;
}

// Only that the ids are usable now: that they name real techniques is Task 12's
// check, and a definition must validate before any technique registry exists.
bool valid_technique_ids(const std::vector<TechniqueId> &ids) {
	for (std::size_t i = 0; i < ids.size(); ++i) {
		if (ids[i] == 0) {
			return false;
		}
		for (std::size_t j = i + 1; j < ids.size(); ++j) {
			if (ids[j] == ids[i]) {
				return false;
			}
		}
	}
	return true;
}

bool claims_a_hand(const SlotSet &slots) {
	return slots.test(static_cast<std::size_t>(Slot::LeftHand)) ||
		   slots.test(static_cast<std::size_t>(Slot::RightHand));
}

bool exactly_both_hands(const SlotSet &slots) {
	SlotSet held;
	held.set(static_cast<std::size_t>(Slot::LeftHand));
	held.set(static_cast<std::size_t>(Slot::RightHand));
	return slots == held;
}

// Placement is the equipment container's business, so a definition has to agree
// with it up front: held records name the two held slots and nothing else, and
// anything else stays out of them.
bool valid_placement(const ItemDefinition &item) {
	if (item.id == 0 || !valid(item.hand_use) || item.allowed_slots.none()) {
		return false;
	}
	if (item.hand_use == HandUse::None) {
		return !claims_a_hand(item.allowed_slots);
	}
	return exactly_both_hands(item.allowed_slots);
}

bool valid_weapon(const ItemDefinition &item) {
	if (!item.weapon.has_value()) {
		return true;
	}
	if (item.hand_use == HandUse::None) {
		return false;  // a weapon nobody can hold is not a weapon
	}
	const WeaponStats &stats = item.weapon->stats;
	if (!valid_job(item.weapon->job) || !valid(stats.weight_class)) {
		return false;
	}
	if (!nonnegative(stats.damage.cut) || !nonnegative(stats.damage.pierce) ||
		!nonnegative(stats.damage.blunt)) {
		return false;
	}
	// A usable weapon has at least one component above zero; which ones is balance.
	if (stats.damage.cut <= 0.0 && stats.damage.pierce <= 0.0 && stats.damage.blunt <= 0.0) {
		return false;
	}
	if (!percent_triple(stats.penetration) || !in_range(stats.speed, kSpeedMin, kSpeedMax) ||
		!positive(stats.max_reach) || !in_range(stats.min_reach, 0.0, stats.max_reach) ||
		!nonnegative(stats.stamina_cost) || !in_range(stats.block_percent, 0.0, kPercentMax) ||
		!nonnegative(stats.weight) || stats.required_trained_rank == 0) {
		return false;
	}
	return valid_requirements(stats.requirements) && valid_technique_ids(item.weapon->supported_techniques);
}

bool valid_shield(const ItemDefinition &item) {
	if (!item.shield.has_value()) {
		return true;
	}
	if (item.hand_use == HandUse::None || !valid_job(item.shield->job)) {
		return false;
	}
	return in_range(item.shield->block_percent, 0.0, kPercentMax) && nonnegative(item.shield->weight);
}

bool valid_armour(const ItemDefinition &item) {
	if (!item.armour.has_value()) {
		return true;
	}
	if (claims_a_hand(item.allowed_slots)) {
		return false;  // gloves wear on Hands; the held slots are for held items
	}
	return percent_triple(item.armour->resistance_percent) && nonnegative(item.armour->weight);
}

bool valid_item(const ItemDefinition &item) {
	if (!valid_placement(item)) {
		return false;
	}
	// One record, one role: the stats types do not overlap on purpose.
	const int roles = static_cast<int>(item.weapon.has_value()) +
					  static_cast<int>(item.shield.has_value()) +
					  static_cast<int>(item.armour.has_value());
	if (roles > 1) {
		return false;
	}
	return valid_weapon(item) && valid_shield(item) && valid_armour(item);
}

// At least one effect, and any requirement has a real attribute and divisor. A
// definition that does nothing is not an enchantment, it is a typo.
bool valid_enchantment(const EnchantmentDefinition &enchantment) {
	if (enchantment.id == 0 || !std::isfinite(enchantment.blunt_per_trained_rank) ||
		enchantment.blunt_per_trained_rank < 0.0) {
		return false;
	}
	if (enchantment.requirement.has_value()) {
		if (!valid(*enchantment.requirement) || enchantment.requirement_divisor == 0) {
			return false;
		}
	} else if (enchantment.requirement_divisor != 0) {
		return false;
	}
	return enchantment.blunt_per_trained_rank > 0.0 || enchantment.requirement.has_value();
}

// A technique that cannot be priced or cannot be unlocked is a typo, not a move. Every
// factor is a positive finite multiplier (an absent effect is 1), the cooldown is a
// positive finite number of seconds, a declared stamina cost is a positive finite number,
// the required rank is a rank a sheet can actually be at, and the titles that grant it are
// real and listed once.
bool valid_technique(const TechniqueDefinition &technique) {
	if (technique.id == 0 || technique.required_rank == 0) {
		return false;
	}
	if (!std::isfinite(technique.cooldown) || technique.cooldown <= 0.0) {
		return false;
	}
	const DamageComponents factors = technique.factors;
	if (!std::isfinite(factors.cut) || !std::isfinite(factors.pierce) ||
		!std::isfinite(factors.blunt) || factors.cut <= 0.0 || factors.pierce <= 0.0 ||
		factors.blunt <= 0.0) {
		return false;
	}
	if (technique.stamina_cost.has_value() &&
		(!std::isfinite(*technique.stamina_cost) || *technique.stamina_cost <= 0.0)) {
		return false;
	}
	std::vector<Title> granted = technique.granting_titles;
	for (std::size_t i = 0; i < granted.size(); ++i) {
		if (!valid(granted[i])) {
			return false;
		}
		for (std::size_t j = i + 1; j < granted.size(); ++j) {
			if (granted[j] == granted[i]) {
				return false;
			}
		}
	}
	return true;
}

bool valid_material(const MaterialDefinition &material) {
	if (material.id == 0 || material.max_durability_by_quality.size() != kQualityTierCount) {
		return false;
	}
	for (const unsigned maximum : material.max_durability_by_quality) {
		if (maximum == 0) {
			return false;
		}
	}
	return true;
}

}  // namespace

// The applied form of an enchantment, at a captured rank: added blunt is the rank
// times the definition's own rate, and the requirement is the ceiling of the rank over
// the definition's divisor. Both are then frozen on the weapon.
std::optional<AppliedEnchantment> make_applied_enchantment(const EnchantmentDefinition &definition,
														   unsigned trained) {
	if (trained == 0 || !valid_enchantment(definition)) {
		return std::nullopt;
	}
	AppliedEnchantment applied;
	applied.definition = definition.id;
	applied.added_blunt = static_cast<double>(trained) * definition.blunt_per_trained_rank;
	if (!std::isfinite(applied.added_blunt) || applied.added_blunt < 0.0) {
		return std::nullopt;
	}
	if (definition.requirement.has_value()) {
		// ceil(trained / divisor), in integers: rank 1 of 20 still asks for 1.
		const unsigned value =
			(trained + definition.requirement_divisor - 1u) / definition.requirement_divisor;
		if (value == 0) {
			return std::nullopt;
		}
		applied.requirement = Requirement{*definition.requirement, static_cast<double>(value)};
	}
	return applied;
}

std::optional<ItemCatalog> ItemCatalog::create(std::vector<ItemDefinition> items,
											   std::vector<MaterialDefinition> materials,
											   std::vector<EnchantmentDefinition> enchantments,
											   std::vector<TechniqueDefinition> techniques,
											   std::vector<CraftRecipe> recipes) {
	ItemCatalog catalog;
	// Techniques first, because a weapon's support list points at them: the link is a
	// foreign key, and it is checked below once every table exists.
	for (auto &technique : techniques) {
		if (!valid_technique(technique)) {
			return std::nullopt;
		}
		const TechniqueId id = technique.id;
		if (catalog.techniques_.count(id) != 0) {
			return std::nullopt;
		}
		catalog.techniques_.emplace(id, std::move(technique));
	}
	for (auto &item : items) {
		if (!valid_item(item)) {
			return std::nullopt;
		}
		const DefinitionId id = item.id;
		if (catalog.items_.count(id) != 0) {
			return std::nullopt;
		}
		catalog.items_.emplace(id, std::move(item));
	}
	for (auto &material : materials) {
		if (!valid_material(material)) {
			return std::nullopt;
		}
		const MaterialId id = material.id;
		if (catalog.materials_.count(id) != 0) {
			return std::nullopt;
		}
		catalog.materials_.emplace(id, std::move(material));
	}
	for (auto &enchantment : enchantments) {
		if (!valid_enchantment(enchantment)) {
			return std::nullopt;
		}
		const EnchantmentId id = enchantment.id;
		if (catalog.enchantments_.count(id) != 0) {
			return std::nullopt;
		}
		catalog.enchantments_.emplace(id, std::move(enchantment));
	}
	// Every technique a weapon claims to support has to exist. A weapon pointing at
	// nothing is not a weapon with a hidden move: it is a definition that will not resolve
	// at the moment someone equips it, so it never becomes a catalog.
	for (const auto &entry : catalog.items_) {
		if (!entry.second.weapon.has_value()) {
			continue;
		}
		for (const TechniqueId supported : entry.second.weapon->supported_techniques) {
			if (catalog.techniques_.count(supported) == 0) {
				return std::nullopt;
			}
		}
	}
	// Recipes last: they are foreign keys into everything above. A crafting recipe's output
	// that is not a weapon would forge a shield from a smithing bench - not a rule the
	// slice has, so this loop refuses to even hold the option. Repair and enchanting act
	// on a target the request names, so they name no output and no material at all, and
	// exactly one of each exists: two repair recipes would make "the repair price" a
	// question with two answers.
	RecipeId repair_recipe = 0;
	RecipeId enchant_recipe = 0;
	for (auto &recipe : recipes) {
		if (recipe.id == 0 || !valid(recipe.kind) || !valid(recipe.bench)
			|| recipe.cost.empty() || catalog.recipes_.count(recipe.id) != 0) {
			return std::nullopt;
		}
		switch (recipe.kind) {
		case WorkKind::CraftWeapon:
			if (recipe.bench != BenchType::Smithing
				|| catalog.item(recipe.output) == nullptr
				|| !catalog.item(recipe.output)->weapon.has_value()
				|| catalog.material(recipe.material) == nullptr) {
				return std::nullopt;
			}
			break;
		case WorkKind::Repair:
			if (recipe.bench != BenchType::Smithing || recipe.output != 0
				|| recipe.material != 0) {
				return std::nullopt;
			}
			if (repair_recipe != 0) {
				return std::nullopt;
			}
			repair_recipe = recipe.id;
			break;
		case WorkKind::EnchantWeapon:
			if (recipe.bench != BenchType::Enchanting || recipe.output != 0
				|| recipe.material != 0) {
				return std::nullopt;
			}
			if (enchant_recipe != 0) {
				return std::nullopt;
			}
			enchant_recipe = recipe.id;
			break;
		case WorkKind::Count:
			return std::nullopt;
		}
		std::vector<MaterialId> listed;
		for (const MaterialQuantity &line : recipe.cost) {
			if (line.quantity == 0 || catalog.material(line.material) == nullptr
				|| std::find(listed.begin(), listed.end(), line.material) != listed.end()) {
				return std::nullopt;
			}
			listed.push_back(line.material);
		}
		const RecipeId id = recipe.id;
		catalog.recipes_.emplace(id, std::move(recipe));
	}
	return catalog;
}

const ItemDefinition *ItemCatalog::item(DefinitionId id) const {
	if (id == 0) {
		return nullptr;
	}
	const auto found = items_.find(id);
	return found == items_.end() ? nullptr : &found->second;
}

const EnchantmentDefinition *ItemCatalog::enchantment(EnchantmentId id) const {
	if (id == 0) {
		return nullptr;
	}
	const auto found = enchantments_.find(id);
	return found == enchantments_.end() ? nullptr : &found->second;
}

// Technique ids are their own domain: no item, material or enchantment id resolves here,
// and no technique id resolves there.
const TechniqueDefinition *ItemCatalog::technique(TechniqueId id) const {
	if (id == 0) {
		return nullptr;
	}
	const auto found = techniques_.find(id);
	return found == techniques_.end() ? nullptr : &found->second;
}

const MaterialDefinition *ItemCatalog::material(MaterialId id) const {
	if (id == 0) {
		return nullptr;
	}
	const auto found = materials_.find(id);
	return found == materials_.end() ? nullptr : &found->second;
}

// A recipe is its own domain of ids, like everything else here: zero names nothing, and
// no item, material or technique id resolves as one.
const CraftRecipe *ItemCatalog::recipe(RecipeId id) const {
	if (id == 0) {
		return nullptr;
	}
	const auto found = recipes_.find(id);
	return found == recipes_.end() ? nullptr : &found->second;
}

std::vector<RecipeId> ItemCatalog::recipe_ids() const {
	std::vector<RecipeId> ids;
	ids.reserve(recipes_.size());
	for (const auto &entry : recipes_) {
		ids.push_back(entry.first);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

std::vector<EnchantmentId> ItemCatalog::enchantment_ids() const {
	std::vector<EnchantmentId> ids;
	ids.reserve(enchantments_.size());
	for (const auto &entry : enchantments_) {
		ids.push_back(entry.first);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

}  // namespace open
