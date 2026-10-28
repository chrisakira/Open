#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "open/move_order.hpp"

class OpenMoveOrder : public godot::RefCounted {
	GDCLASS(OpenMoveOrder, godot::RefCounted)

	open::MoveOrder order_{};

protected:
	static void _bind_methods();

public:
	void set_destination(const godot::Vector3 &destination);
	godot::Vector3 velocity_at(const godot::Vector3 &position, float speed) const;
};
