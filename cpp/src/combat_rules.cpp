#include "open/combat_rules.hpp"

#include "open/character.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <random>
#include <vector>

namespace open {
namespace {

constexpr float kSlowestCooldown = 5.0f;
constexpr float kFastestCooldown = 1.0f / 3.0f;

// Combat uses trained ranks, never effective attributes, so every rank in a
// formula is a trained number over this same scale.
constexpr float kRankScale = 200.0f;

// The parts a melee hit can take: the whole BodyPart enum except Count.
constexpr int kMeleePartFaces = 6;

bool usable(float value) {
	return std::isfinite(value);
}

float rank_fraction(unsigned trained) {
	return static_cast<float>(trained) / kRankScale;
}

float in_band(float seconds) {
	return std::clamp(seconds, kFastestCooldown, kSlowestCooldown);
}

// The held records this character can actually use, in LeftHand then RightHand
// order. A two-handed item reserves both hands but is stored once, so it appears
// only once here: this slice has no dual-wield attack.
std::vector<ItemRef> usable_held(const Character &character) {
	std::vector<ItemRef> held;
	const std::optional<ItemId> hands[2] = {character.equipped(Slot::LeftHand),
										   character.equipped(Slot::RightHand)};
	std::optional<ItemId> seen;
	for (const std::optional<ItemId> &id : hands) {
		if (!id.has_value() || *id == 0 || (seen.has_value() && *id == *seen)) {
			continue;
		}
		seen = id;
		// Gate at query time: a broken arm takes gear out of use without taking it
		// off the character.
		if (!character.held_usable(*id)) {
			continue;
		}
		const std::optional<ItemRef> record = character.item(*id);
		if (record.has_value()) {
			held.push_back(*record);
		}
	}
	return held;
}

// Blocking is the shield tag, so it names a protector rather than a weapon.
bool is_shield(const ItemRef &item) {
	return item.combat_skill.has_value() && *item.combat_skill == BasicSkill::Blocking;
}

// What a held record fights with. An untagged held record is the generic weapon
// for its hand use; only the tagged ones name a polearm.
std::optional<BasicSkill> weapon_basic(const ItemRef &item) {
	if (item.combat_skill.has_value()) {
		return is_shield(item) ? std::nullopt : item.combat_skill;
	}
	switch (item.hand_use) {
	case HandUse::OneHanded:
		return BasicSkill::OneHandedWeapons;
	case HandUse::TwoHanded:
		return BasicSkill::TwoHandedWeapons;
	case HandUse::None:
	case HandUse::Count:
		break;
	}
	return std::nullopt;
}

// Only a tagged record names its own specialisation, so a generic weapon never
// inherits whatever weapon spec the character happens to have chosen.
std::optional<AdvancedSkill> record_option(const ItemRef &item) {
	return item.combat_skill.has_value() ? item.combat_option : std::nullopt;
}

// Unarmed Combat needs one surviving limb to hit with or to stand on.
bool has_surviving_limb(const Character &character) {
	const BodyPart limbs[4] = {BodyPart::LeftArm, BodyPart::RightArm, BodyPart::LeftLeg, BodyPart::RightLeg};
	for (const BodyPart part : limbs) {
		if (character.body_part(part).current > 0.0f) {
			return true;
		}
	}
	return false;
}

AttackProfile attack_using(const Character &character, BasicSkill basic, std::optional<AdvancedSkill> option) {
	return AttackProfile{basic, option, character.resolve_skill(basic, option).rank};
}

DefendProfile block_with(const Character &character, std::optional<AdvancedSkill> option) {
	return DefendProfile{DefendAction::Block, BasicSkill::Blocking, option,
						 character.resolve_skill(BasicSkill::Blocking, option).rank, kPlaceholderBlock};
}

}  // namespace

bool melee_in_range(const Vec3 &self, const Vec3 &target) {
	if (!usable(self.x) || !usable(self.z) || !usable(target.x) || !usable(target.z)) {
		return false;
	}
	const float dx = target.x - self.x;
	const float dz = target.z - self.z;
	// A huge separation overflows the square to infinity, which is out of range
	// rather than in it.
	const float length = std::sqrt(dx * dx + dz * dz);
	return usable(length) && length <= kMeleeRange;
}

std::optional<AttackProfile> attack_profile(const Character &attacker) {
	if (!attacker.alive()) {
		return std::nullopt;
	}
	for (const ItemRef &item : usable_held(attacker)) {
		const std::optional<BasicSkill> basic = weapon_basic(item);
		if (!basic.has_value()) {
			continue;
		}
		return attack_using(attacker, *basic, record_option(item));
	}
	if (!has_surviving_limb(attacker)) {
		return std::nullopt;
	}
	return attack_using(attacker, BasicSkill::UnarmedCombat, attacker.chosen(BasicSkill::UnarmedCombat));
}

std::optional<DefendProfile> defend_profile(const Character &defender) {
	if (!defender.alive()) {
		return std::nullopt;
	}
	const std::vector<ItemRef> held = usable_held(defender);
	for (const ItemRef &item : held) {
		if (is_shield(item)) {
			// The shield's own spec, which Skills::resolve falls back off whenever a
			// different shield was chosen.
			return block_with(defender, item.combat_option);
		}
	}
	// A weapon block is always the plain Blocking job. Carrying the weapon's spec,
	// or the shield spec the character did choose, would read the wrong track.
	for (HandUse use : {HandUse::TwoHanded, HandUse::OneHanded}) {
		for (const ItemRef &item : held) {
			if (item.hand_use == use && weapon_basic(item).has_value()) {
				return block_with(defender, std::nullopt);
			}
		}
	}
	const std::optional<AdvancedSkill> evasion = defender.chosen(BasicSkill::Dodging);
	return DefendProfile{DefendAction::Dodge, BasicSkill::Dodging, evasion,
						 defender.resolve_skill(BasicSkill::Dodging, evasion).rank, 0.0f};
}

float melee_cooldown(unsigned trained, bool exhausted) {
	const float rate = 0.2f + 2.8f * rank_fraction(trained);
	float cooldown = in_band(1.0f / rate);
	if (exhausted) {
		cooldown = in_band(cooldown * 2.0f);
	}
	return cooldown;
}

float melee_damage(unsigned trained, float weapon, float toughness_base, float armour, bool exhausted) {
	float offensive = weapon * rank_fraction(trained);
	if (exhausted) {
		// Half the swing, before the soak: the final damage is not halved twice.
		offensive *= 0.5f;
	}
	return std::max(0.0f, offensive - toughness_base * armour);
}

float melee_damage(const AttackProfile &attack, const Character &defender, float weapon, float armour,
				   bool exhausted) {
	// The sheet taking the blow owns the soak. Whoever is swinging is not asked.
	return melee_damage(attack.trained, weapon, defender.base(Attribute::Toughness), armour, exhausted);
}

bool melee_nullifies(bool defend_ready, float shield, unsigned defend_trained, unsigned attack_trained,
					 float weapon) {
	if (!defend_ready) {
		return false;
	}
	return shield + rank_fraction(defend_trained) > rank_fraction(attack_trained) * weapon;
}

BodyPart random_melee_part(std::mt19937_64 &rng) {
	// One roll, six faces, in enum order: Head, Torso, LeftArm, RightArm,
	// LeftLeg, RightLeg. Count is a sentinel, never a face.
	std::uniform_int_distribution<int> die(0, kMeleePartFaces - 1);
	return static_cast<BodyPart>(die(rng));
}

}  // namespace open
