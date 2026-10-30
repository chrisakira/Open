#pragma once

#include "open/character.hpp"
#include "open/combat_rules.hpp"
#include "open/items.hpp"
#include "open/move_order.hpp"

#include <optional>
#include <vector>

namespace open {

// A percentage, in points: the domain mitigation, resistances and penetrations all
// live in, and the bound the definition validator shares.
inline constexpr double kPercentMax = 100.0;

// Durability at zero is a third of the damage. The locked number is 0.33 rather than
// one third computed, because the difference is what a test would have to chase. It
// discounts a weapon's own offense and, on the other side, a blocking item that has
// nothing left to block with.
inline constexpr double kZeroDurabilityFactor = 0.33;

// What one exchange costs the things it touches. These are separate on purpose: a
// swing, a block that turned something aside, and a defence in general are three
// different actions, and docs/weapons.md keeps each of them tunable alone.
inline constexpr unsigned kWeaponAttackWear = 1;
inline constexpr unsigned kWeaponBlockWear = 1;
inline constexpr double kDefenseStaminaCost = 8.0;

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
// `enchantments` are the ones fixed to the item being swung, when there is one: their
// attribute requirements add to the weapon's own for the same attribute, so a blade
// demanding 1 Intelligence with two Blunt Edges at 2 each asks 5 in total, while a
// Profanity requirement stays a separate check. Passing none judges the weapon alone.
std::optional<double> weapon_effectiveness(const Character &character, const WeaponStats &stats,
										   const SkillJob &job,
										   const std::vector<AppliedEnchantment> &enchantments = {});

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

// Which item is being swung, what it is worth, and how well the wielder measures up
// to it. Resolved from the character's equipment and catalog at the moment the
// attempt is made, so a broken arm takes a weapon out of use without taking it off
// the character, and a cooldown or a cancelled order never removes a loadout.
//
// Captured at the start of an attempt by the caller: durability used for the whole
// action is the value in here, so wear from this very swing cannot discount the
// swing that caused it.
struct WeaponAttackProfile {
	SkillJob job;
	unsigned trained = 0;
	std::optional<ItemId> item;      // none means the locked unarmed fallback
	WeaponStats stats;
	double effectiveness = 1.0;
	unsigned quality = 0;            // unarmed reports tier 1, whose multiplier is 1
	unsigned durability = 0;         // unarmed has nothing to wear
	unsigned max_durability = 0;
	// The enchantments fixed to the blade, as they were made. Their added blunt joins
	// the weapon's own after quality, and their requirements are already folded into
	// `effectiveness` below.
	std::vector<AppliedEnchantment> enchantments;
};

// What a defender defends with. `mitigation` is the item's declared percentage,
// undiscounted: the caller applies the zero-durability and unpaid-action factors,
// so this stays a description of the loadout rather than of one exchange.
struct WeaponDefendProfile {
	DefendAction action;
	SkillJob job;
	unsigned trained = 0;
	std::optional<ItemId> item;      // none means a dodge
	double mitigation = 0.0;         // percent; a dodge is worth everything
	unsigned durability = 0;
};

// The attack loadout: the right hand's usable weapon, else the left hand's, else
// the locked unarmed fallback. A shield never attacks, and neither does a record
// this sheet's catalog cannot describe - a placement-only or undefined record
// confers no stats, so it is skipped rather than guessed at. A two-handed item
// reserves both hands and is still read once. Dead, there is no attack.
std::optional<WeaponAttackProfile> weapon_attack_profile(const Character &character);

// The defense loadout: a usable shield, even at zero durability or zero mitigation;
// otherwise, with a usable weapon, the better of the relevant effective Blocking
// and Dodging, ties to dodge; otherwise a dodge. The clock a defense starts reads
// the trained rank in here, never the effective values the choice compared.
std::optional<WeaponDefendProfile> weapon_defend_profile(const Character &character);

// Horizontal reach with an inclusive maximum: exactly at reach is in reach, a hair
// further is not, and height is ignored as it is when walking. A non-finite input
// or a non-positive reach is never in reach, so a bad observation cannot open a
// swing.
bool horizontal_in_reach(const Vec3 &self, const Vec3 &target, double max_reach);

// The minimum-reach penalty: strictly inside the minimum each offensive component
// is halved, exactly on it is free, and a weapon with no minimum is never penalised.
// One factor among several, so it stacks with exhaustion rather than replacing it.
double minimum_range_factor(double separation, double min_reach);

// Walking toward an attack target at a declared reach: zero when already in reach
// (never retreat), otherwise the sheet's own walk speed toward the point that makes
// the existing 0.4 arrival band stop the actor at reach. For a weapon whose reach is
// shorter than that band the aim point moves past the target, which is what keeps a
// 0.2-reach weapon from stalling outside it. `speed` is the caller's, so an injured
// leg stays half as fast. Bad geometry answers zero rather than a wild velocity.
Vec3 pursuit_velocity(const Vec3 &self, const Vec3 &target, double max_reach, float speed);

// One typed hit, reported at every step it went through. The steps are kept apart
// because they are each read by something different: the defense reports what it
// prevented (which is what pays a Blocking or Dodging XP and Wall progress, and is
// measured before armour even when armour would have absorbed everything), the part
// routing needs the post-defense components, and the sheet takes the final total.
struct DamageResolution {
	DamageComponents offensive;
	DamageComponents after_defense;
	DamageComponents after_armour;
	DamageComponents final;   // after Toughness: what each component became
	double total = 0.0;        // sum of final, the one amount handed to Character::damage
	double prevented = 0.0;    // what the defense took off, before armour
};

// The typed pipeline, in the order the spec fixes: defense first, then armour with
// penetration subtracted from the matching resistance and floored at zero, then the
// defender's base Toughness as a universal divisor. Pure: no part is drawn, no HP is
// moved, no XP is granted and no random number is consumed, so the caller can run it
// against a copy of its state and only commit the results it accepts.
//
// `mitigation_percent` is the effective percentage for this exchange, already
// discounted for a worn-out blocking item or an unpaid defense: 100 is a ready dodge
// and 50 an exhausted one. Penetration touches armour resistance only - never the
// defense, never Toughness. `base_toughness` is the defender's BASE value: this is
// not where a level or a blessing belongs.
std::optional<DamageResolution> resolve_typed_damage(DamageComponents offensive,
													 double mitigation_percent,
													 DamageComponents armour_percent,
													 DamageComponents penetration_points,
													 double base_toughness);

// The one armour piece a hit reads, per the locked routing: Head to Head armour,
// Torso to Chest, either arm to Hands, either leg to Legs. Slot::Feet is never
// consulted - there is no foot hit location yet - and pieces never combine, so the
// answer is at most one piece's resistances. Nothing worn, nothing described by the
// sheet's catalog, or a part that is not a part all answer zero.
DamageComponents armour_for_part(const Character &character, BodyPart part);

// What a swing is actually worth before defense: the weapon's components times its
// quality, then multiplied by the trained-rank fraction, the wielder's effectiveness,
// the technique's own component factors, exhaustion, the minimum-range factor and the
// zero-durability penalty. Every one of those is independent, so an exhausted swing
// inside a spear's minimum with a worn-out blade is halved twice and then multiplied
// by 0.33, not reduced to whichever penalty was worst.
//
// Quality multiplies the base components only; the fixed blunt added by an
// enchantment joins after it (Task 8), and the technique factors apply to the
// combined component. An item at durability 1 is charged nothing: durability is read
// as it was when the attempt began, so this swing's own wear cannot discount it.
std::optional<DamageComponents> scaled_offense(const WeaponAttackProfile &profile,
											   DamageComponents technique_factors, bool exhausted,
											   double min_factor);

}  // namespace open
