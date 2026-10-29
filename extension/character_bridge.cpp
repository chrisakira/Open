#include "character_bridge.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>
#include <limits>
#include <random>

namespace {

// Godot integers are 64-bit. Check the domain before narrowing to the int the
// simulation enums use, so an id that only becomes legal after truncation is
// refused instead of silently reinterpreted.
template <class Enum>
bool enum_id_is_valid(std::int64_t id) {
	if (id < std::numeric_limits<int>::min() || id > std::numeric_limits<int>::max()) {
		return false;
	}
	return open::valid(static_cast<Enum>(static_cast<int>(id)));
}

bool level_id_is_valid(std::int64_t level) {
	if (level < std::numeric_limits<int>::min() || level > std::numeric_limits<int>::max()) {
		return false;
	}
	return open::valid(static_cast<int>(level));
}

void report_bad_id(const char *what, std::int64_t id) {
	godot::UtilityFunctions::push_error(godot::String("OpenCharacter: unknown ") +
										godot::String(what) + " id " +
										godot::String::num_int64(id));
}

}  // namespace

void OpenCharacter::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("configure", "id", "name", "faction", "level", "blessing"),
							   &OpenCharacter::configure);
	godot::ClassDB::bind_method(
		godot::D_METHOD("configure_npc", "id", "name", "faction", "level", "seed"),
		&OpenCharacter::configure_npc);
	godot::ClassDB::bind_method(godot::D_METHOD("is_configured"), &OpenCharacter::is_configured);
	godot::ClassDB::bind_method(godot::D_METHOD("get_character_id"), &OpenCharacter::get_character_id);
	godot::ClassDB::bind_method(godot::D_METHOD("get_character_name"), &OpenCharacter::get_character_name);
	godot::ClassDB::bind_method(godot::D_METHOD("get_faction"), &OpenCharacter::get_faction);
	godot::ClassDB::bind_method(godot::D_METHOD("get_level"), &OpenCharacter::get_level);
	godot::ClassDB::bind_method(godot::D_METHOD("get_blessing"), &OpenCharacter::get_blessing);
	godot::ClassDB::bind_method(godot::D_METHOD("get_health"), &OpenCharacter::get_health);
	godot::ClassDB::bind_method(godot::D_METHOD("get_max_health"), &OpenCharacter::get_max_health);
	godot::ClassDB::bind_method(godot::D_METHOD("get_strength"), &OpenCharacter::get_strength);
	godot::ClassDB::bind_method(godot::D_METHOD("get_attribute", "attribute_id"),
							   &OpenCharacter::get_attribute);
	godot::ClassDB::bind_method(godot::D_METHOD("get_attribute_base", "attribute_id"),
							   &OpenCharacter::get_attribute_base);
	godot::ClassDB::bind_method(godot::D_METHOD("get_stamina"), &OpenCharacter::get_stamina);
	godot::ClassDB::bind_method(godot::D_METHOD("get_max_stamina"), &OpenCharacter::get_max_stamina);
	godot::ClassDB::bind_method(godot::D_METHOD("get_mana"), &OpenCharacter::get_mana);
	godot::ClassDB::bind_method(godot::D_METHOD("get_max_mana"), &OpenCharacter::get_max_mana);
	godot::ClassDB::bind_method(godot::D_METHOD("get_part_health", "part_id"),
							   &OpenCharacter::get_part_health);
	godot::ClassDB::bind_method(godot::D_METHOD("get_part_max_health", "part_id"),
							   &OpenCharacter::get_part_max_health);
	godot::ClassDB::bind_method(godot::D_METHOD("is_alive"), &OpenCharacter::is_alive);
	godot::ClassDB::bind_method(godot::D_METHOD("can_walk"), &OpenCharacter::can_walk);
	godot::ClassDB::bind_method(godot::D_METHOD("get_walk_speed"), &OpenCharacter::get_walk_speed);
	godot::ClassDB::bind_method(godot::D_METHOD("has_left_arm"), &OpenCharacter::has_left_arm);
	godot::ClassDB::bind_method(godot::D_METHOD("has_right_arm"), &OpenCharacter::has_right_arm);
	godot::ClassDB::bind_method(godot::D_METHOD("has_left_leg"), &OpenCharacter::has_left_leg);
	godot::ClassDB::bind_method(godot::D_METHOD("has_right_leg"), &OpenCharacter::has_right_leg);
	godot::ClassDB::bind_method(godot::D_METHOD("get_can_move"), &OpenCharacter::get_can_move);
	godot::ClassDB::bind_method(godot::D_METHOD("set_can_move", "can_move"),
							   &OpenCharacter::set_can_move);
	godot::ClassDB::bind_method(godot::D_METHOD("set_destination", "destination"),
							   &OpenCharacter::set_destination);
	godot::ClassDB::bind_method(godot::D_METHOD("clear_order"), &OpenCharacter::clear_order);
	godot::ClassDB::bind_method(godot::D_METHOD("velocity_at", "position"), &OpenCharacter::velocity_at);
	godot::ClassDB::bind_method(godot::D_METHOD("apply_damage", "part_id", "amount"),
							   &OpenCharacter::apply_damage);
	godot::ClassDB::bind_method(godot::D_METHOD("regenerate", "seconds", "in_combat"),
							   &OpenCharacter::regenerate);
}

bool OpenCharacter::configure(int64_t id, const godot::String &name, int64_t faction, int64_t level,
							  int64_t blessing) {
	if (character_.has_value()) {
		return false;  // one configuration per object
	}
	if (id <= 0) {
		return false;
	}
	if (!enum_id_is_valid<open::Faction>(faction) || !level_id_is_valid(level) ||
		!enum_id_is_valid<open::Attribute>(blessing)) {
		return false;
	}
	const open::Faction team = static_cast<open::Faction>(static_cast<int>(faction));
	const open::Attribute blessed = static_cast<open::Attribute>(static_cast<int>(blessing));
	const std::optional<open::Character> made = open::Character::create(open::CharacterConfig{
		static_cast<std::uint64_t>(id), std::string(name.utf8().get_data()), team, level, blessed});
	if (!made.has_value()) {
		return false;
	}
	character_ = made;
	return true;
}

bool OpenCharacter::configure_npc(int64_t id, const godot::String &name, int64_t faction,
								  int64_t level, int64_t seed) {
	if (character_.has_value()) {
		return false;
	}
	if (id <= 0) {
		return false;
	}
	if (!enum_id_is_valid<open::Faction>(faction) || !level_id_is_valid(level)) {
		return false;
	}
	const open::Faction team = static_cast<open::Faction>(static_cast<int>(faction));
	std::mt19937_64 rng(static_cast<std::uint64_t>(seed));
	const open::Attribute blessed = open::random_blessing(rng);
	const std::optional<open::Character> made = open::Character::create(open::CharacterConfig{
		static_cast<std::uint64_t>(id), std::string(name.utf8().get_data()), team, level, blessed});
	if (!made.has_value()) {
		return false;
	}
	character_ = made;
	return true;
}

const open::Character *OpenCharacter::sheet() const {
	if (!character_.has_value()) {
		return nullptr;
	}
	return &*character_;
}

open::Character *OpenCharacter::sheet() {
	if (!character_.has_value()) {
		return nullptr;
	}
	return &*character_;
}

bool OpenCharacter::is_configured() const {
	return character_.has_value();
}

int64_t OpenCharacter::get_character_id() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0 : static_cast<int64_t>(c->id());
}

godot::String OpenCharacter::get_character_name() const {
	const open::Character *c = sheet();
	return c == nullptr ? godot::String() : godot::String(c->name().c_str());
}

int OpenCharacter::get_faction() const {
	const open::Character *c = sheet();
	if (c == nullptr) {
		return -1;
	}
	return c->faction() == open::Faction::Friendly ? 0 : 1;
}

int OpenCharacter::get_level() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0 : c->level();
}

int OpenCharacter::get_blessing() const {
	const open::Character *c = sheet();
	return c == nullptr ? -1 : static_cast<int>(c->blessing());
}

float OpenCharacter::get_health() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0.0f : c->pool(open::PoolKind::Hp).current;
}

float OpenCharacter::get_max_health() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0.0f : c->pool(open::PoolKind::Hp).maximum;
}

float OpenCharacter::get_strength() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0.0f : c->attribute(open::Attribute::Strength);
}

float OpenCharacter::get_attribute(int64_t attribute_id) const {
	const open::Character *c = sheet();
	if (c == nullptr) {
		return 0.0f;
	}
	if (!enum_id_is_valid<open::Attribute>(attribute_id)) {
		report_bad_id("attribute", attribute_id);
		return 0.0f;
	}
	const open::Attribute attribute = static_cast<open::Attribute>(static_cast<int>(attribute_id));
	return c->attribute(attribute);
}

float OpenCharacter::get_attribute_base(int64_t attribute_id) const {
	const open::Character *c = sheet();
	if (c == nullptr) {
		return 0.0f;
	}
	if (!enum_id_is_valid<open::Attribute>(attribute_id)) {
		report_bad_id("attribute", attribute_id);
		return 0.0f;
	}
	const open::Attribute attribute = static_cast<open::Attribute>(static_cast<int>(attribute_id));
	return c->base(attribute);
}

float OpenCharacter::get_stamina() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0.0f : c->pool(open::PoolKind::Stamina).current;
}

float OpenCharacter::get_max_stamina() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0.0f : c->pool(open::PoolKind::Stamina).maximum;
}

float OpenCharacter::get_mana() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0.0f : c->pool(open::PoolKind::Mana).current;
}

float OpenCharacter::get_max_mana() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0.0f : c->pool(open::PoolKind::Mana).maximum;
}

float OpenCharacter::get_part_health(int64_t part_id) const {
	const open::Character *c = sheet();
	if (c == nullptr) {
		return 0.0f;
	}
	if (!enum_id_is_valid<open::BodyPart>(part_id)) {
		report_bad_id("body part", part_id);
		return 0.0f;
	}
	const open::BodyPart part = static_cast<open::BodyPart>(static_cast<int>(part_id));
	return c->body_part(part).current;
}

float OpenCharacter::get_part_max_health(int64_t part_id) const {
	const open::Character *c = sheet();
	if (c == nullptr) {
		return 0.0f;
	}
	if (!enum_id_is_valid<open::BodyPart>(part_id)) {
		report_bad_id("body part", part_id);
		return 0.0f;
	}
	const open::BodyPart part = static_cast<open::BodyPart>(static_cast<int>(part_id));
	return c->body_part(part).maximum;
}

bool OpenCharacter::is_alive() const {
	const open::Character *c = sheet();
	return c != nullptr && c->alive();
}

bool OpenCharacter::can_walk() const {
	const open::Character *c = sheet();
	return c != nullptr && c->can_walk();
}

float OpenCharacter::get_walk_speed() const {
	const open::Character *c = sheet();
	return c == nullptr ? 0.0f : c->walk_speed();
}

bool OpenCharacter::has_left_arm() const {
	const open::Character *c = sheet();
	return c != nullptr && c->body_part(open::BodyPart::LeftArm).current > 0.0f;
}

bool OpenCharacter::has_right_arm() const {
	const open::Character *c = sheet();
	return c != nullptr && c->body_part(open::BodyPart::RightArm).current > 0.0f;
}

bool OpenCharacter::has_left_leg() const {
	const open::Character *c = sheet();
	return c != nullptr && c->body_part(open::BodyPart::LeftLeg).current > 0.0f;
}

bool OpenCharacter::has_right_leg() const {
	const open::Character *c = sheet();
	return c != nullptr && c->body_part(open::BodyPart::RightLeg).current > 0.0f;
}

bool OpenCharacter::get_can_move() const {
	const open::Character *c = sheet();
	return c != nullptr && c->can_move();
}

void OpenCharacter::set_can_move(bool can_move) {
	open::Character *c = sheet();
	if (c == nullptr) {
		return;
	}
	c->set_can_move(can_move);
}

bool OpenCharacter::set_destination(const godot::Vector3 &destination) {
	open::Character *c = sheet();
	if (c == nullptr) {
		return false;
	}
	return c->set_destination(destination.x, destination.y, destination.z);
}

void OpenCharacter::clear_order() {
	open::Character *c = sheet();
	if (c == nullptr) {
		return;
	}
	c->clear_order();
}

godot::Vector3 OpenCharacter::velocity_at(const godot::Vector3 &position) const {
	const open::Character *c = sheet();
	if (c == nullptr) {
		return godot::Vector3();
	}
	const open::Vec3 v = c->velocity_at(position.x, position.y, position.z);
	return godot::Vector3(v.x, v.y, v.z);
}

bool OpenCharacter::apply_damage(int64_t part_id, float amount) {
	open::Character *c = sheet();
	if (c == nullptr) {
		return false;
	}
	if (!enum_id_is_valid<open::BodyPart>(part_id)) {
		report_bad_id("body part", part_id);
		return false;
	}
	const open::BodyPart part = static_cast<open::BodyPart>(static_cast<int>(part_id));
	return c->damage(part, amount);
}

bool OpenCharacter::regenerate(float seconds, bool in_combat) {
	open::Character *c = sheet();
	if (c == nullptr) {
		return false;
	}
	return c->regenerate(seconds, in_combat);
}
