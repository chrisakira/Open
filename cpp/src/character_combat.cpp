#include "open/character.hpp"

#include "open/combat_rules.hpp"
#include "open/weapon_rules.hpp"

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
// Any component left with something in it. Decides whether a blow has a destination,
// which is what gates the body-part draw.
bool has_any(const DamageComponents &value) {
	return value.cut > 0.0 || value.pierce > 0.0 || value.blunt > 0.0;
}

// Horizontal distance, measured the same way reach is. Doubles, so an absurd
// separation cannot look like a plausible float distance.
bool measured_separation(const Vec3 &self, const Vec3 &target, double &out) {
	if (!std::isfinite(self.x) || !std::isfinite(self.z) || !std::isfinite(target.x) ||
		!std::isfinite(target.z)) {
		return false;
	}
	const double dx = static_cast<double>(target.x) - static_cast<double>(self.x);
	const double dz = static_cast<double>(target.z) - static_cast<double>(self.z);
	const double distance = std::sqrt(dx * dx + dz * dz);
	if (!std::isfinite(distance)) {
		return false;
	}
	out = distance;
	return true;
}

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
	// Everything that can refuse does so here, before anything is spent, drawn or
	// damaged: an order this swing cannot honour, a dead sheet on either side, a swing
	// still cooling, a stunned attacker, a bad or out-of-reach observation, a weapon the
	// actor can no longer use, or arithmetic the rules cannot price.
	if (!attack_target_.has_value() || *attack_target_ != defender.id_) {
		return std::nullopt;
	}
	if (!alive() || !defender.alive()) {
		return std::nullopt;
	}
	if (attack_cooldown_ > 0.0f || is_stunned()) {
		return std::nullopt;  // a stun is not a moment to swing in
	}
	const std::optional<WeaponAttackProfile> attack = weapon_attack_profile(*this);
	if (!attack.has_value()) {
		return std::nullopt;
	}
	if (!horizontal_in_reach(self_position, target_position, attack->stats.max_reach)) {
		return std::nullopt;
	}
	double separation = 0.0;
	if (!measured_separation(self_position, target_position, separation)) {
		return std::nullopt;
	}

	// The defender answers with what they are holding now, unless they are stunned: a
	// stunned sheet defends with nothing, and its armour is worth nothing either.
	const std::optional<WeaponDefendProfile> defence =
		defender.is_stunned() ? std::nullopt : weapon_defend_profile(defender);
	const bool defence_ready = defence.has_value() && defender.defend_cooldown_ <= 0.0f;

	// Every number the exchange will use, settled and checked before the first point of
	// stamina moves, so a refusal cannot leave a bill half paid.
	const double attack_cost = weapon_attack_cost(attack->stats.stamina_cost, attack->effectiveness);
	if (!std::isfinite(attack_cost) || attack_cost < 0.0) {
		return std::nullopt;
	}
	// All-or-nothing: what cannot be paid for in full is not paid for at all, and the
	// swing is performed exhausted instead.
	const bool paid = attack_cost <= static_cast<double>(pool(PoolKind::Stamina).current);
	const bool exhausted = !paid;
	const double min_factor = minimum_range_factor(separation, attack->stats.min_reach);
	const DamageComponents basic_technique{1.0, 1.0, 1.0};  // Task 13 selects a real one
	const std::optional<DamageComponents> offense =
		scaled_offense(*attack, basic_technique, exhausted, min_factor);
	if (!offense.has_value()) {
		return std::nullopt;
	}

	// Mitigation is the blocking item's own percentage, discounted for a worn-out item
	// and for a defence that could not pay for itself. A dodge has no item, so only the
	// exhaustion factor can touch it: all of it ready, half of it unpaid.
	bool defence_paid = false;
	double mitigation = 0.0;
	if (defence_ready) {
		defence_paid = kDefenseStaminaCost <= static_cast<double>(defender.pool(PoolKind::Stamina).current);
		const bool worn_out = defence->item.has_value() && defence->durability == 0u;
		mitigation = defence->mitigation * (worn_out ? kZeroDurabilityFactor : 1.0) *
					 (defence_paid ? 1.0 : 0.5);
	}

	// A body part is drawn only once the attack still has something in it after the
	// defence: nothing prevented, nothing aimed at, and no random number consumed for a
	// blow that had nowhere to land.
	const bool anything_arrives = has_any(*offense) && mitigation < kPercentMax;
	std::optional<BodyPart> part;
	DamageComponents armour{0.0, 0.0, 0.0};
	if (anything_arrives) {
		part = random_melee_part(rng);
		armour = armour_for_part(defender, *part);  // stunned means no defence and no armour
	}

	const std::optional<DamageResolution> resolution =
		resolve_typed_damage(*offense, mitigation, armour, attack->stats.penetration,
							 static_cast<double>(defender.base(Attribute::Toughness)));
	if (!resolution.has_value()) {
		return std::nullopt;
	}
	const bool after_defence_is_empty = !has_any(resolution->after_defense);

	// ---------------------------------------------------------------- effects, once

	// The swing pays its own price, exactly once, in full or not at all.
	if (paid) {
		spend_stamina(static_cast<float>(attack_cost));
	}
	// A ready defence is spent on the attempt win or lose, and its clock starts whether
	// or not it prevented anything - that is what makes a worthless guard a real action.
	if (defence_ready) {
		if (defence_paid) {
			defender.spend_stamina(static_cast<float>(kDefenseStaminaCost));
		}
		// The defence clock is the defender's own: it is the defence going unpaid that
		// tires it, never the attacker's empty bar, and there is no speed or
		// requirement term for a block or a dodge to carry.
		defender.defend_cooldown_ = weapon_cooldown(defence->trained, 1.0, 1.0, !defence_paid);
	}

	if (resolution->total > 0.0 && part.has_value()) {
		// One amount, one part, applied through the sheet so main HP and that part move
		// together and overkill clamps.
		defender.damage(*part, static_cast<float>(resolution->total));
		// Positive damage on a living target trains the weapon used, lethal included,
		// and there is no second credit for a technique (there is none yet).
		record_use(attack->job.basic, attack->job.option, true);
		if (!defender.alive()) {
			// A kill is credited once, to the title that claims this job, and the corpse
			// keeps no meter, no stun and no intent.
			const std::optional<Title> title = kill_title(attack->job.basic);
			if (title.has_value()) {
				record_title_progress(*title, true);
			}
			defender.clear_stagger();
		} else {
			// Stagger is fed by the final blunt and nothing else, overkill included, and
			// a stunned or killed sheet is not fed at all.
			add_stagger(defender.stagger_, resolution->final.blunt, defender.stagger_threshold());
		}
	}

	// What the defence turned aside pays the defence, even when armour would have
	// absorbed the rest, and a block wears its item for the same prevention.
	if (defence_ready && resolution->prevented > 0.0) {
		defender.record_use(defence->job.basic, defence->job.option, true);
		if (defence->action == DefendAction::Block) {
			defender.record_title_progress(Title::Wall, true);
			if (defence->item.has_value()) {
				defender.apply_wear(*defence->item, kWeaponBlockWear);
			}
		}
	}

	// A performed attempt wears its weapon, whatever it achieved: swung, blocked,
	// soaked by armour, or wholly unfit to swing well.
	if (attack->item.has_value()) {
		apply_wear(*attack->item, kWeaponAttackWear);
	}

	// The clock runs from the rank and effectiveness the swing was resolved with, and a
	// swing that was not paid for is slower as well as weaker.
	attack_cooldown_ =
		weapon_cooldown(attack->trained, attack->stats.speed, attack->effectiveness, exhausted);

	SwingResult result{};
	result.damage = static_cast<float>(resolution->total);
	// Turned aside means the defence, not the armour, took all of a blow that had
	// something in it. Never an XP authority: each award above reads its own number.
	result.nullified = defence_ready && has_any(*offense) && after_defence_is_empty;
	result.part = part;  // kept even when armour made the total zero
	result.prevented = resolution->prevented;
	result.final = resolution->final;
	result.attack_item = attack->item;
	result.defense_item = defence_ready ? defence->item : std::nullopt;
	return result;
}

void Character::clear_stagger() {
	stagger_ = StaggerState{};
}

}  // namespace open
