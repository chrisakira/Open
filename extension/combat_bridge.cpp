#include "combat_bridge.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <vector>

#include "open/starter_content.hpp"
#include "open/work_actions.hpp"

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

// The three words the action chat switches on, written from the C++ answer and from
// nothing else. `defender_item` cannot tell a dodge from no defence, and `prevented`
// cannot tell a block worth nothing from no block at all, so neither is consulted here;
// and because the value arrives from a known C++ enum rather than from a script, the
// switch covers it exhaustively and no unvalidated GDScript input is cast into anything.
godot::String defense_action_name(const std::optional<open::DefendAction> &action) {
	if (!action.has_value()) {
		return godot::String("none");  // no answer was attempted: cooling, stunned, or nothing held
	}
	switch (*action) {
	case open::DefendAction::Block: return godot::String("block");
	case open::DefendAction::Dodge: return godot::String("dodge");
	}
	return godot::String("none");  // unreachable while the enum holds two answers
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
	godot::ClassDB::bind_method(godot::D_METHOD("issue_recruit", "actor_id", "target_id"),
							   &OpenCombat::issue_recruit);
	godot::ClassDB::bind_method(godot::D_METHOD("found_squad", "actor_id"),
							   &OpenCombat::found_squad);
	godot::ClassDB::bind_method(godot::D_METHOD("step", "seconds"), &OpenCombat::step);
	godot::ClassDB::bind_method(godot::D_METHOD("is_in_combat", "id"), &OpenCombat::is_in_combat);
	godot::ClassDB::bind_method(godot::D_METHOD("velocity_at", "id"), &OpenCombat::velocity_at);
	godot::ClassDB::bind_method(godot::D_METHOD("setup_starter_player", "actor_id"),
							   &OpenCombat::setup_starter_player);
	godot::ClassDB::bind_method(godot::D_METHOD("take_combat_events"),
							   &OpenCombat::take_combat_events);
	godot::ClassDB::bind_method(godot::D_METHOD("update_workbenches", "benches"),
							   &OpenCombat::update_workbenches);
	godot::ClassDB::bind_method(godot::D_METHOD("request_work", "actor_id", "request"),
							   &OpenCombat::request_work);
	godot::ClassDB::bind_method(godot::D_METHOD("cancel_work", "actor_id"),
							   &OpenCombat::cancel_work);
	godot::ClassDB::bind_method(godot::D_METHOD("approach_workbench", "actor_id", "bench_id"),
							   &OpenCombat::approach_workbench);
	godot::ClassDB::bind_method(godot::D_METHOD("issue_move", "actor_id", "position"),
							   &OpenCombat::issue_move);
	godot::ClassDB::bind_method(godot::D_METHOD("get_work_snapshot", "actor_id"),
							   &OpenCombat::get_work_snapshot);
}

OpenCombat::OpenCombat()
	: catalog_(open::make_starter_catalog()), work_(*catalog_, ids_, materials_),
	  session_(random_seed()) {}

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
	open::Character *departing = leaving.is_null() ? nullptr : leaving->sheet();
	// Removing a worker interrupts before anything is released: a queued job clears free,
	// an active one stays paid, and the ledger never sees a second transaction or a refund.
	// `cancel_work` needs a roster, and the departing sheet with its last known position
	// is exactly one.
	if (departing != nullptr && departing->work_status().busy()) {
		const std::vector<open::Combatant> alone{
			open::Combatant{departing, entry->position}};
		session_.cancel_work(alone, static_cast<std::uint64_t>(id), work_);
	}
	if (departing != nullptr) {
		departing->clear_order();
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

bool OpenCombat::issue_recruit(int64_t actor_id, int64_t target_id) {
	if (!usable_id(actor_id) || !usable_id(target_id)) {
		return false;
	}
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	// The ask and every reason it might be refused are C++'s, including that faction is
	// not one of them: this door adds no filter of its own, only the id shape the
	// boundary asks for.
	return session_.issue_recruit(roster, static_cast<std::uint64_t>(actor_id),
								  static_cast<std::uint64_t>(target_id));
}

bool OpenCombat::found_squad(int64_t actor_id) {
	if (!usable_id(actor_id)) {
		return false;
	}
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	// The seat is C++'s decision and C++'s write: this door forwards the id and adds no
	// rule of its own. A founder that is already a member keeps that one seat, and a
	// corpse is refused by the sheet itself rather than by anything kept here.
	return session_.found_squad(roster, static_cast<std::uint64_t>(actor_id));
}

bool OpenCombat::step(float seconds) {
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	// The session rejects a bad frame before it reads or writes anything, so an
	// earlier position stays valid. Every frame runs with the work environment: queued
	// jobs may arrive and active ones may finish, and a frame that hid the environment
	// from a live job is refused outright rather than silently ticking the fight without
	// it. The environment may hold no benches at all - that is the last bench vanishing,
	// a fact live work has to be allowed to hear.
	const bool stepped = session_.step(seconds, roster, work_);
	if (!stepped) {
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
		// The full duration the performed attempt started its attack clock with, seconds:
		// presentation scales one clip to it. Copied from the report, never re-read from
		// the sheet, whose clock the frames since have already been wearing down.
		entry["attack_cooldown"] = swing.attack_cooldown;
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
		// Which answer the defender actually attempted, in the three words above. A dodge and
		// no defence at all share a zero `defender_item`, so this is the only field that can
		// tell them apart - and it is copied out of the report, never re-decided from the
		// clocks or the equipment, which frames since have already moved.
		entry["defense_action"] = defense_action_name(swing.defense_action);
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

// --- workbenches (Task 17) -------------------------------------------------------------

namespace {

// One strictly-typed integer field: no float masquerading as an id, no text that "reads
// like" a number, no silent Variant coercion. `allow_zero` marks the optional-reference
// fields (target, enchantment), where 0 is the documented spelling of "none".
bool read_int_field(const godot::Dictionary &source, const char *key, std::int64_t &out,
					bool allow_zero) {
	if (!source.has(key)) {
		return false;
	}
	const godot::Variant value = source.get(key, godot::Variant());
	if (value.get_type() != godot::Variant::INT) {
		return false;
	}
	const std::int64_t number = static_cast<std::int64_t>(value);
	if (number < 0 || (number == 0 && !allow_zero)) {
		return false;
	}
	out = number;
	return true;
}

bool has_exact_keys(const godot::Dictionary &source, const std::initializer_list<const char *> keys) {
	if (static_cast<std::size_t>(source.size()) != keys.size()) {
		return false;
	}
	for (const char *key : keys) {
		if (!source.has(key)) {
			return false;
		}
	}
	return true;
}

const char *work_phase_name(open::WorkPhase phase) {
	switch (phase) {
	case open::WorkPhase::Idle: return "idle";
	case open::WorkPhase::Queued: return "queued";
	case open::WorkPhase::Active: return "active";
	case open::WorkPhase::Completed: return "completed";
	case open::WorkPhase::Failed: return "failed";
	}
	return "idle";
}

const char *work_failure_name(open::WorkFailure failure) {
	switch (failure) {
	case open::WorkFailure::None: return "none";
	case open::WorkFailure::Cancelled: return "cancelled";
	case open::WorkFailure::Combat: return "combat";
	case open::WorkFailure::Movement: return "movement";
	case open::WorkFailure::BenchLost: return "bench_lost";
	case open::WorkFailure::TargetLost: return "target_lost";
	case open::WorkFailure::Dead: return "dead";
	case open::WorkFailure::RequirementsUnmet: return "requirements_unmet";
	}
	return "none";
}

}  // namespace

bool OpenCombat::update_workbenches(const godot::Array &benches) {
	// An empty table is a complete fact, not a missing one: a scene can lose its last
	// bench, and the docs count bench disappearance among the causes that end work. step
	// below always hands the session this (possibly empty) context while anything works,
	// so a cleared table reads as "the benches are gone" rather than as a hidden
	// environment.
	std::vector<open::WorkbenchObservation> observed;
	observed.reserve(static_cast<std::size_t>(benches.size()));
	for (std::int64_t index = 0; index < benches.size(); ++index) {
		const godot::Variant raw = benches[static_cast<int>(index)];
		if (raw.get_type() != godot::Variant::DICTIONARY) {
			return false;
		}
		const godot::Dictionary entry = raw;
		if (!has_exact_keys(entry, {"id", "type", "position"})) {
			return false;  // including a fourth field the coordinator never declared
		}
		std::int64_t id = 0;
		std::int64_t type = 0;
		if (!read_int_field(entry, "id", id, false) || !read_int_field(entry, "type", type, true)) {
			return false;
		}
		if (type != static_cast<std::int64_t>(open::BenchType::Smithing)
			&& type != static_cast<std::int64_t>(open::BenchType::Enchanting)) {
			return false;
		}
		if (entry.get("position", godot::Variant()).get_type() != godot::Variant::VECTOR3) {
			return false;
		}
		const godot::Vector3 position = entry.get("position", godot::Variant());
		if (!std::isfinite(static_cast<double>(position.x))
			|| !std::isfinite(static_cast<double>(position.y))
			|| !std::isfinite(static_cast<double>(position.z))) {
			return false;
		}
		for (const open::WorkbenchObservation &earlier : observed) {
			if (earlier.id == static_cast<open::WorkbenchId>(id)) {
				return false;  // one id cannot be two places
			}
		}
		observed.push_back(open::WorkbenchObservation{static_cast<open::WorkbenchId>(id),
													  static_cast<open::BenchType>(type),
													  to_sim(position)});
	}
	// Validated whole, then adopted whole: the previous table served until this moment and
	// the new one serves from it.
	work_.benches = std::move(observed);
	return true;
}

bool OpenCombat::request_work(int64_t actor_id, const godot::Dictionary &request) {
	if (!usable_id(actor_id) || !has_exact_keys(request,
			{"kind", "recipe", "bench", "target", "quality", "enchantment"})) {
		return false;
	}
	std::int64_t kind = 0;
	std::int64_t recipe = 0;
	std::int64_t bench = 0;
	std::int64_t target = 0;
	std::int64_t quality = 0;
	std::int64_t enchantment = 0;
	if (!read_int_field(request, "kind", kind, true) || !read_int_field(request, "recipe", recipe, false)
		|| !read_int_field(request, "bench", bench, false)
		|| !read_int_field(request, "target", target, true)
		|| !read_int_field(request, "quality", quality, true)
		|| !read_int_field(request, "enchantment", enchantment, true)) {
		return false;
	}
	open::WorkRequest parsed;
	// GDScript ints are 64-bit and the domain types are narrower in places, so nothing is
	// narrowed before it has been measured: a kind that only becomes a kind by losing its
	// high half, or a tier that only becomes 1 by losing its own, is a malformed request,
	// not a job. The id fields need no such guard - they are 64-bit in the domain too, and
	// read_int_field has already refused the negatives.
	if (kind >= static_cast<std::int64_t>(open::WorkKind::Count)
		|| !open::valid(static_cast<open::WorkKind>(kind))) {
		return false;
	}
	parsed.kind = static_cast<open::WorkKind>(kind);
	parsed.recipe = static_cast<open::RecipeId>(recipe);
	parsed.bench = static_cast<open::WorkbenchId>(bench);
	if (target != 0) {
		parsed.target = static_cast<open::ItemId>(target);
	}
	if (quality > static_cast<std::int64_t>(std::numeric_limits<unsigned>::max())) {
		return false;
	}
	parsed.quality = static_cast<unsigned>(quality);
	if (enchantment != 0) {
		parsed.enchantment = static_cast<open::EnchantmentId>(enchantment);
	}
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	return session_.request_work(roster, static_cast<std::uint64_t>(actor_id), parsed, work_);
}

bool OpenCombat::cancel_work(int64_t actor_id) {
	if (!usable_id(actor_id)) {
		return false;
	}
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	return session_.cancel_work(roster, static_cast<std::uint64_t>(actor_id), work_);
}

bool OpenCombat::approach_workbench(int64_t actor_id, int64_t bench_id) {
	if (!usable_id(actor_id) || !usable_id(bench_id)) {
		return false;
	}
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	return session_.approach_workbench(roster, static_cast<std::uint64_t>(actor_id),
									   static_cast<open::WorkbenchId>(bench_id), work_);
}

bool OpenCombat::issue_move(int64_t actor_id, const godot::Vector3 &position) {
	if (!usable_id(actor_id)) {
		return false;
	}
	std::vector<open::Combatant> roster;
	if (!build_roster(roster)) {
		return false;
	}
	return session_.issue_move(roster, static_cast<std::uint64_t>(actor_id), to_sim(position),
							   work_);
}

godot::Dictionary OpenCombat::get_work_snapshot(int64_t actor_id) const {
	godot::Dictionary snapshot;
	if (!usable_id(actor_id)) {
		snapshot["valid"] = false;
		return snapshot;
	}
	const auto entry = find(actor_id);
	open::Character *who = entry == entries_.end() || entry->sheet.is_null() ? nullptr
																			 : entry->sheet->sheet();
	if (who == nullptr) {
		snapshot["valid"] = false;
		return snapshot;
	}
	snapshot["valid"] = true;
	const open::WorkStatus status = who->work_status();
	snapshot["busy"] = status.busy();
	snapshot["phase"] = godot::String::utf8(work_phase_name(status.phase));
	snapshot["failure"] = godot::String::utf8(work_failure_name(status.failure));
	snapshot["elapsed"] = status.elapsed;
	snapshot["duration"] = open::kWorkDuration;
	snapshot["output"] = static_cast<std::int64_t>(status.output.value_or(0));
	godot::Dictionary request;
	request["kind"] = static_cast<std::int64_t>(status.request.kind);
	request["recipe"] = static_cast<std::int64_t>(status.request.recipe);
	request["bench"] = static_cast<std::int64_t>(status.request.bench);
	request["target"] = static_cast<std::int64_t>(status.request.target.value_or(0));
	request["quality"] = static_cast<std::int64_t>(status.request.quality);
	request["enchantment"] = static_cast<std::int64_t>(status.request.enchantment.value_or(0));
	snapshot["request"] = request;

	// The purse, by material id. The ledger answers zero for what is not owned, so an
	// absent entry and an empty one cannot be confused by whoever reads this.
	godot::Dictionary materials;
	std::set<open::MaterialId> spendable;
	for (const open::RecipeId id : catalog_->recipe_ids()) {
		const open::CraftRecipe *recipe = catalog_->recipe(id);
		if (recipe != nullptr) {
			for (const open::MaterialQuantity &line : recipe->cost) {
				spendable.insert(line.material);
			}
		}
	}
	for (const open::MaterialId material : spendable) {
		materials[static_cast<std::int64_t>(material)] = static_cast<std::int64_t>(
			materials_.quantity(static_cast<std::uint64_t>(actor_id), material));
	}
	snapshot["materials"] = materials;
	snapshot["quality_cap"] = static_cast<std::int64_t>(open::max_weapon_quality(*who));

	// The whole work table, with one fact added in C++ per row: can this sheet, right now,
	// pay this recipe's cost. Eligibility beyond the purse - caps, benches, targets, combat -
	// stays the start-time answer it has always been, so a greyed button can never disagree
	// with a refused request, but a lit one never promises delivery either.
	godot::Array recipes;
	for (const open::RecipeId id : catalog_->recipe_ids()) {
		const open::CraftRecipe *recipe = catalog_->recipe(id);
		if (recipe == nullptr) {
			continue;
		}
		godot::Dictionary row;
		row["id"] = static_cast<std::int64_t>(recipe->id);
		row["kind"] = static_cast<std::int64_t>(recipe->kind);
		row["bench"] = static_cast<std::int64_t>(recipe->bench);
		row["output"] = static_cast<std::int64_t>(recipe->output);
		godot::Array cost;
		bool affordable = true;
		for (const open::MaterialQuantity &line : recipe->cost) {
			godot::Dictionary bill;
			bill["material"] = static_cast<std::int64_t>(line.material);
			bill["quantity"] = static_cast<std::int64_t>(line.quantity);
			cost.push_back(bill);
			if (materials_.quantity(static_cast<std::uint64_t>(actor_id), line.material) < line.quantity) {
				affordable = false;
			}
		}
		row["cost"] = cost;
		row["affordable"] = affordable;
		if (recipe->kind == open::WorkKind::EnchantWeapon) {
			// The bench's own enchantment list, from the catalog: the popup sends an id it
			// was shown and invents none.
			godot::Array offers;
			for (const open::EnchantmentId enchantment_id : catalog_->enchantment_ids()) {
				offers.push_back(static_cast<std::int64_t>(enchantment_id));
			}
			row["enchantments"] = offers;
		}
		recipes.push_back(row);
	}
	snapshot["recipes"] = recipes;
	return snapshot;
}
