#pragma once

#include "open/character_rules.hpp"
#include "open/items.hpp"
#include "open/move_order.hpp"

#include <optional>
#include <random>

namespace open {

// Melee reach for a sheet that has not resolved a weapon yet. The typed rules read a
// reach off the weapon being swung (`WeaponAttackProfile` in weapon_rules.hpp) and this
// constant is only the unarmed fallback's own maximum, kept here because the fallback
// has no definition to read it from. Height is ignored, exactly as it is when walking.
inline constexpr float kMeleeRange = 1.5f;

// True when the two positions are within melee reach horizontally. Measured on
// X/Z only and inclusive at kMeleeRange; a non-finite horizontal input is never
// in range, so a bad observation cannot open a swing.
bool melee_in_range(const Vec3 &self, const Vec3 &target);

// What one attempt did, from the typed exchange in `Character::try_melee_swing`.
//
// `damage` is the whole amount handed to `Character::damage`; the sheet clamps an
// overkill hit at zero, so on a killing blow the target may have absorbed less than
// this. `nullified` says a ready defence turned a blow that had something in it
// entirely aside - it is a description of the exchange, never an XP authority: each
// award reads its own number (`prevented` for a defence, `damage` for an attack).
// `part` names the body part the blow was drawn against whenever a part was drawn: a
// defence that prevented everything draws nothing, and armour that absorbs everything
// still leaves the aimed-at part named.
struct SwingResult {
	float damage;
	bool nullified;
	std::optional<BodyPart> part;
	// What the defence turned aside before armour, which is what pays a Blocking or
	// Dodging XP and Wall progress even when armour would have absorbed the rest.
	double prevented = 0.0;
	// The final typed components, for whatever the exchange has to tell the sheet:
	// blunt is all stagger reads, and the total is the one amount damage() was given.
	DamageComponents final;
	// The owned records the exchange touched, when there were any: the weapon swung
	// and the item a block was made with. A dodge names neither.
	std::optional<ItemId> attack_item;
	std::optional<ItemId> defense_item;
	// The technique this swing performed, when it performed one. A basic attack reports
	// none, which is a different fact from a technique that did no damage.
	std::optional<TechniqueId> technique;
};

// The part a landed hit takes: uniform over the six parts a sheet has, with the
// caller's rng so a fight can be replayed from a seed. Count is not a part.
BodyPart random_melee_part(std::mt19937_64 &rng);

}  // namespace open
