#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "open/character.hpp"

class OpenCharacter : public godot::RefCounted {
	GDCLASS(OpenCharacter, godot::RefCounted)

	open::Character character_{};

protected:
	static void _bind_methods();

public:
	void configure(int faction, int level);
	int get_faction() const;
	int get_level() const;
	float get_health() const;
	float get_max_health() const;
	float get_strength() const;
	bool has_left_arm() const;
	bool has_right_arm() const;
	bool has_left_leg() const;
	bool has_right_leg() const;
	bool get_can_move() const;
	void set_can_move(bool can_move);
	void set_destination(const godot::Vector3 &destination);
	godot::Vector3 velocity_at(const godot::Vector3 &position, float speed) const;
};
