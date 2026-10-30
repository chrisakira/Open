#include "open/techniques.hpp"

#include "open/character.hpp"
#include "open/items.hpp"
#include "open/weapon_rules.hpp"

#include <algorithm>
#include <cstddef>
#include <random>

namespace open {

std::vector<TechniqueDefinition> starter_techniques() {
	// docs/weapons.md, Starter techniques. The multipliers are the only thing these three
	// do, and each touches exactly one component: 1 elsewhere is "says nothing about it",
	// which is why an absent effect is a factor of 1 and never a 0.
	std::vector<TechniqueDefinition> made;

	TechniqueDefinition slash;
	slash.id = 1;
	slash.required_rank = 1;
	slash.factors = DamageComponents{1.25, 1.0, 1.0};
	slash.cooldown = 6.0;
	made.push_back(slash);

	TechniqueDefinition thrust;
	thrust.id = 2;
	thrust.required_rank = 1;
	thrust.factors = DamageComponents{1.0, 1.25, 1.0};
	thrust.cooldown = 8.0;
	made.push_back(thrust);

	TechniqueDefinition heavy;
	heavy.id = 3;
	heavy.required_rank = 1;
	heavy.factors = DamageComponents{1.0, 1.0, 1.25};
	heavy.cooldown = 10.0;
	made.push_back(heavy);
	return made;
}

double technique_cooldown_remaining(const TechniqueState &state, TechniqueId id) {
	const auto found = state.cooldowns.find(id);
	return found == state.cooldowns.end() ? 0.0 : found->second;
}

bool technique_eligible(const Character &character, const WeaponAttackProfile &attack,
						const TechniqueDefinition &technique) {
	if (technique.id == 0) {
		return false;
	}
	// Support always comes from the weapon, whichever route unlocked the technique. The
	// unarmed fallback carries an empty list, which is how "an unarmed attack has no
	// techniques" is enforced rather than remembered: there is nothing for it to support.
	const std::vector<TechniqueId> &supports = attack.techniques;
	if (std::find(supports.begin(), supports.end(), technique.id) == supports.end()) {
		return false;
	}
	// The rank that matters is the one this swing resolves to - the chosen matching
	// specialisation if the sheet has one, the frozen parent otherwise - and never an
	// effective skill, and never a rank earned wielding something else.
	if (attack.trained >= technique.required_rank) {
		return true;
	}
	// A title on the grant list unlocks it outright. Titles are additive: they never take
	// an unlock away.
	for (const Title title : technique.granting_titles) {
		if (character.has_title(title)) {
			return true;
		}
	}
	return false;
}

TechniqueSelection select_technique(const Character &character, const WeaponAttackProfile &attack,
									const TechniqueState &state, std::mt19937_64 &rng) {
	const std::shared_ptr<const ItemCatalog> catalog = character.catalog();
	if (catalog == nullptr) {
		return TechniqueSelection{};  // nothing to resolve technique ids against
	}
	if (state.player_controlled && !state.enabled) {
		// The player's switch. The sequence stays configured and untouched, because
		// switching techniques off is not the same as deleting the list.
		return TechniqueSelection{};
	}

	const auto ready = [&](TechniqueId id) {
		const TechniqueDefinition *definition = catalog->technique(id);
		if (definition == nullptr) {
			return false;
		}
		return technique_eligible(character, attack, *definition) &&
			   technique_cooldown_remaining(state, id) <= 0.0;
	};

	if (state.player_controlled && !state.sequence.empty()) {
		// One full pass per attack, from where the sequence stands. An entry whose technique
		// the weapon does not support, that the sheet has not unlocked, or that the catalog
		// does not know is walked past rather than removed: it may become usable again, and
		// the player's list is not the simulation's to rewrite.
		const std::size_t size = state.sequence.size();
		const std::size_t start = state.index < size ? state.index : 0u;
		for (std::size_t offset = 0; offset < size; ++offset) {
			const std::size_t position = (start + offset) % size;
			const TechniqueStep &entry = state.sequence[position];
			const TechniqueDefinition *definition = catalog->technique(entry.id);
			if (definition == nullptr || !technique_eligible(character, attack, *definition)) {
				continue;
			}
			if (technique_cooldown_remaining(state, entry.id) > 0.0) {
				if (entry.skip_on_cooldown) {
					continue;
				}
				// Waiting: the basic attack is this character's, and so is the entry it
				// still owes. Nothing advances.
				return TechniqueSelection{std::nullopt, position};
			}
			// The entry is only consumed by the attack that performs it, which is why the
			// next position is proposed rather than taken.
			return TechniqueSelection{entry.id, (position + 1) % size};
		}
		// Everything on the list was skipped or waited. A basic attack, with the sequence
		// exactly where it was.
		return TechniqueSelection{std::nullopt, start};
	}

	// An NPC, and a player who configured nothing: choose among the ready, applicable,
	// unlocked techniques the weapon supports. Sorted by id so the draw indexes the same
	// list on every run and in every build, whatever order the catalog happened to store.
	std::vector<TechniqueId> candidates = attack.techniques;
	std::sort(candidates.begin(), candidates.end());
	candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
	candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
									[&](TechniqueId id) { return !ready(id); }),
					 candidates.end());
	if (candidates.empty()) {
		// No luck is spent on a fight that had nothing to choose. A preview, or a window in
		// which every technique is cooling, cannot reorder the fight that follows it.
		return TechniqueSelection{};
	}
	std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
	return TechniqueSelection{candidates[pick(rng)], std::nullopt};
}

void edit_technique_sequence(TechniqueState &state, std::vector<TechniqueStep> sequence) {
	state.sequence = std::move(sequence);
	// "Editing a sequence restarts it at its first entry" - and only the entry. Cooldowns
	// are what the last fight cost, and the enabled switch and the controller are the
	// player's, not the list's.
	state.index = 0;
}

void set_technique_controller(TechniqueState &state, bool player_controlled) {
	// Swapping who is driving changes the choice rule and nothing else: no clock is
	// refunded, no configuration is erased, and the position in the sequence is kept
	// because the next attack is the same character's next attack.
	state.player_controlled = player_controlled;
}

}  // namespace open
