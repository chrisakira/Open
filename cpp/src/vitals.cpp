#include "open/vitals.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace open {
namespace {

constexpr float kFloatMax = std::numeric_limits<float>::max();

std::size_t pool_slot(PoolKind kind) {
	return static_cast<std::size_t>(static_cast<int>(kind));
}

std::size_t part_slot(BodyPart part) {
	return static_cast<std::size_t>(static_cast<int>(part));
}

bool usable(const std::array<Pool, static_cast<std::size_t>(BodyPart::Count)> &parts, BodyPart part) {
	return parts[part_slot(part)].current > 0.0f;
}

float fraction_of_body(BodyPart part) {
	switch (part) {
	case BodyPart::Head:
		return kHeadBodyFraction;
	case BodyPart::Torso:
		return kTorsoBodyFraction;
	case BodyPart::LeftArm:
	case BodyPart::RightArm:
	case BodyPart::LeftLeg:
	case BodyPart::RightLeg:
		return kLimbBodyFraction;
	case BodyPart::Count:
		break;
	}
	throw std::invalid_argument("Vitals: unknown body part id");
}

bool fits_float(double value) {
	return std::isfinite(value) && value >= 0.0 && value <= static_cast<double>(kFloatMax);
}

struct Maxima {
	std::array<double, static_cast<std::size_t>(PoolKind::Count)> pools{};
	std::array<double, static_cast<std::size_t>(BodyPart::Count)> parts{};
	bool valid = true;
};

double average(double a, double b) {
	return (a + b) / 2.0;
}

double attribute_base(const Attributes &attributes, Attribute attribute) {
	return static_cast<double>(attributes.base(attribute));
}

Maxima compute_maxima(const Attributes &attributes, const Skills &skills) {
	// Pool maxima use attribute bases, never effective attributes, so level and
	// blessing cannot stretch a bar.
	const double constitution = attribute_base(attributes, Attribute::Constitution);
	const double profanity = attribute_base(attributes, Attribute::Profanity);
	const double endurance = static_cast<double>(skills.basic(BasicSkill::Endurance).rank);
	const double mana_control = static_cast<double>(skills.basic(BasicSkill::ManaControl).rank);

	Maxima maxima;
	maxima.pools[pool_slot(PoolKind::Hp)] = static_cast<double>(kPoolBase) * constitution;
	maxima.pools[pool_slot(PoolKind::Stamina)] = static_cast<double>(kPoolBase) * endurance * constitution;
	maxima.pools[pool_slot(PoolKind::Mana)] =
		static_cast<double>(kPoolBase) * mana_control * average(constitution, profanity);

	const double main_hp = maxima.pools[pool_slot(PoolKind::Hp)];
	for (std::size_t i = 0; i < maxima.parts.size(); ++i) {
		maxima.parts[i] = main_hp * static_cast<double>(fraction_of_body(static_cast<BodyPart>(i)));
	}

	for (const double value : maxima.pools) {
		if (!fits_float(value)) {
			maxima.valid = false;
		}
	}
	for (const double value : maxima.parts) {
		if (!fits_float(value)) {
			maxima.valid = false;
		}
	}
	return maxima;
}

float clamp_to_float(double value) {
	if (!std::isfinite(value) || value > static_cast<double>(kFloatMax)) {
		return kFloatMax;
	}
	if (value < 0.0) {
		return 0.0f;
	}
	return static_cast<float>(value);
}

}  // namespace

bool valid(ResourceUse use) {
	const int id = static_cast<int>(use);
	return id >= 0 && id < static_cast<int>(ResourceUse::Count);
}

// See the header: this cannot refuse. clamp_to_float keeps the state finite and
// finite-but-absurd; Vitals::recalculate is the predicate that refuses.
Vitals::Vitals(const Attributes &attributes, const Skills &skills) {
	const Maxima maxima = compute_maxima(attributes, skills);
	for (std::size_t i = 0; i < pools_.size(); ++i) {
		const float maximum = clamp_to_float(maxima.pools[i]);
		pools_[i] = Pool{maximum, maximum};
	}
	for (std::size_t i = 0; i < parts_.size(); ++i) {
		const float maximum = clamp_to_float(maxima.parts[i]);
		parts_[i] = Pool{maximum, maximum};
	}
}

Pool Vitals::pool(PoolKind kind) const {
	if (!valid(kind)) {
		throw std::invalid_argument("Vitals::pool: unknown pool id");
	}
	return pools_[pool_slot(kind)];
}

Pool Vitals::part(BodyPart part) const {
	if (!valid(part)) {
		throw std::invalid_argument("Vitals::part: unknown body part id");
	}
	return parts_[part_slot(part)];
}

bool Vitals::recalculate(const Attributes &attributes, const Skills &skills) {
	const Maxima maxima = compute_maxima(attributes, skills);
	if (!maxima.valid) {
		return false;
	}
	for (std::size_t i = 0; i < pools_.size(); ++i) {
		const float maximum = static_cast<float>(maxima.pools[i]);
		pools_[i].maximum = maximum;
		pools_[i].current = std::min(pools_[i].current, maximum);
	}
	for (std::size_t i = 0; i < parts_.size(); ++i) {
		const float maximum = static_cast<float>(maxima.parts[i]);
		parts_[i].maximum = maximum;
		parts_[i].current = std::min(parts_[i].current, maximum);
	}
	return true;
}

bool Vitals::spend(ResourceUse use, float quantity) {
	if (!valid(use)) {
		return false;
	}
	if (!std::isfinite(quantity) || quantity <= 0.0f) {
		return false;
	}
	if (!alive()) {
		return false;
	}

	PoolKind kind = PoolKind::Stamina;
	double cost = 0.0;
	switch (use) {
	case ResourceUse::Sprint:
		cost = static_cast<double>(kSprintStaminaPerSecond) * static_cast<double>(quantity);
		break;
	case ResourceUse::Swimming:
		cost = static_cast<double>(kSwimStaminaPerSecond) * static_cast<double>(quantity);
		break;
	case ResourceUse::MeleeSwing:
		if (quantity != 1.0f) {
			return false;  // a single swing cannot be bought in bulk
		}
		cost = static_cast<double>(kMeleeSwingStamina);
		break;
	case ResourceUse::Labour:
		if (quantity != 1.0f) {
			return false;
		}
		cost = static_cast<double>(kLabourStamina);
		break;
	case ResourceUse::Spell:
		if (quantity != 1.0f) {
			return false;
		}
		kind = PoolKind::Mana;
		cost = static_cast<double>(kSpellMana);
		break;
	case ResourceUse::Count:
		return false;
	}

	if (!std::isfinite(cost) || cost > static_cast<double>(pools_[pool_slot(kind)].current)) {
		return false;
	}
	pools_[pool_slot(kind)].current -= static_cast<float>(cost);
	return true;
}

bool Vitals::regenerate(float seconds, bool in_combat, const Attributes &attributes) {
	if (!std::isfinite(seconds) || seconds < 0.0f) {
		return false;
	}
	if (!alive()) {
		return false;
	}

	const double factor = static_cast<double>(seconds);
	const double stamina_rate =
		static_cast<double>(kStaminaRegenBasePerSecond) *
		attribute_base(attributes, Attribute::Constitution) *
		(in_combat ? static_cast<double>(kCombatStaminaRegenFactor) : 1.0);
	const double mana_rate =
		static_cast<double>(kManaRegenBasePerSecond) *
		average(attribute_base(attributes, Attribute::Intelligence),
				attribute_base(attributes, Attribute::Profanity)) *
		(in_combat ? static_cast<double>(kCombatManaRegenFactor) : 1.0);

	const double stamina_amount = stamina_rate * factor;
	const double mana_amount = mana_rate * factor;
	if (!std::isfinite(stamina_amount) || !std::isfinite(mana_amount)) {
		return false;
	}

	const auto apply = [&](PoolKind kind, double amount) {
		Pool &entry = pools_[pool_slot(kind)];
		const double grown = static_cast<double>(entry.current) + amount;
		entry.current = grown >= static_cast<double>(entry.maximum) ? entry.maximum
																   : static_cast<float>(grown);
	};
	apply(PoolKind::Stamina, stamina_amount);
	apply(PoolKind::Mana, mana_amount);
	return true;
}

bool Vitals::damage(BodyPart part, float amount) {
	if (!valid(part)) {
		return false;
	}
	if (!std::isfinite(amount) || amount < 0.0f) {
		return false;
	}
	if (amount == 0.0f) {
		return true;  // a graze that counts as nothing is still a legal hit
	}

	Pool &main = pools_[pool_slot(PoolKind::Hp)];
	main.current = std::max(0.0f, main.current - amount);
	Pool &target = parts_[part_slot(part)];
	target.current = std::max(0.0f, target.current - amount);
	return true;
}

bool Vitals::alive() const {
	return pools_[pool_slot(PoolKind::Hp)].current > 0.0f && usable(parts_, BodyPart::Head) &&
		   usable(parts_, BodyPart::Torso);
}

float Vitals::leg_multiplier() const {
	if (!alive()) {
		return 0.0f;
	}
	const int standing = static_cast<int>(usable(parts_, BodyPart::LeftLeg)) +
						 static_cast<int>(usable(parts_, BodyPart::RightLeg));
	if (standing >= 2) {
		return 1.0f;
	}
	if (standing == 1) {
		return 0.5f;
	}
	return 0.0f;
}

bool Vitals::one_hand() const {
	return alive() && (usable(parts_, BodyPart::LeftArm) || usable(parts_, BodyPart::RightArm));
}

bool Vitals::two_hands() const {
	return alive() && usable(parts_, BodyPart::LeftArm) && usable(parts_, BodyPart::RightArm);
}

bool Vitals::can_carry() const {
	return alive() && (usable(parts_, BodyPart::LeftArm) || usable(parts_, BodyPart::RightArm));
}

}  // namespace open
