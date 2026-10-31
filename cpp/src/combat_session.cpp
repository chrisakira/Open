#include "open/combat_session.hpp"

#include "open/work_actions.hpp"

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

// One frame's bench pass: every busy sheet gets exactly one reading of this frame -
// engaged or free, bench standing or gone, arrived or still walking, finished or not -
// through the sheet's own advance_work. Zero seconds interrupts but never starts, ticks
// or completes, which is advance_work's rule, not a loop condition here. The pass runs
// after the swings and the dead-order reconciliation, so "combat arrives first" is the
// step's order of operations rather than a rule anybody has to remember.
}  // namespace

CombatSession::CombatSession(std::uint64_t seed) : rng_(seed) {}

void CombatSession::advance_all_work(std::span<const Combatant> actors, WorkContext &work,
									 float seconds) {
	for (const Combatant &actor : actors) {
		if (!actor.character->work_status().busy()) {
			continue;
		}
		const bool engaged = is_engaged(actor, actors);
		actor.character->advance_work(seconds, actor.position, work, engaged);
	}
}

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
	if (!actor->character->set_attack_target(target_id)) {
		return false;
	}
	// Combat beginning is true of both ends the moment the order is accepted. The
	// attacker's own sheet door interrupts its bench; the target's interruption lives
	// here, at issue, because the docs count "an incoming attack order" among the
	// facts that clear a queue and interrupt a job: queued clears free, active stays
	// paid, and an attacker that changes its mind before any frame has run does not
	// un-begin a combat that began. The frame's engagement reading stays as the
	// in-frame backstop for combat that arrives without any new order at all - an
	// order that only became live as positions moved, for instance.
	target->character->interrupt_work(open::WorkFailure::Combat);
	return true;
}

bool CombatSession::step(float seconds, std::span<const Combatant> actors) {
	if (!std::isfinite(seconds) || seconds < 0.0f) {
		return false;
	}
	if (!usable_roster(actors)) {
		return false;
	}
	// Work without a work environment is the silent-ticking bug with a frame around it:
	// while any sheet is busy at a bench, this door refuses the whole step rather than
	// pretending the frame held no schedule. Terminal results are not live work; the old
	// frame runs again once nothing is walking or paid.
	for (const Combatant &actor : actors) {
		if (actor.character->work_status().busy()) {
			return false;
		}
	}
	return step_core(seconds, actors, nullptr);
}

bool CombatSession::step(float seconds, std::span<const Combatant> actors, WorkContext &work) {
	if (!std::isfinite(seconds) || seconds < 0.0f) {
		return false;
	}
	if (!usable_roster(actors)) {
		return false;
	}
	return step_core(seconds, actors, &work);
}

bool CombatSession::step_core(float seconds, std::span<const Combatant> actors,
							  WorkContext *work) {
	drop_dead_orders(actors);
	if (seconds == 0.0f) {
		// The reconciliation frame still notices a bench that is gone or a fight that
		// started - interrupting is reconciliation, not progress - but it never starts,
		// ticks or completes a job.
		if (work != nullptr) {
			advance_all_work(actors, *work, false);
		}
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
		const std::optional<SwingResult> swung = actor.character->try_melee_swing(
			*target->character, actor.position, target->position, rng_);
		// Performed, so reported. A refused attempt returns nothing and queues nothing, which
		// is the difference between a swing that missed and a swing nobody was ordered to take.
		if (swung.has_value()) {
			if (events_.size() == kMaxPendingEvents) {
				events_.erase(events_.begin());
			}
			events_.push_back(CombatEvent{actor.character->id(), target->character->id(), *swung});
			// Auto-retaliation: the performed attempt is the provocation, whatever its
			// outcome - dodged, blocked, armour-zero or a plain hit all count, and a
			// refused one provokes nobody because nothing was done to provoke with. The
			// order goes in only when the victim held no attack order at all: being hit
			// from the side never repoints a fight already chosen, and an issued order -
			// the player's click among them - always outranks this one. Death refuses the
			// order inside set_attack_target, so a killing blow provokes nobody, and a
			// walk or bench the retaliation replaces is cleared by that same setter.
			// Roster order is untouched: a victim asked later returns the blow inside
			// this step, a victim asked earlier does it in the next.
			if (!target->character->attack_target().has_value()) {
				target->character->set_attack_target(actor.character->id());
			}
		}
	}
	// Whatever those swings killed has to be reconciled before the frame reports
	// its combat state, and a fighter who just lost its target does not get a
	// replacement.
	drop_dead_orders(actors);
	// The bench gets this frame after the fight has had all of it: engagement and death are
	// already on the sheets, so work that "finishes" now knows combat arrived first, which
	// is the fifteenth-second rule stated as an order of operations.
	if (work != nullptr) {
		advance_all_work(actors, *work, seconds);
	}
	for (const Combatant &actor : actors) {
		if (!actor.character->alive()) {
			continue;
		}
		actor.character->regenerate(seconds, is_engaged(actor, actors));
	}
	return true;
}

bool CombatSession::approach_workbench(std::span<const Combatant> actors, std::uint64_t actor_id,
									   WorkbenchId bench, WorkContext &work) {
	if (!usable_roster(actors)) {
		return false;
	}
	const Combatant *actor = find_actor(actors, actor_id);
	if (actor == nullptr || !actor->character->alive()) {
		return false;
	}
	const WorkbenchObservation *observed = work.bench(bench);
	if (observed == nullptr || !std::isfinite(observed->position.x)
		|| !std::isfinite(observed->position.z)) {
		return false;
	}
	if (in_bench_reach(actor->position, observed->position)) {
		return true;  // already standing at it: the click opens, and orders nothing
	}
	if (!actor->character->can_walk()) {
		return false;  // no legs, no bench
	}
	return actor->character->set_destination(observed->position.x, observed->position.y,
											 observed->position.z);
}

bool CombatSession::request_work(std::span<const Combatant> actors, std::uint64_t actor_id,
								 const WorkRequest &request, WorkContext &work) {
	if (!usable_roster(actors)) {
		return false;
	}
	const Combatant *actor = find_actor(actors, actor_id);
	if (actor == nullptr || !actor->character->alive()) {
		return false;
	}
	// One job at a time, and a terminal result is kept only until this call's next accepted
	// job, so a busy sheet is refused before anything is read.
	if (actor->character->work_status().busy()) {
		return false;
	}
	if (!work_request_ok(*actor->character, request, work)) {
		return false;
	}
	// A bench is not a refuge: everything about smithing says outside combat, and
	// `in_combat` is the session's own word for inside it - pursuit included, both ways.
	if (is_engaged(*actor, actors)) {
		return false;
	}
	const WorkbenchObservation *observed = work.bench(request.bench);
	if (in_bench_reach(actor->position, observed->position)) {
		// The immediate start is the strict door: materials, cap and target are checked now,
		// and a refusal leaves no job, no reason, no spent anything.
		return actor->character->begin_work(request, actor->position, work);
	}
	if (!actor->character->can_walk()) {
		return false;  // out of reach and going nowhere: no magic bench
	}
	// Command order, deliberately: walk first, queue second. set_destination interrupts any
	// prior job (there is none - the busy check above), and queueing after it means the
	// approach this request itself issues cannot be the interruption that clears the queue.
	if (!actor->character->set_destination(observed->position.x, observed->position.y,
										   observed->position.z)) {
		return false;
	}
	return actor->character->queue_work(request);
}

bool CombatSession::cancel_work(std::span<const Combatant> actors, std::uint64_t actor_id,
								WorkContext &work) {
	(void)work;  // the purse is never touched: nothing returns, that is what "paid" means
	if (!usable_roster(actors)) {
		return false;
	}
	const Combatant *actor = find_actor(actors, actor_id);
	if (actor == nullptr) {
		return false;
	}
	return actor->character->interrupt_work(WorkFailure::Cancelled);
}

bool CombatSession::issue_move(std::span<const Combatant> actors, std::uint64_t actor_id,
							   const Vec3 &position, WorkContext &work) {
	(void)work;  // the interruption lives on the sheet's own setter, so neither door dodges it
	if (!usable_roster(actors)) {
		return false;
	}
	const Combatant *actor = find_actor(actors, actor_id);
	if (actor == nullptr) {
		return false;
	}
	return actor->character->set_destination(position.x, position.y, position.z);
}

bool CombatSession::in_combat(std::uint64_t id, std::span<const Combatant> actors) const {
	if (!usable_roster(actors)) {
		return false;
	}
	const Combatant *self = find_actor(actors, id);
	return self != nullptr && is_engaged(*self, actors);
}

std::vector<CombatEvent> CombatSession::take_swing_events() {
	// Moved out and left empty: the queue's whole job is to say "these happened", once.
	std::vector<CombatEvent> drained;
	drained.swap(events_);
	return drained;
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
