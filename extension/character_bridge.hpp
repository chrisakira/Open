#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
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
	// One successful configuration per object.
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
