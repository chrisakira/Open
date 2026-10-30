#pragma once

#include "open/items.hpp"
#include "open/weapon_rules.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <random>
#include <vector>

namespace open {

class Character;

// The three locked starter techniques, as production data. Task 15 puts them on the
// starter weapons and Task 14a shows them in a popup; they are not test fixtures, and no
// other technique exists until the human locks one. Each asks for trained rank 1 in the
// weapon's own job, multiplies only the component the spec names, and uses the weapon's
// normal stamina cost.
std::vector<TechniqueDefinition> starter_techniques();

// One entry of a configured sequence. `skip_on_cooldown` is the difference between "not
// yet" and "not this turn": false means wait, so the character throws basic attacks and
// keeps this entry as the next thing it owes; true means skip, so the sequence moves on.
struct TechniqueStep {
	TechniqueId id = 0;
	bool skip_on_cooldown = false;
};

inline bool operator==(const TechniqueStep &left, const TechniqueStep &right) {
	return left.id == right.id && left.skip_on_cooldown == right.skip_on_cooldown;
}

// What one character has configured and owes. The cooldowns live here, on the sheet, and
// are keyed per technique: swapping a weapon does not move one, and no two techniques
// share one. Performing a technique starts its clock even if the blow was turned aside.
struct TechniqueState {
	// The player's switch. Off means the player-controlled character performs basic attacks
	// however the sequence is configured. It means nothing to an NPC, which has no switch.
	bool enabled = true;
	// False is the simulation choosing at random; true is the sequence below.
	bool player_controlled = false;
	std::vector<TechniqueStep> sequence;
	// Which entry comes next. Always inside the sequence, and 0 when there is none.
	std::size_t index = 0;
	std::map<TechniqueId, double> cooldowns;  // seconds left, absent means ready
};

// Seconds of cooldown a state still owes on one technique. Reads only.
double technique_cooldown_remaining(const TechniqueState &state, TechniqueId id);

// Could this sheet perform this technique with this weapon right now: does the weapon
// support it, and is it unlocked by the resolved job rank the swing would use or by a
// title that grants it. Deliberately says nothing about cooldown, sequence or stamina -
// those decide whether it is *this attack's* technique, not whether it is available.
bool technique_eligible(const Character &character, const WeaponAttackProfile &attack,
						const TechniqueDefinition &technique);

// What the next attack would be, and where the sequence would stand after it. An empty
// `technique` is the weapon's basic attack. This is a question and not an action: it
// starts no clock, spends no sequence entry, and draws a random number only when it has
// something to choose between - so a popup can ask it as often as it likes. Committing a
// selection is the attack pipeline's job.
struct TechniqueSelection {
	std::optional<TechniqueId> technique;
	std::optional<std::size_t> proposed_index;
};

TechniqueSelection select_technique(const Character &character, const WeaponAttackProfile &attack,
									const TechniqueState &state, std::mt19937_64 &rng);

// Replacing the sequence is the only edit the popup does, and it restarts the sequence at
// its first entry. Cooldowns, the enabled switch and the controller are left exactly as
// they were: reordering a list is not a refund.
void edit_technique_sequence(TechniqueState &state, std::vector<TechniqueStep> sequence);

// Taking the sheet from the simulation or handing it back. Neither the sequence nor its
// position nor any cooldown moves.
void set_technique_controller(TechniqueState &state, bool player_controlled);

}  // namespace open
