#include "open/character.hpp"

#include "open/combat_rules.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace open {
namespace {

// The title table owns the weapon-class mapping, so combat asks it rather than
// keeping a second copy of it: a kill is credited to the title whose rule names
// the basic that swing resolved to. Unarmed Combat and Polearms name no title,
// so a swing that did not kill - or killed with a job no title claims - credits
// nothing, and no title is invented here.
std::optional<Title> kill_title(BasicSkill basic) {
	for (int index = 0; index < static_cast<int>(Title::Count); ++index) {
		const Title title = static_cast<Title>(index);
		const std::optional<BasicSkill> &owner = title_rule(title).skill;
		if (owner.has_value() && *owner == basic) {
			return title;
		}
	}
	return std::nullopt;
}

}  // namespace

double Character::stagger_buildup() const {
	return stagger_.buildup;
}

double Character::stun_remaining() const {
	return stagger_.stun_remaining;
}

bool Character::is_stunned() const {
	return stagger_.stun_remaining > 0.0;
}

// Stagger threshold = 10 x effective Toughness. Asked for whenever a hit is resolved
// rather than stored, so a level-up or a blessing takes effect on the next exchange
// without ever re-checking a meter that has already been paid.
double Character::stagger_threshold() const {
	return kStaggerThresholdCoefficient * static_cast<double>(attribute(Attribute::Toughness));
}

bool Character::advance_combat_clocks(float seconds) {
	if (!std::isfinite(seconds) || seconds < 0.0f) {
		return false;
	}

	// One policy for both clocks and the meter: time spent stunned is spent frozen, and
	// only the unstunned remainder runs a clock or decays the meter. A clock counts
	// down toward ready and never past it, so a long frame cannot bank time for the
	// next one.
	ClockState clocks;
	clocks.attack = static_cast<double>(attack_cooldown_);
	clocks.defense = static_cast<double>(defend_cooldown_);
	clocks.technique = 0.0;  // the technique clock joins this in Task 13
	if (!advance_frozen_time(clocks, stagger_, static_cast<double>(seconds), stagger_threshold())) {
		return false;
	}
	attack_cooldown_ = static_cast<float>(clocks.attack);
	defend_cooldown_ = static_cast<float>(clocks.defense);
	return true;
}

float Character::attack_cooldown_remaining() const {
	return attack_cooldown_;
}

float Character::defend_cooldown_remaining() const {
	return defend_cooldown_;
}

std::optional<SwingResult> Character::try_melee_swing(Character &defender, const Vec3 &self_position,
													 const Vec3 &target_position, std::mt19937_64 &rng) {
	// Every refusal happens before anything is spent: an order this swing cannot
	// honour, a dead sheet on either side, a bad or out-of-reach observation, a
	// weapon the actor can no longer use, or a clock still running.
	if (!attack_target_.has_value() || *attack_target_ != defender.id_) {
		return std::nullopt;
	}
	if (!alive() || !defender.alive()) {
		return std::nullopt;
	}
	if (!melee_in_range(self_position, target_position)) {
		return std::nullopt;
	}
	if (attack_cooldown_ > 0.0f) {
		return std::nullopt;
	}
	const std::optional<AttackProfile> attack = attack_profile(*this);
	if (!attack.has_value()) {
		return std::nullopt;
	}

	// An attempt always pays what it can. Below the cost it swings tired instead:
	// half the offensive term and a doubled clock, for this swing only.
	const bool paid = spend(ResourceUse::MeleeSwing);
	const bool exhausted = !paid;

	// The defence is read now, and a ready one is spent on the attempt win or
	// lose. Both ranks here are the ones the swing was resolved with, before any
	// XP from this same swing is awarded.
	const std::optional<DefendProfile> defence = defend_profile(defender);
	const bool defend_ready = defence.has_value() && defender.defend_cooldown_ <= 0.0f;
	if (defend_ready) {
		defender.defend_cooldown_ = melee_cooldown(defence->trained, false);
	}
	const bool nullified = defend_ready && melee_nullifies(true, defence->shield, defence->trained,
														   attack->trained, kPlaceholderWeapon);

	SwingResult result{0.0f, nullified, std::nullopt};
	if (nullified) {
		// A turned-aside blow trains the defence that turned it, and only Wall for
		// a block: a dodge is not a block.
		defender.record_use(defence->basic, defence->option, true);
		if (defence->action == DefendAction::Block) {
			defender.record_title_progress(Title::Wall, true);
		}
	} else {
		// The soak belongs to the sheet being hit: the character-aware form names
		// the defender, so the swing cannot be soaked by the attacker's Toughness.
		const float amount = melee_damage(*attack, defender, kPlaceholderWeapon, kPlaceholderArmour,
										  exhausted);
		// Only a hit that still does damage names a part, and it is applied once,
		// through the sheet: main HP and that part move together, overkill clamps.
		if (amount > 0.0f) {
			const BodyPart part = random_melee_part(rng);
			const bool victim_was_alive = defender.alive();
			if (defender.damage(part, amount)) {
				result.damage = amount;
				result.part = part;
				record_use(attack->basic, attack->option, true);
				if (victim_was_alive && !defender.alive()) {
					const std::optional<Title> title = kill_title(attack->basic);
					if (title.has_value()) {
						record_title_progress(*title, true);
					}
					// No new target is taken: the caller reconciles the order.
				}
			}
		}
	}

	// A swing that lands, slides off or is turned aside still starts the clock,
	// from the rank it was resolved with and, if unpaid, doubled once.
	attack_cooldown_ = melee_cooldown(attack->trained, exhausted);
	return result;
}

}  // namespace open
