#include "open/combat_session.hpp"

#include "open/character.hpp"
#include "open/combat_rules.hpp"
#include "open/weapon_rules.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace open {
namespace {

bool finite(const Vec3 &position) {
	return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

// The snapshot is all-or-nothing. A repeated id would make two actors of one
// sheet, a null sheet cannot be read at all, and a non-finite position could open
// or close a swing that never happened; one bad entry and the whole roster is
// refused rather than half trusted.
bool usable_roster(std::span<const Combatant> actors) {
	std::vector<std::uint64_t> ids;
	ids.reserve(actors.size());
	for (const Combatant &actor : actors) {
		if (actor.character == nullptr || !finite(actor.position)) {
			return false;
		}
		const std::uint64_t id = actor.character->id();
		if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
			return false;
		}
		ids.push_back(id);
	}
	return true;
}

const Combatant *find_actor(std::span<const Combatant> actors, std::uint64_t id) {
	if (id == 0) {
		return nullptr;
	}
	for (const Combatant &actor : actors) {
		if (actor.character != nullptr && actor.character->id() == id) {
			return &actor;
		}
	}
	return nullptr;
}

// Both legs at zero is a destroyed capability and is not the same thing as the
// raw can_move switch: a standing dummy keeps its order, an actor with nothing to
// stand on cannot close the distance.
bool has_no_legs(const Character &sheet) {
	return sheet.body_part(BodyPart::LeftLeg).current <= 0.0f &&
		   sheet.body_part(BodyPart::RightLeg).current <= 0.0f;
}

// Can this actor still act on the attack order it holds, given this snapshot?
// Death, an absent or dead target, nothing left to swing with, and an actor with
// no legs that is not already in reach all end it. Reach is the weapon's own, and is
// measured from the position the caller observed, never from one kept here.
bool order_is_live(const Combatant &actor, std::span<const Combatant> actors) {
	Character *sheet = actor.character;
	const std::optional<std::uint64_t> target_id = sheet->attack_target();
	if (!target_id.has_value() || !sheet->alive()) {
		return false;
	}
	const Combatant *target = find_actor(actors, *target_id);
	if (target == nullptr || target->character == sheet || !target->character->alive()) {
		return false;
	}
	const std::optional<WeaponAttackProfile> attack = weapon_attack_profile(*sheet);
	if (!attack.has_value()) {
		return false;
	}
	if (!horizontal_in_reach(actor.position, target->position, attack->stats.max_reach) &&
		has_no_legs(*sheet)) {
		return false;
	}
	return true;
}

// Both directions of a fight: an order this sheet can still act on, or a live
// order aimed at it. A defender that never ordered anything is in combat, and a
// bystander nobody has ordered is not.
bool is_engaged(const Combatant &actor, std::span<const Combatant> actors) {
	if (order_is_live(actor, actors)) {
		return true;
	}
	for (const Combatant &other : actors) {
		if (other.character == actor.character) {
			continue;
		}
		const std::optional<std::uint64_t> target_id = other.character->attack_target();
		if (target_id.has_value() && *target_id == actor.character->id() && order_is_live(other, actors)) {
			return true;
		}
	}
	return false;
}

// Reconciliation: an order nobody can honour any more is dropped, so no later
// query, swing or regen is built on it. Nothing else is touched - no new target is
// chosen, and no clock is reset, because clocks answer to time alone.
void drop_dead_orders(std::span<const Combatant> actors) {
	for (const Combatant &actor : actors) {
		if (actor.character->attack_target().has_value() && !order_is_live(actor, actors)) {
			// An accepted attack cleared this actor's move order, so clearing the
			// order back cannot destroy a walk the actor was still doing.
			actor.character->clear_order();
		}
	}
}

}  // namespace

CombatSession::CombatSession(std::uint64_t seed) : rng_(seed) {}

bool CombatSession::issue_attack(std::span<const Combatant> actors, std::uint64_t actor_id,
								 std::uint64_t target_id) {
	if (!usable_roster(actors)) {
		return false;
	}
	const Combatant *actor = find_actor(actors, actor_id);
	const Combatant *target = find_actor(actors, target_id);
	if (actor == nullptr || target == nullptr || target->character == actor->character) {
		return false;
	}
	if (!actor->character->alive() || !target->character->alive()) {
		return false;
	}
	// The order itself stays on the sheet, which is also what refuses zero and
	// self. Faction is never compared here.
	return actor->character->set_attack_target(target_id);
}

bool CombatSession::step(float seconds, std::span<const Combatant> actors) {
	if (!std::isfinite(seconds) || seconds < 0.0f) {
		return false;
	}
	if (!usable_roster(actors)) {
		return false;
	}

	drop_dead_orders(actors);
	if (seconds == 0.0f) {
		return true;
	}

	// Every sheet is advanced exactly once per frame, whoever swung at whom.
	for (const Combatant &actor : actors) {
		actor.character->advance_combat_clocks(seconds);
	}
	// Roster order, one attempt per ordered actor: this is a real-time frame, not
	// a turn queue, and try_melee_swing refuses everything it cannot honour -
	// a cooling clock, a corpse, a target that is now out of reach.
	for (const Combatant &actor : actors) {
		const std::optional<std::uint64_t> target_id = actor.character->attack_target();
		if (!target_id.has_value()) {
			continue;
		}
		const Combatant *target = find_actor(actors, *target_id);
		if (target == nullptr) {
			continue;
		}
		actor.character->try_melee_swing(*target->character, actor.position, target->position, rng_);
	}
	// Whatever those swings killed has to be reconciled before the frame reports
	// its combat state, and a fighter who just lost its target does not get a
	// replacement.
	drop_dead_orders(actors);
	for (const Combatant &actor : actors) {
		if (!actor.character->alive()) {
			continue;
		}
		actor.character->regenerate(seconds, is_engaged(actor, actors));
	}
	return true;
}

bool CombatSession::in_combat(std::uint64_t id, std::span<const Combatant> actors) const {
	if (!usable_roster(actors)) {
		return false;
	}
	const Combatant *self = find_actor(actors, id);
	return self != nullptr && is_engaged(*self, actors);
}

Vec3 CombatSession::velocity_at(std::uint64_t id, std::span<const Combatant> actors) const {
	if (!usable_roster(actors)) {
		return {};
	}
	const Combatant *self = find_actor(actors, id);
	if (self == nullptr) {
		return {};
	}
	const std::optional<std::uint64_t> target_id = self->character->attack_target();
	if (!target_id.has_value()) {
		return self->character->velocity_at(self->position.x, self->position.y, self->position.z);
	}
	const Combatant *target = find_actor(actors, *target_id);
	if (target == nullptr || target->character == self->character || !self->character->alive() ||
		!target->character->alive()) {
		return {};
	}
	return self->character->attack_velocity_at(self->position, target->position);
}

}  // namespace open
