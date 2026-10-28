#include "move_order_bridge.hpp"

#include <godot_cpp/core/class_db.hpp>

void OpenMoveOrder::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("set_destination", "destination"),
							   &OpenMoveOrder::set_destination);
	godot::ClassDB::bind_method(godot::D_METHOD("velocity_at", "position", "speed"),
							   &OpenMoveOrder::velocity_at);
}

void OpenMoveOrder::set_destination(const godot::Vector3 &destination) {
	order_.set_destination(destination.x, destination.y, destination.z);
}

godot::Vector3 OpenMoveOrder::velocity_at(const godot::Vector3 &position, float speed) const {
	const open::Vec3 v = order_.velocity_at(position.x, position.y, position.z, speed);
	return godot::Vector3(v.x, v.y, v.z);
}
