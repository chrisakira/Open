#include "open/character.hpp"

#include "open/items.hpp"
#include "open/techniques.hpp"

#include <cstddef>
#include <vector>

namespace open {

bool Character::configure_techniques(bool enabled, std::span<const TechniqueStep> sequence) {
	if (!alive()) {
		return false;  // a dead sheet opens no popup and performs no next attack
	}
	// An entry has to name a technique. Anything else - a technique the equipped weapon
	// does not support, one not yet unlocked, one the catalog no longer defines - is a
	// legitimate thing to have configured yesterday, and the selector walks past it without
	// deleting it, so this is the only refusal the shape of a sequence can earn.
	for (const TechniqueStep &entry : sequence) {
		if (entry.id == 0) {
			return false;
		}
	}
	// The enabled switch and the sequence are what the popup owns; `edit_technique_sequence`
	// is the one place the edit rule lives, so the clock the last fight started and the
	// controller the last handover left cannot be disturbed by an edit.
	techniques_.enabled = enabled;
	edit_technique_sequence(
		techniques_, std::vector<TechniqueStep>(sequence.begin(), sequence.end()));
	return true;
}

bool Character::set_player_controlled(bool player_controlled) {
	if (!alive()) {
		return false;
	}
	// Only who chooses changes. `set_technique_controller` keeps the sequence, the position
	// in it, the clocks and the enabled switch, which is the spec's "switching between
	// player and NPC control does not reset cooldowns or erase the player's sequence".
	set_technique_controller(techniques_, player_controlled);
	return true;
}

TechniqueState Character::technique_state() const {
	return techniques_;
}

void Character::apply_ran_technique_clocks(std::vector<double> ran) {
	// Positional, in the map's own id order, which is the order
	// `advance_combat_clocks` handed them over in. A clock that reached ready is dropped
	// rather than remembered at zero: readiness is the absence of a debt, and a popup that
	// reads this state should not have to tell the two apart.
	std::size_t index = 0;
	for (auto &entry : techniques_.cooldowns) {
		entry.second = index < ran.size() ? ran[index] : 0.0;
		++index;
	}
	for (auto it = techniques_.cooldowns.begin(); it != techniques_.cooldowns.end();) {
		if (it->second <= 0.0) {
			it = techniques_.cooldowns.erase(it);
		} else {
			++it;
		}
	}
}

}  // namespace open
