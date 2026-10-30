#pragma once

#include "open/character.hpp"
#include "open/items.hpp"
#include "open/techniques.hpp"
#include "open/weapon_rules.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// A read-only, Godot-independent view of one character's technique state for the technique
// popup: what the sheet has configured, who is driving it, what the weapon in its hand
// offers, and what it still owes. Same arrangement as the other snapshot headers in this
// directory - presentation only, no rule of its own.
//
// The judgement of "may this be performed" is `technique_eligible`, called here and nowhere
// else in the popup's path, so a row the popup greys out and a move the attack pipeline
// refuses are the same fact. `required_rank` and `trained` travel alongside it as data, so
// a popup can say "needs 50, this swing reads 12" without working out the rule - the rule
// also has a title route, which the popup must not reimplement.

namespace open {

struct TechniqueSnapshotRow {
	std::uint64_t id = 0;
	// False when no definition answers the id: an entry the sequence still holds for a
	// technique that does not exist in this catalog.
	bool defined = false;
	// `technique_eligible`: the held weapon supports it and a rank or a title has unlocked
	// it. Says nothing about cooldown, which is `cooldown_remaining`.
	bool available = false;
	// Only meaningful on a sequence row: false is Wait, true is Skip.
	bool skip_on_cooldown = false;
	unsigned required_rank = 0;
	unsigned trained = 0;
	double cut = 1.0;
	double pierce = 1.0;
	double blunt = 1.0;
	double cooldown = 0.0;
	// False means "the weapon's own cost", which is the starter slice; a declared cost
	// replaces it, before the effectiveness adjustment.
	bool has_stamina_cost = false;
	double stamina_cost = 0.0;
	double cooldown_remaining = 0.0;
};

struct TechniqueSnapshot {
	bool enabled = true;
	bool player_controlled = false;
	// A dead sheet is configured by nobody: it has no popup and no next attack, and C++
	// refuses both commands, so the popup disables its controls on this rather than finding
	// out by being refused.
	bool can_configure = false;
	std::size_t index = 0;
	// What the weapon in hand supports, in the weapon's own order, whether or not the sheet
	// has unlocked any of it. Bare hands support nothing, which is a fact and not a gap.
	std::vector<TechniqueSnapshotRow> supported;
	// The configured sequence, in order, one row per entry - including entries the held
	// weapon does not offer, because those are kept rather than deleted.
	std::vector<TechniqueSnapshotRow> sequence;
	// Every clock still running, in id order, whether or not the current weapon supports
	// the technique it belongs to. A clock that has come ready is not in here at all.
	std::vector<TechniqueSnapshotRow> cooldowns;
};

// The snapshot of a sheet that has not been configured at all is the default state, not an
// empty one: enabled, simulation-driven, with nothing to choose from until a weapon says
// otherwise.
inline TechniqueSnapshot technique_snapshot(const Character &character) {
	TechniqueSnapshot snapshot;
	const TechniqueState state = character.technique_state();
	snapshot.enabled = state.enabled;
	snapshot.player_controlled = state.player_controlled;
	snapshot.can_configure = character.alive();
	snapshot.index = state.index;

	const std::shared_ptr<const ItemCatalog> catalog = character.catalog();
	// A dead sheet has no resolved attack, and so nothing is supported or available - which
	// is exactly what the swing itself would conclude.
	const std::optional<WeaponAttackProfile> attack = weapon_attack_profile(character);

	const auto row_for = [&](TechniqueId id) {
		TechniqueSnapshotRow row;
		row.id = id;
		if (attack.has_value()) {
			row.trained = attack->trained;
		}
		if (catalog != nullptr) {
			const TechniqueDefinition *definition = catalog->technique(id);
			if (definition != nullptr) {
				row.defined = true;
				row.required_rank = definition->required_rank;
				row.cut = definition->factors.cut;
				row.pierce = definition->factors.pierce;
				row.blunt = definition->factors.blunt;
				row.cooldown = definition->cooldown;
				row.has_stamina_cost = definition->stamina_cost.has_value();
				if (definition->stamina_cost.has_value()) {
					row.stamina_cost = *definition->stamina_cost;
				}
				if (attack.has_value()) {
					row.available = technique_eligible(character, *attack, *definition);
				}
			}
		}
		row.cooldown_remaining = technique_cooldown_remaining(state, id);
		return row;
	};

	if (attack.has_value()) {
		for (const TechniqueId id : attack->techniques) {
			snapshot.supported.push_back(row_for(id));
		}
	}
	for (const TechniqueStep &entry : state.sequence) {
		TechniqueSnapshotRow row = row_for(entry.id);
		row.skip_on_cooldown = entry.skip_on_cooldown;
		snapshot.sequence.push_back(row);
	}
	for (const auto &owed : state.cooldowns) {
		snapshot.cooldowns.push_back(row_for(owed.first));
	}
	return snapshot;
}

}  // namespace open
