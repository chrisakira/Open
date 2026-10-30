#include "open/weapon_rules.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

#include "open/character_rules.hpp"
#include "open/items.hpp"

namespace open {
namespace {

// The same seven tiers for the same reasons in both directions: damage scales with
// craftsmanship, and the last tier is a cliff on purpose.
constexpr std::array<double, kQualityTierCount> kQualityMultipliers = {1.0,  2.0,  4.0,   8.0,
																	   16.0, 32.0, 100.0};

// docs/combat.md section 4: nothing in the game attacks faster than 3/s or slower
// than once per 5 s, whatever the weapon or the shortfall. weapon_rules.cpp owns
// these for the typed path; the scalar helper in combat_rules.cpp keeps its own
// copy until Task 10 replaces that path, and the two bands are the same numbers.
constexpr float kFastestCooldown = 1.0f / 3.0f;
constexpr float kSlowestCooldown = 5.0f;

// The rate floor and span from the same section: 0.2 + 2.8 x trained/200.
constexpr double kRateFloor = 0.2;
constexpr double kRateSpan = 2.8;
constexpr double kMaxTrained = 200.0;

double clamp(double value, double low, double high) {
	return value < low ? low : (value > high ? high : value);
}

}  // namespace

std::optional<double> quality_damage_multiplier(unsigned quality) {
	if (quality < 1 || quality > kQualityTierCount) {
		return std::nullopt;
	}
	return kQualityMultipliers[quality - 1];
}

std::optional<double> requirement_effectiveness(double actual, double required) {
	if (!std::isfinite(required) || required <= 0.0) {
		return std::nullopt;
	}
	if (!std::isfinite(actual) || actual < 0.0) {
		return std::nullopt;
	}
	const double ratio = clamp(actual / required, 0.0, 1.0);
	return ratio * ratio;
}

std::optional<double> weapon_effectiveness(const Character &character, const WeaponStats &stats,
										   const SkillJob &job) {
	if (stats.required_trained_rank == 0) {
		return std::nullopt;  // a declared requirement is always positive
	}
	if (!valid(job.basic)) {
		return std::nullopt;
	}

	// The worst single shortfall limits the weapon. Multiplying them would punish a
	// character twice for one bad day, and the spec says the largest shortfall is
	// the limiting factor.
	double effectiveness = 1.0;
	for (const Requirement &need : stats.requirements) {
		if (!valid(need.attribute) || !std::isfinite(need.value) || need.value <= 0.0) {
			return std::nullopt;
		}
		// Effective, not base: the level multiplier and the blessing are already
		// inside the attribute, so they are never separate requirements.
		const std::optional<double> met =
			requirement_effectiveness(static_cast<double>(character.attribute(need.attribute)), need.value);
		if (!met.has_value()) {
			return std::nullopt;
		}
		effectiveness = *met < effectiveness ? *met : effectiveness;
	}

	const unsigned trained = character.resolve_skill(job.basic, job.option).rank;
	const std::optional<double> skilled = requirement_effectiveness(static_cast<double>(trained),
															 static_cast<double>(stats.required_trained_rank));
	if (!skilled.has_value()) {
		return std::nullopt;
	}
	return *skilled < effectiveness ? *skilled : effectiveness;
}

double weapon_attack_cost(double base_cost, double effectiveness) {
	return base_cost * (2.0 - effectiveness);
}

float weapon_cooldown(unsigned trained, double speed, double effectiveness, bool exhausted) {
	if (!std::isfinite(speed) || !std::isfinite(effectiveness) || speed <= 0.0 || effectiveness <= 0.0) {
		// Zero effectiveness is a real state (a wholly unfit wielder) and answers the
		// slowest permitted cooldown instead of dividing by it. Anything worse than
		// that is a caller bug, and the safe answer is still the same.
		return kSlowestCooldown;
	}

	const double skill_rate = kRateFloor + kRateSpan * (static_cast<double>(trained) / kMaxTrained);
	const double rate = skill_rate * speed * effectiveness;
	if (!std::isfinite(rate) || rate <= 0.0) {
		return kSlowestCooldown;
	}

	double seconds = 1.0 / rate;
	seconds = clamp(seconds, static_cast<double>(kFastestCooldown), static_cast<double>(kSlowestCooldown));
	if (exhausted) {
		// An unpaid swing is slower, and still cannot leave the band.
		seconds = clamp(seconds * 2.0, static_cast<double>(kFastestCooldown),
						static_cast<double>(kSlowestCooldown));
	}
	return static_cast<float>(seconds);
}

}  // namespace open
