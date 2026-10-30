#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <vector>

#include "character_bridge.hpp"
#include "open/combat_session.hpp"
#include "open/item_instance.hpp"

// Godot-facing handle on one open::CombatSession. It keeps the OpenCharacter
// objects the scene registered alive, remembers where each was last standing, and
// hands the coordinator a fresh roster for every call. It decides nothing: reach,
// damage, cooldowns, XP and in_combat all come back from C++.
//
// Two contracts hold this class together. First, nothing here may throw: godot-cpp
// builds this target with -fno-exceptions, so an escaped throw terminates the
// process instead of crossing the boundary, and every id is validated before a
// checked sim getter runs. Second, no raw sim pointer is ever handed to GDScript;
// only ids and values cross.
class OpenCombat : public godot::RefCounted {
	GDCLASS(OpenCombat, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// Seeded from C++ randomness: the scene never needs a reproducible part draw,
	// and seeded simulation tests stay in C++.
	OpenCombat();

	// The sheet must be configured, its id unique here, and the position finite.
	bool add_character(const godot::Ref<OpenCharacter> &sheet, const godot::Vector3 &position);
	// Clears the departing actor's orders and reconciles the survivors, releasing
	// the last reference only after that reconciliation has run.
	bool remove_character(int64_t id);
	bool set_position(int64_t id, const godot::Vector3 &position);
	bool issue_attack(int64_t actor_id, int64_t target_id);
	bool step(float seconds);
	bool is_in_combat(int64_t id) const;
	godot::Vector3 velocity_at(int64_t id) const;

	// The scene's one item-minting command, and deliberately narrow: it puts this registered
	// sheet's starting backpack on it - the five starter definitions, one instance each, at
	// tier 1, worn by nothing - and it can do nothing else. There is no definition id, no
	// quantity and no quality to pass, because the table is C++'s; a second call cannot mint a
	// duplicate, and a sheet that cannot carry anything is refused with its backpack intact.
	bool setup_starter_player(int64_t actor_id);

	// The swings this session has performed since the last read, oldest first, drained as it is
	// returned: a listener that asks twice gets the second answer empty, so a swing cannot be
	// animated or counted twice. Each dictionary is one report - who hit whom, what arrived,
	// what was turned aside, which part, and which technique was performed (0 when the attack
	// was the weapon's own). Presentation reads these and never decides from them.
	//
	// Keys: actor_id, target_id, part, damage, prevented, nullified, technique_id,
	// attacker_item, defender_item. Absent part, technique and items are 0.
	godot::Array take_combat_events();

private:
	struct Entry {
		godot::Ref<OpenCharacter> sheet;
		open::Vec3 position;
	};

	// One snapshot per call. False only if a retained object lost its sheet,
	// which add_character makes impossible; the roster is never kept.
	bool build_roster(std::vector<open::Combatant> &roster) const;
	std::vector<Entry>::iterator find(int64_t id);
	std::vector<Entry>::const_iterator find(int64_t id) const;

	// Ids for every instance this scene will ever own, and the only thing that mints them
	// besides the workbench actions to come. Reserved before use and never recycled, so an id
	// the popup is holding cannot belong to a second item later in the same session.
	open::ItemIdAllocator ids_;
	// Swings collected from the session on the step that performed them, so that reading them
	// is a separate decision from fighting.
	std::vector<open::CombatEvent> events_;

	std::vector<Entry> entries_;
	open::CombatSession session_;
};
