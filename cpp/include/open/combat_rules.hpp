#pragma once

#include "open/character_rules.hpp"
#include "open/move_order.hpp"

#include <optional>
#include <random>

namespace open {

// The profiles read a sheet; they never own or construct one, and the sheet
// needs these types for its own swing method, so the dependency points this way.
class Character;

// Placeholder reach of a melee attack. Height is ignored, exactly as it is when
// walking.
inline constexpr float kMeleeRange = 1.5f;

// True when the two positions are within melee reach horizontally. Measured on
// X/Z only and inclusive at kMeleeRange; a non-finite horizontal input is never
// in range, so a bad observation cannot open a swing.
bool melee_in_range(const Vec3 &self, const Vec3 &target);

// Item numbers are placeholders until the item catalog exists: every weapon hits
// with this, every usable block is worth this, and nothing soaks damage. A tag
// says what an item is for; it never carries a stat of its own.
inline constexpr float kPlaceholderWeapon = 8.0f;
inline constexpr float kPlaceholderBlock = 8.0f;
inline constexpr float kPlaceholderArmour = 0.0f;

// Which skill job one swing uses, and the trained rank behind it. `option` is the
// item's or the character's own specialisation: the rank is read through
// Skills::resolve, so an unchosen specialisation falls back to its frozen parent
// exactly as record_use does.
struct AttackProfile {
	BasicSkill basic;
	std::optional<AdvancedSkill> option;
	unsigned trained;
};

enum class DefendAction {
	Block,
	Dodge,
};

// What a defender defends with, and the placeholder value that stands between
// them and the blow.
struct DefendProfile {
	DefendAction action;
	BasicSkill basic;
	std::optional<AdvancedSkill> option;
	unsigned trained;
	float shield;
};

// The attacker's loadout, read at query time: held gates and limb damage are not
// cached from the moment the item was equipped. A usable non-shield weapon, or
// Unarmed Combat if there is none and one surviving arm or leg remains. Dead, and
// alive with nothing to swing, there is no attack.
std::optional<AttackProfile> attack_profile(const Character &attacker);

// The defender's loadout: a usable shield, else a usable two-handed weapon, else a
// usable one-handed weapon, else a dodge. Blocking is the job for both kinds of
// block, so a shield specialisation is only read for that shield. A dead character
// defends with nothing.
std::optional<DefendProfile> defend_profile(const Character &defender);

// Seconds between swings for a trained rank. Exhaustion doubles the swing's own
// cooldown; it is not a lasting debuff. Always inside [1/3, 5].
float melee_cooldown(unsigned trained, bool exhausted);

// A landed hit: weapon x (trained / 200) minus toughness_base x armour. The
// toughness is the defender's base value, never the level- and blessing-scaled
// one, and exhaustion halves only the offensive term. Clamped at zero.
float melee_damage(unsigned trained, float weapon, float toughness_base, float armour, bool exhausted);

// The same blow between two named sheets: the attack profile's trained rank,
// swung with `weapon`, soaked by the base Toughness of the character handed in as
// `defender`. The defender is a Character here on purpose, because the float
// form above takes a toughness a caller can still read off the wrong sheet.
float melee_damage(const AttackProfile &attack, const Character &defender, float weapon, float armour,
				   bool exhausted);

// A defence only matters with its clock ready, and only ever wins by being
// strictly greater: equality does not nullify.
bool melee_nullifies(bool defend_ready, float shield, unsigned defend_trained, unsigned attack_trained,
					 float weapon);

// What one attempt did. `damage` is the amount the swing computed and handed to
// `Character::damage`, before the sheet clamps an overkill hit at zero: on a
// killing blow the sheet may absorb less than this. `nullified` says a ready
// defence turned the blow aside, and `part` names the body part only when damage
// was applied: a turned-aside or fully soaked hit injures nothing, so it has no
// part to name.
struct SwingResult {
	float damage;
	bool nullified;
	std::optional<BodyPart> part;
};

// The part a landed hit takes: uniform over the six parts a sheet has, with the
// caller's rng so a fight can be replayed from a seed. Count is not a part.
BodyPart random_melee_part(std::mt19937_64 &rng);

}  // namespace open
