#pragma once

#include "open/character.hpp"
#include "open/items.hpp"

#include <optional>

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

}  // namespace open
