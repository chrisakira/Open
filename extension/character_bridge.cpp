#include "character_bridge.hpp"

#include <godot_cpp/core/class_db.hpp>

void OpenCharacter::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("configure", "faction", "level"),
							   &OpenCharacter::configure);
	godot::ClassDB::bind_method(godot::D_METHOD("get_faction"), &OpenCharacter::get_faction);
	godot::ClassDB::bind_method(godot::D_METHOD("get_level"), &OpenCharacter::get_level);
	godot::ClassDB::bind_method(godot::D_METHOD("get_health"), &OpenCharacter::get_health);
	godot::ClassDB::bind_method(godot::D_METHOD("get_max_health"), &OpenCharacter::get_max_health);
	godot::ClassDB::bind_method(godot::D_METHOD("get_strength"), &OpenCharacter::get_strength);
	godot::ClassDB::bind_method(godot::D_METHOD("has_left_arm"), &OpenCharacter::has_left_arm);
	godot::ClassDB::bind_method(godot::D_METHOD("has_right_arm"), &OpenCharacter::has_right_arm);
	godot::ClassDB::bind_method(godot::D_METHOD("has_left_leg"), &OpenCharacter::has_left_leg);
	godot::ClassDB::bind_method(godot::D_METHOD("has_right_leg"), &OpenCharacter::has_right_leg);
	godot::ClassDB::bind_method(godot::D_METHOD("get_can_move"), &OpenCharacter::get_can_move);
	godot::ClassDB::bind_method(godot::D_METHOD("set_can_move", "can_move"),
							   &OpenCharacter::set_can_move);
	godot::ClassDB::bind_method(godot::D_METHOD("set_destination", "destination"),
							   &OpenCharacter::set_destination);
	godot::ClassDB::bind_method(godot::D_METHOD("velocity_at", "position", "speed"),
							   &OpenCharacter::velocity_at);
}

void OpenCharacter::configure(int faction, int level) {
	const open::Faction f = (faction == 0) ? open::Faction::Friendly : open::Faction::Enemy;
	character_ = open::Character::make(f, level);
}

int OpenCharacter::get_faction() const {
	return character_.faction() == open::Faction::Friendly ? 0 : 1;
}

int OpenCharacter::get_level() const {
	return character_.level();
}

float OpenCharacter::get_health() const {
	return character_.health();
}

float OpenCharacter::get_max_health() const {
	return character_.max_health();
}

float OpenCharacter::get_strength() const {
	return character_.strength();
}

bool OpenCharacter::has_left_arm() const {
	return character_.limbs().left_arm;
}

bool OpenCharacter::has_right_arm() const {
	return character_.limbs().right_arm;
}

bool OpenCharacter::has_left_leg() const {
	return character_.limbs().left_leg;
}

bool OpenCharacter::has_right_leg() const {
	return character_.limbs().right_leg;
}

bool OpenCharacter::get_can_move() const {
	return character_.can_move();
}

void OpenCharacter::set_can_move(bool can_move) {
	character_.set_can_move(can_move);
}

void OpenCharacter::set_destination(const godot::Vector3 &destination) {
	character_.set_destination(destination.x, destination.y, destination.z);
}

godot::Vector3 OpenCharacter::velocity_at(const godot::Vector3 &position, float speed) const {
	const open::Vec3 v = character_.velocity_at(position.x, position.y, position.z, speed);
	return godot::Vector3(v.x, v.y, v.z);
}
