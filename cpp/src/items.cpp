#include "open/items.hpp"

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

std::optional<ItemCatalog> ItemCatalog::create(std::vector<ItemDefinition> items,
											   std::vector<MaterialDefinition> materials) {
	ItemCatalog catalog;
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
	return catalog;
}

const ItemDefinition *ItemCatalog::item(DefinitionId id) const {
	if (id == 0) {
		return nullptr;
	}
	const auto found = items_.find(id);
	return found == items_.end() ? nullptr : &found->second;
}

const MaterialDefinition *ItemCatalog::material(MaterialId id) const {
	if (id == 0) {
		return nullptr;
	}
	const auto found = materials_.find(id);
	return found == materials_.end() ? nullptr : &found->second;
}

}  // namespace open
