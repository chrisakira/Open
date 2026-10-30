#include "open/work_actions.hpp"

#include <limits>

#include "open/character_rules.hpp"
#include "open/items.hpp"
#include "open/skills.hpp"

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

}  // namespace open
