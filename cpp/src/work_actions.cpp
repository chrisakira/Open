#include "open/work_actions.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "open/character_rules.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"
#include "open/skills.hpp"
#include "open/work.hpp"

namespace open {
namespace {

// The rank caps are the sheet's own, in skills.hpp. kPercentFull is both "100% of the
// maximum" and the rounding base for a repair.
constexpr unsigned kPercentFull = 100u;

}  // namespace

std::optional<unsigned> weapon_quality_cap(unsigned trained, bool matching_weaponsmith) {
	if (matching_weaponsmith) {
		// Advanced Weaponsmith, 1..200.
		if (trained < 1 || trained > kAdvancedRankCap) {  // skills.hpp
			return std::nullopt;
		}
		if (trained <= 63u) {
			return 4u;
		}
		if (trained <= 127u) {
			return 5u;
		}
		if (trained <= 190u) {
			return 6u;
		}
		return 7u;
	}
	// Basic Smithing, 1..100: the frozen parent for anyone who specialised elsewhere.
	if (trained < 1 || trained > kBasicRankCap) {  // skills.hpp
		return std::nullopt;
	}
	if (trained <= 34u) {
		return 1u;
	}
	if (trained <= 67u) {
		return 2u;
	}
	return 3u;
}

SkillJob weapon_craft_job(const Character &character) {
	const std::optional<AdvancedSkill> chosen = character.chosen(BasicSkill::Smithing);
	if (chosen.has_value() && *chosen == AdvancedSkill::Weaponsmith) {
		return SkillJob{BasicSkill::Smithing, *chosen};
	}
	// Unspecialised, or specialised in armour: weapons are the basic job, and
	// Skills::resolve hands back the frozen 100 rather than a zero.
	return SkillJob{BasicSkill::Smithing, std::nullopt};
}

unsigned max_weapon_quality(const Character &character) {
	const SkillJob job = weapon_craft_job(character);
	const unsigned trained = character.resolve_skill(job.basic, job.option).rank;
	const std::optional<unsigned> cap = weapon_quality_cap(trained, job.option.has_value());
	return cap.has_value() ? *cap : 0u;
}

unsigned repair_points(unsigned max_durability, unsigned trained) {
	// A rank past 100 is still 100% of the maximum: repairs do not get better than
	// whole, and an advanced smith is not repairing twice over.
	const unsigned percent = trained > kPercentFull ? kPercentFull : trained;
	// Rounded up in integer arithmetic, so a 101-point item at rank 1 restores two
	// points rather than the zero that flooring would give.
	const unsigned long long scaled =
		static_cast<unsigned long long>(max_durability) * static_cast<unsigned long long>(percent);
	const unsigned long long rounded = (scaled + kPercentFull - 1u) / kPercentFull;
	if (rounded >= static_cast<unsigned long long>(max_durability)) {
		return max_durability;
	}
	return static_cast<unsigned>(rounded);
}

// --- the workbench queue ------------------------------------------------------------

bool valid(WorkKind kind) {
	const int id = static_cast<int>(kind);
	return id >= 0 && id < static_cast<int>(WorkKind::Count);
}

bool in_bench_reach(const Vec3 &self, const Vec3 &bench) {
	if (!std::isfinite(self.x) || !std::isfinite(self.z) || !std::isfinite(bench.x)
		|| !std::isfinite(bench.z)) {
		return false;  // a position that is not a place is nowhere near anything
	}
	const double dx = static_cast<double>(self.x) - static_cast<double>(bench.x);
	const double dz = static_cast<double>(self.z) - static_cast<double>(bench.z);
	return std::sqrt(dx * dx + dz * dz) <= kBenchReach;  // inclusive, Y ignored
}

namespace {

bool finite_place(const Vec3 &p) {
	return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// The thing being worked on, when the kind names one: owned by this sheet, and a real
// made instance with a durability record. Placement-only legacy records are not repairable
// or enchantable - there is nothing about them to restore or attach to.
std::optional<ItemInstance> worked_item(const Character &who, const WorkRequest &request) {
	if (!request.target.has_value()) {
		return std::nullopt;
	}
	const std::optional<ItemInstance> owned = who.instance(*request.target);
	if (!owned.has_value() || owned->quality == 0u || owned->max_durability == 0u) {
		return std::nullopt;
	}
	return owned;
}

bool workable_kind_target(const Character &who, const WorkRequest &request,
						  const ItemCatalog &catalog, bool require_damage) {
	const std::optional<ItemInstance> target = worked_item(who, request);
	if (!target.has_value()) {
		return false;
	}
	const ItemDefinition *definition = catalog.item(target->definition);
	if (definition == nullptr) {
		return false;
	}
	switch (request.kind) {
	case WorkKind::Repair: {
		const bool repairable = definition->weapon.has_value() || definition->shield.has_value();
		return repairable && (!require_damage || target->durability < target->max_durability);
	}
	case WorkKind::EnchantWeapon:
		return definition->weapon.has_value();
	case WorkKind::CraftWeapon:
	case WorkKind::Count:
		return false;
	}
	return false;
}

// The job a repair pays: the weapon craft job for a blade, and plain basic Smithing for a
// shield, whoever the smith specialises in.
SkillJob repair_job(const Character &who, const ItemInstance &target, const ItemCatalog &catalog) {
	const ItemDefinition *definition = catalog.item(target.definition);
	if (definition != nullptr && definition->weapon.has_value()) {
		return weapon_craft_job(who);
	}
	return SkillJob{BasicSkill::Smithing, std::nullopt};
}

}  // namespace

bool work_request_ok(const Character &who, const WorkRequest &request, const WorkContext &env) {
	if (!who.alive() || !valid(request.kind)) {
		return false;
	}
	const WorkbenchObservation *bench = env.bench(request.bench);
	if (bench == nullptr || !finite_place(bench->position)) {
		return false;
	}
	const CraftRecipe *recipe = env.catalog.recipe(request.recipe);
	// The recipe must exist, do the kind of work asked for, and belong to the kind of bench
	// the observation claims - both ways round, so an enchanting table cannot host a repair
	// and a smithing bench cannot host an enchantment.
	if (recipe == nullptr || recipe->kind != request.kind || recipe->bench != bench->type) {
		return false;
	}
	switch (request.kind) {
	case WorkKind::CraftWeapon:
		// A craft acts on its recipe alone; the tier must at least be a tier. Whether this
		// smith may raise the hammer to that tier is a start-time question, sampled where
		// the work starts.
		if (request.target.has_value() || request.enchantment.has_value()) {
			return false;
		}
		return request.quality >= 1u && request.quality <= kQualityTierCount;
	case WorkKind::Repair:
		// Shape asks only that the target is a real owned weapon or shield; "damaged enough
		// to mend" is re-checked at start, where arrival is allowed to change.
		if (request.quality != 0u || request.enchantment.has_value()) {
			return false;
		}
		return workable_kind_target(who, request, env.catalog, false);
	case WorkKind::EnchantWeapon:
		if (request.quality != 0u || !request.enchantment.has_value()
			|| env.catalog.enchantment(*request.enchantment) == nullptr) {
			return false;
		}
		return workable_kind_target(who, request, env.catalog, false);
	case WorkKind::Count:
		return false;
	}
	return false;
}

std::optional<WorkStart> work_begin(const Character &who, const WorkRequest &request,
									WorkContext &env) {
	if (!who.alive() || !who.can_carry()) {
		return std::nullopt;  // bench work needs hands and a backpack, and re-runs the shape
	}
	if (!work_request_ok(who, request, env)) {
		return std::nullopt;
	}
	const CraftRecipe *recipe = env.catalog.recipe(request.recipe);
	WorkStart start;
	start.request = request;
	switch (request.kind) {
	case WorkKind::CraftWeapon: {
		if (max_weapon_quality(who) < request.quality) {
			return std::nullopt;  // the chosen tier is above what this smith can make
		}
		start.job = weapon_craft_job(who);
		start.trained = who.resolve_skill(start.job.basic, start.job.option).rank;
		const std::optional<ItemId> reserved = env.ids.allocate();
		if (!reserved.has_value()) {
			return std::nullopt;  // no id to promise the output: nothing may be spent
		}
		start.reserved = *reserved;
		break;
	}
	case WorkKind::Repair: {
		const std::optional<ItemInstance> target = worked_item(who, request);
		if (!target.has_value() || target->durability >= target->max_durability) {
			return std::nullopt;  // a whole thing needs no mending and pays no visit
		}
		start.job = repair_job(who, *target, env.catalog);
		start.trained = who.resolve_skill(start.job.basic, start.job.option).rank;
		break;
	}
	case WorkKind::EnchantWeapon: {
		const std::optional<ItemInstance> target = worked_item(who, request);
		if (!target.has_value()
			|| target->enchantments.size() >= static_cast<std::size_t>(target->enchantment_slots)) {
			return std::nullopt;  // no room: a full bench slot is refused before it is paid
		}
		// Only the weapon track raises weapon enchanting. An Armoursmith - or an
		// ArmourEnchant - is the frozen parent for this bench exactly as an Armoursmith is
		// for the forge, so the chosen track is consulted, not assumed.
		const std::optional<AdvancedSkill> chosen = who.chosen(BasicSkill::Enchanting);
		start.job = chosen.has_value() && *chosen == AdvancedSkill::WeaponEnchant
						? SkillJob{BasicSkill::Enchanting, *chosen}
						: SkillJob{BasicSkill::Enchanting, std::nullopt};
		start.trained = who.resolve_skill(start.job.basic, start.job.option).rank;
		const std::optional<AppliedEnchantment> applied =
			make_applied_enchantment(*env.catalog.enchantment(*request.enchantment), start.trained);
		if (!applied.has_value()) {
			return std::nullopt;
		}
		start.enchantment = applied;
		break;
	}
	case WorkKind::Count:
		return std::nullopt;
	}
	// The last gate, and the only one that leaves anything behind: pay by the recipe's own
	// list, all or none. Every earlier refusal above has spent neither ids nor materials.
	if (!env.materials.consume(who.id(), recipe->cost)) {
		return std::nullopt;
	}
	start.paid = recipe->cost;
	return start;
}

bool work_settle(Character &who, const WorkStart &start, WorkContext &env) {
	if (!who.alive()) {
		return false;  // the dead do not collect
	}
	switch (start.request.kind) {
	case WorkKind::CraftWeapon: {
		const CraftRecipe *recipe = env.catalog.recipe(start.request.recipe);
		if (recipe == nullptr || start.reserved == 0) {
			return false;
		}
		const std::optional<ItemInstance> made =
			make_item_instance(env.catalog, recipe->output, recipe->material, start.reserved,
							   start.request.quality);
		if (!made.has_value() || !who.add_instance(*made)) {
			return false;  // the paid-for delivery could not land: spent either way
		}
		who.award_work_xp(start.job, 1u);
		who.record_title_progress(Title::Hammerhand, true);  // one item per success
		return true;
	}
	case WorkKind::Repair: {
		const std::optional<ItemInstance> target = worked_item(who, start.request);
		if (!target.has_value() || target->durability >= target->max_durability) {
			return false;
		}
		const unsigned points = repair_points(target->max_durability, start.trained);
		if (points == 0u || !who.restore_durability(*start.request.target, points)) {
			return false;
		}
		who.award_work_xp(start.job, 1u);
		return true;
	}
	case WorkKind::EnchantWeapon: {
		if (!start.enchantment.has_value() || !start.request.target.has_value()) {
			return false;
		}
		if (!who.attach_enchantment(*start.request.target, *start.enchantment)) {
			return false;
		}
		who.award_work_xp(start.job, 1u);
		return true;
	}
	case WorkKind::Count:
		return false;
	}
	return false;
}

// --- the ledger -----------------------------------------------------------------------

std::uint64_t MaterialLedger::quantity(std::uint64_t owner, MaterialId material) const {
	const auto found = stocks_.find(owner);
	if (found == stocks_.end()) {
		return 0;
	}
	const auto stack = found->second.find(material);
	return stack == found->second.end() ? 0 : stack->second;
}

bool MaterialLedger::consume(std::uint64_t owner, std::span<const MaterialQuantity> cost) {
	if (cost.empty()) {
		return false;  // an empty bill is not a transaction
	}
	for (const MaterialQuantity &line : cost) {
		if (line.quantity == 0 || quantity(owner, line.material) < line.quantity) {
			return false;  // read-only first pass: one short line refuses the whole bill
		}
	}
	for (const MaterialQuantity &line : cost) {
		stocks_[owner][line.material] -= line.quantity;
	}
	return true;
}

bool MaterialLedger::grant(std::uint64_t owner, MaterialId material, std::uint64_t amount) {
	if (amount == 0) {
		return false;
	}
	const std::uint64_t held = quantity(owner, material);
	if (held > std::numeric_limits<std::uint64_t>::max() - amount) {
		return false;  // no wrapping: the stack stays exactly where it was
	}
	stocks_[owner][material] = held + amount;
	return true;
}

bool MaterialLedger::was_granted(std::uint64_t owner) const {
	return std::find(granted_.begin(), granted_.end(), owner) != granted_.end();
}

void MaterialLedger::mark_granted(std::uint64_t owner) {
	if (!was_granted(owner)) {
		granted_.push_back(owner);
	}
}

}  // namespace open
