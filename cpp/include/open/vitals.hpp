#pragma once

#include "open/attributes.hpp"
#include "open/character_rules.hpp"
#include "open/skills.hpp"

#include <array>
#include <cstddef>

namespace open {

struct Pool {
	float current;
	float maximum;
};

enum class ResourceUse {
	Sprint,
	MeleeSwing,
	Labour,
	Swimming,
	Spell,
	Count,
};

bool valid(ResourceUse use);

// Main pools plus a health pool per body part. One hit damages main HP and the
// targeted part once each; overkill on the part is not charged to main HP again.
// Vitals holds no references: the sheet is passed in whenever it is needed, so
// Character can own all of these by value.
class Vitals {
public:
	// A created character starts full. A constructor cannot report failure, so it
	// never refuses a sheet: an impossible maximum is clamped to the float range
	// instead of becoming infinity. recalculate() is the gate that says no, and
	// Character is the layer that validates before it gets here.
	Vitals(const Attributes &attributes, const Skills &skills);

	// Checked getters: throw std::invalid_argument for a bad id.
	Pool pool(PoolKind kind) const;
	Pool part(BodyPart part) const;

	// Recomputes maxima after level, base or training changes. Returns false
	// without touching anything if a maximum would be invalid. Current values are
	// preserved and clamped: this never refills, heals or revives.
	bool recalculate(const Attributes &attributes, const Skills &skills);

	// Sprint and swimming quantities are seconds; the other uses take quantity 1.
	// All-or-nothing: without the funds nothing is spent.
	bool spend(ResourceUse use, float quantity = 1.0f);

	// Pays a cost the caller calculated, for actions whose price is not one of the
	// fixed table entries: a swing costs what the weapon and the wielder's
	// shortfall add up to, and a defense has its own tunable price. Finite and
	// nonnegative, or the call is refused. All-or-nothing like spend(): affordable
	// pays exactly once, anything less pays nothing and says no, and a dead sheet
	// pays neither. Zero is a legal price that spends zero.
	bool spend_stamina(float amount);

	// Resources only; HP and body parts never regen here.
	bool regenerate(float seconds, bool in_combat, const Attributes &attributes);

	bool damage(BodyPart part, float amount);

	bool alive() const;
	float leg_multiplier() const;
	bool one_hand() const;
	bool two_hands() const;
	bool can_carry() const;

private:
	std::array<Pool, static_cast<std::size_t>(PoolKind::Count)> pools_{};
	std::array<Pool, static_cast<std::size_t>(BodyPart::Count)> parts_{};
};

}  // namespace open
