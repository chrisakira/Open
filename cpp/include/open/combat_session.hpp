#pragma once

#include "open/combat_rules.hpp"
#include "open/move_order.hpp"
#include "open/work.hpp"

#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace open {

class Character;

// One actor in a snapshot the caller owns: the sheet plus where that sheet was
// observed standing. The coordinator keeps neither. It resolves ids, never copies
// a sheet, and never moves anything itself: positions are observations fed back
// in, movement and collisions belong to the caller.
struct Combatant {
	Character *character;
	Vec3 position;
};

// One swing this session actually performed, reported afterwards for presentation to react
// to: which two sheets it happened between and the whole result C++ already applied - damage,
// the body part it was charged to, what the defence turned aside, and the technique it
// performed, if any.
//
// It is a report and not a request. The blow is in the sheets already, so a listener that
// "applies" an event is double-counting the simulation, and one that invents an event it did
// not receive is animating a swing that never happened. That is also why a refused attempt is
// not here at all: an order on a target out of reach, a cooling clock, a corpse and a stunned
// sheet each produced no swing, no stamina, no wear and no event.
struct CombatEvent {
	std::uint64_t actor = 0;
	std::uint64_t target = 0;
	SwingResult swing;
};

// Coordinates the parts a single Character cannot judge on its own: what an
// attack id refers to right now, whether an order can still be carried out, who
// is in the fight, and one frame of combat for a whole set of actors. Every order,
// clock, pool and trained track stays on its own sheet; this holds only the rng
// that decides which body part a swing takes.
class CombatSession {
public:
	// The seed makes a session's part draws replayable. Seeded simulation tests
	// live in C++; a bridge may seed this from C++ randomness instead.
	explicit CombatSession(std::uint64_t seed);

	// Orders `actor_id` to attack `target_id`, both resolved against this
	// snapshot. Refused, leaving the actor's existing order untouched, when the
	// snapshot is invalid, either id is absent or zero, the ids are the same, or
	// either sheet is dead. Faction is not a filter: a Friendly may attack a
	// Friendly.
	bool issue_attack(std::span<const Combatant> actors, std::uint64_t actor_id, std::uint64_t target_id);

	// One frame of the fight. Negative or non-finite time is refused before
	// anything is read or written. A valid frame first drops orders that this
	// snapshot cannot honour; with zero seconds that is all it does, so a
	// reconciliation never opens a swing, moves a clock or regrows a pool. A
	// positive frame then advances every sheet's clocks once, attempts at most one
	// swing per ordered actor in roster order, reconciles again for any death that
	// caused, and regenerates every living sheet once with the combat state the
	// frame produced. A long frame is not several turns: a swing that did not fit
	// is simply missed.
	bool step(float seconds, std::span<const Combatant> actors);

	// True while this id is in a fight it can still act on: an outgoing order it
	// could carry out, or a live order pointed at it by someone else. Pursuit
	// counts; melee range is not required. A defended character is in combat
	// without having ordered anything itself, which is the flag the sheet's
	// regenerate(..., in_combat) expects.
	bool in_combat(std::uint64_t id, std::span<const Combatant> actors) const;

	// The horizontal velocity C++ wants for this id: pursuit toward the target's
	// current snapshot position while an attack order is live, otherwise the
	// ordinary move order's velocity. An order on a dead or absent target yields
	// zero rather than chasing a position nobody supplied; the next step reconciles
	// it.
	Vec3 velocity_at(std::uint64_t id, std::span<const Combatant> actors) const;

	// Every swing performed since the last call, oldest first, and the queue emptied. Reading
	// is the only way to clear it, which is what makes "one event, one animation" true for a
	// listener that polls every frame and for one that polls twice: the second read is empty
	// and cannot replay the blow.
	//
	// The queue is bounded: a listener that never reads cannot grow the simulation's memory, so
	// the oldest events are dropped once `kMaxPendingEvents` are waiting. Losing a swing's worth
	// of presentation is cheap; losing a swing is not, and nothing here loses one.
	std::vector<CombatEvent> take_swing_events();

	// How many performed swings can wait to be read before the oldest is dropped.
	static constexpr std::size_t kMaxPendingEvents = 64u;

	// --- workbenches (Task 16) ----------------------------------------------------
	//
	// The session's share of bench work is the part a single sheet cannot judge: who is in
	// combat (which interrupts), whether the walk toward a bench has finished, and the one
	// place a frame's combat and a frame's settlement may collide. Materials, ranks and
	// deliveries belong to the sheet and to work_actions; the session only ever calls the
	// sheet's own work engine with this frame's readings.
	//
	// Every one of these takes the caller's current `WorkContext`: the catalog, the id mint,
	// the material purse, and this frame's bench observations. Observations are read and
	// never stored, exactly like Combatant positions, so a bench that vanished between calls
	// is discovered here rather than trusted from last time.

	// The click on a bench with no action chosen: in reach, it succeeds and orders nothing
	// (the popup it opens is the caller's business); out of reach it issues the approach
	// walk. It cannot start, queue or pay for work, and an immobile actor is not promised a
	// bench it cannot reach.
	bool approach_workbench(std::span<const Combatant> actors, std::uint64_t actor_id,
							WorkbenchId bench, WorkContext &work);

	// Ask for a job. Refused, touching nothing: bad shape (see work_request_ok), an actor
	// who is absent, dead or already working, smithing done in combat, an immobile actor
	// facing a distant bench, or an in-reach start whose materials and ranks cannot cover
	// it. Accepted in reach as an active job (paid here, now); accepted out of reach as a
	// queued job with an issued approach and nothing paid - the arrival check runs again on
	// arrival, against whoever the smith and the purse have become by then.
	bool request_work(std::span<const Combatant> actors, std::uint64_t actor_id,
					  const WorkRequest &request, WorkContext &work);

	// Put the job down. A queue clears free; an active job stays paid. Cancelling finished
	// or unstarted work answers false and keeps whatever result was being kept.
	bool cancel_work(std::span<const Combatant> actors, std::uint64_t actor_id, WorkContext &work);

	// The session-routed move order. Like the sheet's own set_destination it interrupts
	// whatever job was on the bench - the interruption lives on the sheet so neither door
	// can be walked around - and unlike approach_workbench it goes where the caller says.
	bool issue_move(std::span<const Combatant> actors, std::uint64_t actor_id, const Vec3 &position,
					WorkContext &work);

	// A frame with a work environment: after the swings have been thrown and the dead
	// reconciled, every busy sheet is advanced once - engagement checked before settlement,
	// so combat arriving on the fifteenth second wins that second; queued work starts at
	// zero only on arrival; a stun freezes the bench like every other clock. The two-
	// argument step below refuses to tick at all while any sheet is busy, which is what
	// makes "no silent ticking without the context" true.
	bool step(float seconds, std::span<const Combatant> actors, WorkContext &work);

private:
	// The frame, shared by both step doors. `work == nullptr` is the no-environment frame;
	// callers other than `step` never reach it.
	bool step_core(float seconds, std::span<const Combatant> actors, WorkContext *work);

	// One frame's bench pass over the roster: every busy sheet gets exactly one reading of
	// this frame through the sheet's own (private, session-friended) advance_work.
	static void advance_all_work(std::span<const Combatant> actors, WorkContext &work,
								 float seconds);

	// Part draws only. The swing, its clocks and its stamina belong to the sheets.
	std::mt19937_64 rng_;
	// Presentation's queue, drained by `take_swing_events`. Not part of the fight's state:
	// nothing in a step reads it, and a session that reports nothing still fights the same.
	std::vector<CombatEvent> events_;
};

}  // namespace open
