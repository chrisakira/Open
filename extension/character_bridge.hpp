#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <optional>

#include "open/character.hpp"

// Presentation-facing wrapper around one open::Character. It validates ids,
// never lets a C++ exception cross into Godot, and answers safely when nothing
// has been configured. No gameplay rules live here.
class OpenCharacter : public godot::RefCounted {
	GDCLASS(OpenCharacter, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// One successful configuration per object. The sheet is made against the production item
	// catalog (see the source): a character with nothing in its hands is unarmed whatever the
	// catalog holds, and a character who is given something can be told what it is.
	bool configure(int64_t id, const godot::String &name, int64_t faction, int64_t level,
			   int64_t blessing);
	bool configure_npc(int64_t id, const godot::String &name, int64_t faction, int64_t level, int64_t seed);

	bool is_configured() const;
	int64_t get_character_id() const;
	godot::String get_character_name() const;
	int get_faction() const;
	int get_level() const;
	int get_blessing() const;

	float get_health() const;
	float get_max_health() const;
	float get_strength() const;
	float get_attribute(int64_t attribute_id) const;
	float get_attribute_base(int64_t attribute_id) const;
	float get_stamina() const;
	float get_max_stamina() const;
	float get_mana() const;
	float get_max_mana() const;
	float get_part_health(int64_t part_id) const;
	float get_part_max_health(int64_t part_id) const;

	bool is_alive() const;
	bool can_walk() const;
	float get_walk_speed() const;

	// Convenience limbs: true while that part's health is above zero.
	bool has_left_arm() const;
	bool has_right_arm() const;
	bool has_left_leg() const;
	bool has_right_leg() const;

	bool get_can_move() const;
	void set_can_move(bool can_move);

	// The playtest lever (Character::playtest_power_up): bases to ten, combat
	// jobs to a hundred. False for an unconfigured object or a dead sheet.
	bool power_up();

	// Read-only view for the character popup: one dictionary per basic skill
	// (name, value, advanced_name, advanced_value) in BasicSkill order. Empty
	// for an unconfigured sheet. Names and effective values come from C++.
	godot::Array get_skill_snapshot() const;

	bool set_destination(const godot::Vector3 &destination);
	void clear_order();
	godot::Vector3 velocity_at(const godot::Vector3 &position) const;

	// Read-only view of the attack order C++ is holding: 0 when the sheet has no
	// attack or was never configured. Ids keep their full 64-bit width.
	int64_t get_attack_target_id() const;

	// --- recruiting ---------------------------------------------------------------
	//
	// The scene's reads and writes for docs/recruiting.md: who may be asked, and which
	// dummy condition answers the ask. Both are character state, so a node property is
	// not offered - and both are refused on a dead sheet, which is C++'s answer rather
	// than one this adapter keeps.
	//
	// Membership and faction are what an answered ask *does*, which is the coordinator's
	// and nothing else's: there is no `set_in_squad` and no `set_faction` here, because a
	// popup that could declare somebody a member would be a second place where joining
	// is decided. `get_recruit_condition` answers the bridge's two ints - 0 always fails,
	// 1 always succeeds - and an int outside them is refused rather than trusted.
	bool is_recruitable() const;
	bool set_recruitable(bool recruitable);
	int get_recruit_condition() const;
	bool set_recruit_condition(int condition);
	bool is_in_squad() const;

	// The sheet this one is walking to ask: 0 when it holds no ask, because a sheet with
	// no ask is not asking a character whose id is 0. The order itself stays C++'s.
	int64_t get_recruit_target_id() const;

	// --- equipment (Task 14a) -------------------------------------------------
	//
	// The equipment popup's two reads and two commands. Nothing here decides where an
	// item may go, whether a limb can hold it or whether it is even owned: `equip_item`
	// and `unequip_item` are the same calls the simulation makes for itself, refused by
	// C++ with its ownership, limb and two-hand rules intact. A dead sheet is refused
	// too, and `get_equipment_snapshot`/`get_weapon_snapshot` answer the documented keys
	// even when nothing is configured, so a popup never has to guess whether an absent
	// key means "empty" or "wrong build".

	// One row per owned instance, carried first and then each occupied slot in slot
	// order: item_id, definition_id, model_key, slots, allowed_slots, hand_use, quality,
	// durability, max_durability, enchantments_used, enchantment_capacity, enchantments,
	// requirements.
	godot::Array get_equipment_snapshot() const;
	// One weapon resolved in this sheet's hands - effectiveness, damage components,
	// reach, the cost an attempt actually carries, and the techniques it supports - for
	// an id this sheet owns. Empty for anything else, including a thing that is not a
	// weapon: the popup shows a whole weapon or no weapon.
	godot::Dictionary get_weapon_snapshot(int64_t item_id) const;
	bool equip_item(int64_t item_id, int64_t slot);
	bool unequip_item(int64_t slot);

	// --- techniques (Task 14a) ------------------------------------------------

	// enabled, player_controlled, can_configure, index, supported, sequence, cooldowns.
	// The rows carry the definition's own numbers plus `available`, which is exactly what
	// the attack pipeline would allow, so the popup cannot offer a move it would refuse.
	// Bare hands support nothing and a dead sheet configures nothing.
	godot::Dictionary get_technique_snapshot() const;
	// Replaces the saved sequence and the enabled switch in one command. Each step must be
	// a dictionary of exactly `technique_id` (a positive int) and `skip_on_cooldown` (a
	// bool); anything else - another type, another key, a zero or negative id - refuses the
	// whole call without touching the state that is already there. A positive id the catalog
	// does not know is accepted and kept: the sequence is what the player chose, and it is
	// not the simulation's to prune.
	bool configure_techniques(bool enabled, const godot::Array &steps);
	// Who chooses the moves: player or simulation. This alone changes, and C++ keeps the
	// sequence, its position, the switch and every clock through the handover - so the scene
	// can say "this sheet's human is driving" without touching what that human configured.
	bool set_player_controlled(bool controlled);

	bool apply_damage(int64_t part_id, float amount);
	bool regenerate(float seconds, bool in_combat);

private:
	const open::Character *sheet() const;
	open::Character *sheet();

	// OpenCombat is the only reader of these pointers outside this class, and a
	// raw pointer is never handed to GDScript.
	friend class OpenCombat;

	std::optional<open::Character> character_;
};
