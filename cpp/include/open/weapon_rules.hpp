#pragma once

#include "open/character.hpp"
#include "open/items.hpp"

#include <optional>

namespace open {

// The arithmetic of wielding: what quality is worth, how far below a weapon's
// requirements a character is, what a swing costs and how soon the next one can
// come. These are pure functions over the sheet and the definition. They change
// nothing, spend nothing and draw no random number; the combat pipeline (Task 10)
// is what calls them and applies the result.
//
// Locked in docs/weapons.md: Character handling, Materials/quality/durability,
// and the speed band shared with docs/combat.md section 4.

// Quality multiplies weapon damage and nothing else. The seven tiers are
// 1, 2, 4, 8, 16, 32, 100: the jump to 100 is deliberate, so it is not smoothed,
// and a tier outside 1..7 has no value to report.
std::optional<double> quality_damage_multiplier(unsigned quality);

// How well one declared requirement is met: min(actual / required, 1) squared, so
// half the requirement is a quarter of the effect and meeting it is whole.
// Refused, not clamped: a non-positive required value is not a requirement, and a
// negative or non-finite actual is not a measurement.
std::optional<double> requirement_effectiveness(double actual, double required);

// Overall weapon effectiveness: the worst of the attribute requirements and the
// trained-rank requirement, taken as a minimum and never multiplied together,
// because the largest shortfall is what limits the swing. Attributes are checked
// effective (level and blessing are already inside them); the job is checked on
// its trained rank, not its effective skill.
//
// This is deliberately not the trained/200 damage multiplier: a rank-1 character
// who meets a rank-1 requirement is fully effective here and still swings at
// 1/200 of the strength there. The two penalties stack in the attack pipeline.
// A requirement absent from the definition is omitted, so an unarmed swing and a
// starter shield are 1.0 here.
std::optional<double> weapon_effectiveness(const Character &character, const WeaponStats &stats,
										   const SkillJob &job);

// Stamina cost of a swing: base x (2 - effectiveness). Full effectiveness pays the
// base, none pays double, and a fractional effectiveness pays a fractional cost.
// Effectiveness is expected to come from weapon_effectiveness, that is 0..1.
double weapon_attack_cost(double base_cost, double effectiveness);

// Seconds until the next attack: rate = (0.2 + 2.8 x trained/200) x speed x
// effectiveness, cooldown = 1 / rate clamped to the game's [1/3, 5] band, then
// doubled and reclamped when the swing was not paid for. Zero effectiveness
// swings as slowly as the game allows instead of dividing by zero, and nonsense
// input answers the slowest legal cooldown rather than NaN or infinity.
//
// The cooldown belongs to the character, so the caller stores the result on the
// sheet; swapping or losing a weapon never resets what is already running.
float weapon_cooldown(unsigned trained, double speed, double effectiveness, bool exhausted);

}  // namespace open
