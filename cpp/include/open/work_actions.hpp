#pragma once

#include "open/character.hpp"
#include "open/items.hpp"
#include "open/work.hpp"

#include <optional>
#include <vector>

namespace open {

// What a smith is allowed to make, and what one repair is worth. Recipes, materials,
// benches and queues belong to the workbench slice; these are the two questions that
// slice asks the sheet, kept where the skill rules live so nobody re-derives a band.
//
// The bands are docs/weapons.md "Materials, quality and durability", quoted in tier
// order: basic Smithing 1-34 makes tier 1, 35-67 tier 2, 68-100 tier 3, and a
// matching Weaponsmith 1-63 makes tier 4, 64-127 tier 5, 128-190 tier 6, 191-200
// tier 7. A specialisation lifts its own field only, so an Armoursmith forging a
// blade is using the frozen parent and its tier-3 ceiling.

// The highest weapon quality a trained rank may produce. `matching_weaponsmith` says
// whether the job being crafted is on the Weaponsmith track; a rank its own track
// cannot reach answers nothing rather than borrowing the other scale.
std::optional<unsigned> weapon_quality_cap(unsigned trained, bool matching_weaponsmith);

// Which job a weapon craft or weapon repair is paid on: Weaponsmith when the sheet
// chose it, otherwise basic Smithing, which is also what an Armoursmith falls back to.
SkillJob weapon_craft_job(const Character &character);

// The ceiling for this sheet, reading the rank its own craft job resolves to. 0 means
// the sheet cannot name a tier at all, which no recipe should accept.
unsigned max_weapon_quality(const Character &character);

// Durability one repair restores: the trained rank as a percentage of the item's
// maximum, a rank past 100 still counting as 100%, rounded up to the next whole
// durability point and capped at that maximum. Repair never raises the maximum and
// never changes quality, so it cannot upgrade anything.
unsigned repair_points(unsigned max_durability, unsigned trained);

// --- the workbench queue (Task 16) ---------------------------------------------
//
// The three questions every bench action asks, in order: may this request exist, may
// this start be paid for, and what does finishing produce. The sheet owns the schedule
// and the session owns the frame; what they consult for the rules is here, so neither
// grows a private idea of what a recipe costs.

// Horizontal reach to a bench, inclusive, Y ignored - the reading every 1.5 in this
// project shares.
bool in_bench_reach(const Vec3 &self, const Vec3 &bench);

// The shape a request must have before anyone walks anywhere: a named kind, a recipe in
// this catalog whose kind and bench agree with the observation the caller means, a bench
// that exists where finite numbers do, and target/quality/enchantment fields used
// exactly as the kind requires. No rank, material or position is consulted here: a shape
// refusal says the request could never be legal, which is the only thing a caller may
// learn without paying for it.
bool work_request_ok(const Character &who, const WorkRequest &request, const WorkContext &env);

// The transaction, in the order the rules require it: every start-time requirement first
// (alive, carrying, rank and cap, the target still worth the action, the bench type),
// then the output id reserved, then the materials consumed. All the cost or none of it:
// an allocator with nothing left consumes nothing, and a refusal mints and spends
// nothing. The caller stands where the reach was already checked; `work_begin` does not
// move anybody.
std::optional<WorkStart> work_begin(const Character &who, const WorkRequest &request,
									WorkContext &env);

// Settlement, run once when the elapsed time reaches the duration: the crafted instance
// minted from the reserved id into the backpack, the repaired durability, or the captured
// enchantment attached - then exactly 1 XP on the sampled job, and one Hammerhand mark
// for a craft that delivered, and nothing else. False means the delivery failed after it
// was paid for, which stays a spent failure: a bench minute that ended never buys a
// do-over.
bool work_settle(Character &who, const WorkStart &start, WorkContext &env);

}  // namespace open
