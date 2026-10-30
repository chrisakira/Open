#pragma once

#include "open/move_order.hpp"

#include <cstdint>
#include <random>
#include <span>

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

private:
	// Part draws only. The swing, its clocks and its stamina belong to the sheets.
	std::mt19937_64 rng_;
};

}  // namespace open
