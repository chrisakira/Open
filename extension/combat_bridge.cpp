#include "combat_bridge.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "open/starter_content.hpp"

// This adapter is compiled with -fno-exceptions, exactly like the rest of the
// extension, so no call here may be able to throw: every input is checked before
// a throwing sim getter is reached, and the seed source below cannot fail. Keep it
// that way - a throw at this boundary cannot be caught, only terminated.

namespace {

// Godot integers are signed 64-bit. Refuse anything that is not a positive id
// before the conversion, so a negative value cannot wrap into a real character.
bool usable_id(std::int64_t id) {
	return id > 0;
}

bool position_is_finite(const godot::Vector3 &position) {
	return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

open::Vec3 to_sim(const godot::Vector3 &position) {
	return open::Vec3{position.x, position.y, position.z};
}

godot::Vector3 to_godot(const open::Vec3 &velocity) {
	return godot::Vector3(velocity.x, velocity.y, velocity.z);
}

// No seed crosses this boundary on purpose: reproducible draws are a C++ test
// concern, the scene only needs an unbiased body part.
//
// This deliberately does not use std::random_device. godot-cpp compiles this
// target with -fno-exceptions, and random_device reports an unavailable entropy
// source by throwing - with exceptions off, that throw is a terminate, which is
// worse than crossing the boundary. The same rule the rest of the bridge follows
// (character_rules.hpp: check ids instead of catching) applies here: pick a seed
// source that cannot throw at all. Clock, ASLR and a per-process counter, mixed by
// splitmix64, give each session its own stream without any allocation.
std::uint64_t splitmix64(std::uint64_t &state) {
	std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

std::uint64_t random_seed() {
	// One counter per process, mixed with a clock value and the address of a local
	// (so ASLR contributes): two sessions opened in the same tick still differ.
	static std::uint64_t counter = 0;
	std::uint64_t state = static_cast<std::uint64_t>(
		std::chrono::steady_clock::now().time_since_epoch().count());
	state ^= ++counter * 0xD6E8FEB86659FD93ull;
	char stack_marker;
	state ^= reinterpret_cast<std::uintptr_t>(&stack_marker);
	return splitmix64(state);
}

}  // namespace

void OpenCombat::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("add_character", "sheet", "position"),
							   &OpenCombat::add_character);
	godot::ClassDB::bind_method(godot::D_METHOD("remove_character", "id"), &OpenCombat::remove_character);
	godot::ClassDB::bind_method(godot::D_METHOD("set_position", "id", "position"),
							   &OpenCombat::set_position);
	godot::ClassDB::bind_method(godot::D_METHOD("issue_attack", "actor_id", "target_id"),
							   &OpenCombat::issue_attack);
	godot::ClassDB::bind_method(godot::D_METHOD("step", "seconds"), &OpenCombat::step);
	godot::ClassDB::bind_method(godot::D_METHOD("is_in_combat", "id"), &OpenCombat::is_in_combat);
	godot::ClassDB::bind_method(godot::D_METHOD("velocity_at", "id"), &OpenCombat::velocity_at);
	godot::ClassDB::bind_method(godot::D_METHOD("setup_starter_player", "actor_id"),
							   &OpenCombat::setup_starter_player);
	godot::ClassDB::bind_method(godot::D_METHOD("take_combat_events"),
							   &OpenCombat::take_combat_events);
}

OpenCombat::OpenCombat() : session_(random_seed()) {}

bool OpenCombat::build_roster(std::vector<open::Combatant> &roster) const {
	roster.clear();
	roster.reserve(entries_.size());
	for (const Entry &entry : entries_) {
		open::Character *sheet = entry.sheet.is_null() ? nullptr : entry.sheet->sheet();
		if (sheet == nullptr) {
			godot::UtilityFunctions::push_error("OpenCombat: a registered sheet has no simulation object");
			return false;
		}
		roster.push_back(open::Combatant{sheet, entry.position});
	}
	return true;
}

std::vector<OpenCombat::Entry>::iterator OpenCombat::find(int64_t id) {
	for (auto entry = entries_.begin(); entry != entries_.end(); ++entry) {
		if (!entry->sheet.is_null() && entry->sheet->get_character_id() == id) {
			return entry;
		}
	}
	return entries_.end();
}

std::vector<OpenCombat::Entry>::const_iterator OpenCombat::find(int64_t id) const {
	for (auto entry = entries_.begin(); entry != entries_.end(); ++entry) {
		if (!entry->sheet.is_null() && entry->sheet->get_character_id() == id) {
			return entry;
		}
	}
	return entries_.end();
}

bool OpenCombat::add_character(const godot::Ref<OpenCharacter> &sheet, const godot::Vector3 &position) {
	if (sheet.is_null() || !sheet->is_configured() || sheet->sheet() == nullptr) {
		return false;
	}
	const int64_t id = sheet->get_character_id();
	if (!usable_id(id) || !position_is_finite(position)) {
		return false;
	}
	// Two entries for one id would make the same sheet two actors, which the
	// coordinator refuses to read; keep the first registration.
	if (find(id) != entries_.end()) {
		return false;
	}
	entries_.push_back(Entry{sheet, to_sim(position)});
	return true;
}

bool OpenCombat::remove_character(int64_t id) {
	if (!usable_id(id)) {
		return false;
	}
	const auto entry = find(id);
	if (entry == entries_.end()) {
		return false;
	}
	// The plan's order, kept explicit: clear the departing actor's own intent,
	// reconcile the survivors, and only then let the last reference go. `leaving`
	// holds the sheet alive across the reconciliation even if the caller already
	// dropped its own reference, so no survivor is ever read against a freed
	// object. Erasing from entries_ first is what makes an order aimed at this id
	// stale, which is exactly what the survivors then reconcile away.
	godot::Ref<OpenCharacter> leaving = entry->sheet;
	if (open::Character *sheet = leaving->sheet()) {
		sheet->clear_order();
	}
	entries_.erase(entry);
	std::vector<open::Combatant> roster;
	if (build_roster(roster)) {
		session_.step(0.0f, roster);
	}
	return true;
}

bool OpenCombat::set_position(int64_t id, const godot::Vector3 &position) {
	if (!usable_id(id) || !position_is_finite(position)) {
		return false;
	}
	const auto entry = find(id);
	if (entry == entries_.end()) {
		return false;
	}
	entry->position = to_sim(position);
	return true;
}

bool OpenCombat::issue_attack(int64_t actor_id, int64_t target_id) {
	if (!usable_id(actor_id) || !usable_id(target_id)) {
		return false;
	}
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	return session_.issue_attack(roster, static_cast<std::uint64_t>(actor_id),
								 static_cast<std::uint64_t>(target_id));
}

bool OpenCombat::step(float seconds) {
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	// The session rejects a bad frame before it reads or writes anything, so an
	// earlier position stays valid.
	if (!session_.step(seconds, roster)) {
		return false;
	}
	// Collected here rather than handed straight on: a frame that performed nothing has
	// nothing to add, and a listener may read the reports on its own schedule.
	std::vector<open::CombatEvent> performed = session_.take_swing_events();
	events_.insert(events_.end(), performed.begin(), performed.end());
	return true;
}

bool OpenCombat::setup_starter_player(int64_t actor_id) {
	if (!usable_id(actor_id)) {
		return false;
	}
	const auto entry = find(actor_id);
	if (entry == entries_.end()) {
		return false;
	}
	open::Character *who = entry->sheet == nullptr ? nullptr : entry->sheet->sheet();
	if (who == nullptr) {
		return false;  // add_character makes this impossible; a sheet with no sim is not a sheet
	}
	// All of the work is C++'s: the table, the ids, the tier, the durability, the starting
	// supply and the all-or-nothing rule. This cannot be asked for anything else.
	return open::initialize_starter_inventory(*who, ids_, materials_);
}

godot::Array OpenCombat::take_combat_events() {
	godot::Array events;
	for (const open::CombatEvent &event : events_) {
		const open::SwingResult &swing = event.swing;
		godot::Dictionary entry;
		entry["actor_id"] = static_cast<std::int64_t>(event.actor);
		entry["target_id"] = static_cast<std::int64_t>(event.target);
		// 0 for "no part was charged", which is what happens when nothing arrived: armour took
		// it all, or a defence worth everything turned the blow aside.
		entry["part"] = swing.part.has_value() ? static_cast<std::int64_t>(*swing.part) : 0;
		entry["damage"] = swing.damage;
		entry["prevented"] = swing.prevented;
		entry["nullified"] = swing.nullified;
		// 0 means no technique: the attack was the weapon's own. A technique that did no
		// damage is still named, which is the difference presentation has to keep.
		entry["technique_id"] =
			swing.technique.has_value() ? static_cast<std::int64_t>(*swing.technique) : 0;
		// 0 for bare hands, and for an answer that was never made: in each case C++ reports no
		// item there, and the bridge does not turn that into a guess.
		entry["attacker_item"] =
			swing.attack_item.has_value() ? static_cast<std::int64_t>(*swing.attack_item) : 0;
		entry["defender_item"] =
			swing.defense_item.has_value() ? static_cast<std::int64_t>(*swing.defense_item) : 0;
		events.push_back(entry);
	}
	events_.clear();
	return events;
}

bool OpenCombat::is_in_combat(int64_t id) const {
	if (!usable_id(id)) {
		return false;
	}
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	return session_.in_combat(static_cast<std::uint64_t>(id), roster);
}

godot::Vector3 OpenCombat::velocity_at(int64_t id) const {
	if (!usable_id(id)) {
		return godot::Vector3();
	}
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return godot::Vector3();
	}
	return to_godot(session_.velocity_at(static_cast<std::uint64_t>(id), roster));
}
