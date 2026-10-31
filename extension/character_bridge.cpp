#include "character_bridge.hpp"

#include "character_skill_snapshot.hpp"
#include "item_bridge.hpp"
#include "technique_bridge.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "open/starter_content.hpp"

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

// Item ids are unsigned 64-bit in the simulation and signed in Godot. Every id a script can
// send that is not a positive signed integer is not one this sheet can own, and an id that
// only becomes legal after truncation is refused rather than reinterpreted.
bool item_id_is_usable(std::int64_t id) {
	return id > 0;
}

godot::Array slot_rows(const std::vector<int> &slots) {
	godot::Array rows;
	for (const int slot : slots) {
		rows.push_back(slot);
	}
	return rows;
}

godot::Array requirement_rows(const std::vector<open::RequirementSnapshotRow> &requirements) {
	godot::Array rows;
	for (const open::RequirementSnapshotRow &requirement : requirements) {
		godot::Dictionary entry;
		entry["attribute"] = requirement.attribute;
		entry["value"] = requirement.value;
		rows.push_back(entry);
	}
	return rows;
}

godot::Array enchantment_rows(const std::vector<open::EnchantmentSnapshotRow> &enchantments) {
	godot::Array rows;
	for (const open::EnchantmentSnapshotRow &enchantment : enchantments) {
		godot::Dictionary entry;
		entry["definition_id"] = static_cast<std::int64_t>(enchantment.definition);
		entry["added_blunt"] = enchantment.added_blunt;
		// -1 is "nothing asked", so a script never has to tell an absent requirement from a
		// requirement on attribute 0.
		entry["requirement_attribute"] = enchantment.requirement_attribute;
		entry["requirement_value"] = enchantment.requirement_value;
		rows.push_back(entry);
	}
	return rows;
}

godot::Array technique_rows(const std::vector<open::TechniqueSnapshotRow> &techniques) {
	godot::Array rows;
	for (const open::TechniqueSnapshotRow &technique : techniques) {
		godot::Dictionary entry;
		entry["technique_id"] = static_cast<std::int64_t>(technique.id);
		entry["defined"] = technique.defined;
		entry["available"] = technique.available;
		entry["skip_on_cooldown"] = technique.skip_on_cooldown;
		entry["required_rank"] = static_cast<std::int64_t>(technique.required_rank);
		entry["trained"] = static_cast<std::int64_t>(technique.trained);
		entry["cut"] = technique.cut;
		entry["pierce"] = technique.pierce;
		entry["blunt"] = technique.blunt;
		entry["cooldown"] = technique.cooldown;
		entry["has_stamina_cost"] = technique.has_stamina_cost;
		// Only meaningful beside `has_stamina_cost`: an absent cost means the weapon's own.
		entry["stamina_cost"] = technique.stamina_cost;
		entry["cooldown_remaining"] = technique.cooldown_remaining;
		rows.push_back(entry);
	}
	return rows;
}

// One step of a configured sequence, validated to the declared schema before any of it is
// sent to C++: a dictionary of exactly `technique_id` (int, positive) and
// `skip_on_cooldown` (bool). The types are checked rather than coerced, so a float, a string
// or an int standing in for a bool is a refusal and not a silent conversion.
bool read_technique_step(const godot::Variant &value, open::TechniqueStep &out) {
	if (value.get_type() != godot::Variant::DICTIONARY) {
		return false;
	}
	const godot::Dictionary step = value;
	if (step.size() != 2 || !step.has("technique_id") || !step.has("skip_on_cooldown")) {
		return false;
	}
	const godot::Variant id = step.get("technique_id", godot::Variant());
	const godot::Variant skip = step.get("skip_on_cooldown", godot::Variant());
	if (id.get_type() != godot::Variant::INT || skip.get_type() != godot::Variant::BOOL) {
		return false;
	}
	const std::int64_t raw_id = static_cast<std::int64_t>(id);
	if (!item_id_is_usable(raw_id)) {
		return false;
	}
	out.id = static_cast<open::TechniqueId>(raw_id);
	out.skip_on_cooldown = static_cast<bool>(skip);
	return true;
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
	godot::ClassDB::bind_method(godot::D_METHOD("get_skill_snapshot"), &OpenCharacter::get_skill_snapshot);
	godot::ClassDB::bind_method(godot::D_METHOD("set_destination", "destination"),
							   &OpenCharacter::set_destination);
	godot::ClassDB::bind_method(godot::D_METHOD("clear_order"), &OpenCharacter::clear_order);
	godot::ClassDB::bind_method(godot::D_METHOD("velocity_at", "position"), &OpenCharacter::velocity_at);
	godot::ClassDB::bind_method(godot::D_METHOD("get_attack_target_id"),
							   &OpenCharacter::get_attack_target_id);
	godot::ClassDB::bind_method(godot::D_METHOD("get_equipment_snapshot"),
							   &OpenCharacter::get_equipment_snapshot);
	godot::ClassDB::bind_method(godot::D_METHOD("get_weapon_snapshot", "item_id"),
							   &OpenCharacter::get_weapon_snapshot);
	godot::ClassDB::bind_method(
		godot::D_METHOD("equip_item", "item_id", "slot"), &OpenCharacter::equip_item);
	godot::ClassDB::bind_method(godot::D_METHOD("unequip_item", "slot"),
							   &OpenCharacter::unequip_item);
	godot::ClassDB::bind_method(godot::D_METHOD("get_technique_snapshot"),
							   &OpenCharacter::get_technique_snapshot);
	godot::ClassDB::bind_method(godot::D_METHOD("configure_techniques", "enabled", "steps"),
							   &OpenCharacter::configure_techniques);
	godot::ClassDB::bind_method(godot::D_METHOD("set_player_controlled", "controlled"),
							   &OpenCharacter::set_player_controlled);
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
	// Every sheet the scene makes is made against the one production item catalog, because
	// every sheet the scene makes can own items from it. A catalog changes no rule and no
	// number for a character that owns nothing - bare hands are still bare hands - and without
	// it an instance could not be described, an equipment row could not name its model and a
	// technique id could not be resolved. Task 15's setup command mints the starting backpack;
	// this is what makes the sheet able to read it.
	const std::optional<open::Character> made =
		open::Character::create(open::CharacterConfig{
									static_cast<std::uint64_t>(id),
									std::string(name.utf8().get_data()), team, level, blessed},
								open::make_starter_catalog());
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
	const std::optional<open::Character> made =
		open::Character::create(open::CharacterConfig{
									static_cast<std::uint64_t>(id),
									std::string(name.utf8().get_data()), team, level, blessed},
								open::make_starter_catalog());
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

godot::Array OpenCharacter::get_skill_snapshot() const {
	godot::Array rows;
	const open::Character *c = sheet();
	if (c == nullptr) {
		return rows;
	}
	for (const open::SkillSnapshotRow &row : open::skill_snapshot(*c)) {
		godot::Dictionary entry;
		entry["name"] = godot::String(row.name.c_str());
		entry["value"] = row.value;
		entry["advanced_name"] = godot::String(row.advanced_name.c_str());
		entry["advanced_value"] = row.advanced_value;
		rows.push_back(entry);
	}
	return rows;
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

int64_t OpenCharacter::get_attack_target_id() const {
	const open::Character *c = sheet();
	if (c == nullptr) {
		return 0;
	}
	const std::optional<std::uint64_t> target = c->attack_target();
	return target.has_value() ? static_cast<int64_t>(*target) : 0;
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

godot::Array OpenCharacter::get_equipment_snapshot() const {
	godot::Array rows;
	const open::Character *c = sheet();
	if (c == nullptr) {
		return rows;
	}
	for (const open::EquipmentSnapshotRow &row : open::equipment_snapshot(*c)) {
		godot::Dictionary entry;
		entry["item_id"] = static_cast<std::int64_t>(row.item);
		entry["definition_id"] = static_cast<std::int64_t>(row.definition);
		entry["model_key"] = godot::String(row.model_key.c_str());
		entry["slots"] = slot_rows(row.slots);
		entry["allowed_slots"] = slot_rows(row.allowed_slots);
		entry["hand_use"] = row.hand_use;
		entry["quality"] = static_cast<std::int64_t>(row.quality);
		entry["durability"] = static_cast<std::int64_t>(row.durability);
		entry["max_durability"] = static_cast<std::int64_t>(row.max_durability);
		entry["enchantments_used"] = static_cast<std::int64_t>(row.enchantments_used);
		entry["enchantment_capacity"] = static_cast<std::int64_t>(row.enchantment_capacity);
		entry["enchantments"] = enchantment_rows(row.enchantments);
		entry["requirements"] = requirement_rows(row.requirements);
		entry["is_weapon"] = row.is_weapon;
		entry["is_shield"] = row.is_shield;
		entry["held_usable"] = row.held_usable;
		rows.push_back(entry);
	}
	return rows;
}

godot::Dictionary OpenCharacter::get_weapon_snapshot(int64_t item_id) const {
	godot::Dictionary empty;
	const open::Character *c = sheet();
	if (c == nullptr || !item_id_is_usable(item_id)) {
		return empty;
	}
	const std::optional<open::WeaponSnapshot> weapon =
		open::weapon_snapshot(*c, static_cast<open::ItemId>(item_id));
	if (!weapon.has_value()) {
		return empty;
	}
	godot::Dictionary snapshot;
	snapshot["item_id"] = static_cast<std::int64_t>(weapon->item);
	snapshot["definition_id"] = static_cast<std::int64_t>(weapon->definition);
	snapshot["model_key"] = godot::String(weapon->model_key.c_str());
	snapshot["job_name"] = godot::String(weapon->job_name.c_str());
	snapshot["option_name"] = godot::String(weapon->option_name.c_str());
	snapshot["trained"] = static_cast<std::int64_t>(weapon->trained);
	snapshot["effectiveness"] = weapon->effectiveness;
	snapshot["cut"] = weapon->damage.cut;
	snapshot["pierce"] = weapon->damage.pierce;
	snapshot["blunt"] = weapon->damage.blunt;
	snapshot["penetration_cut"] = weapon->penetration.cut;
	snapshot["penetration_pierce"] = weapon->penetration.pierce;
	snapshot["penetration_blunt"] = weapon->penetration.blunt;
	snapshot["min_reach"] = weapon->min_reach;
	snapshot["max_reach"] = weapon->max_reach;
	snapshot["speed"] = weapon->speed;
	snapshot["stamina_cost"] = weapon->stamina_cost;
	snapshot["attack_cost"] = weapon->attack_cost;
	snapshot["block_percent"] = weapon->block_percent;
	snapshot["weight"] = weapon->weight;
	snapshot["weight_class"] = weapon->weight_class;
	snapshot["required_trained_rank"] = static_cast<std::int64_t>(weapon->required_trained_rank);
	snapshot["quality"] = static_cast<std::int64_t>(weapon->quality);
	snapshot["durability"] = static_cast<std::int64_t>(weapon->durability);
	snapshot["max_durability"] = static_cast<std::int64_t>(weapon->max_durability);
	godot::Array techniques;
	for (const std::uint64_t id : weapon->techniques) {
		techniques.push_back(static_cast<std::int64_t>(id));
	}
	snapshot["techniques"] = techniques;
	snapshot["enchantments"] = enchantment_rows(weapon->enchantments);
	snapshot["requirements"] = requirement_rows(weapon->requirements);
	return snapshot;
}

bool OpenCharacter::equip_item(int64_t item_id, int64_t slot) {
	open::Character *c = sheet();
	if (c == nullptr) {
		return false;
	}
	if (!item_id_is_usable(item_id)) {
		report_bad_id("item", item_id);
		return false;
	}
	if (!enum_id_is_valid<open::Slot>(slot)) {
		report_bad_id("slot", slot);
		return false;
	}
	// Ownership, placement, free hands and limbs are C++'s to decide, all of them, and a
	// record the sheet does not own simply is not there to equip.
	return c->equip(static_cast<open::ItemId>(item_id),
					static_cast<open::Slot>(static_cast<int>(slot)));
}

bool OpenCharacter::unequip_item(int64_t slot) {
	open::Character *c = sheet();
	if (c == nullptr) {
		return false;
	}
	if (!enum_id_is_valid<open::Slot>(slot)) {
		report_bad_id("slot", slot);
		return false;
	}
	return c->unequip(static_cast<open::Slot>(static_cast<int>(slot)));
}

godot::Dictionary OpenCharacter::get_technique_snapshot() const {
	godot::Dictionary snapshot;
	const open::Character *c = sheet();
	// The schema is answered in full even with nothing behind it: a popup that has lost its
	// sheet should show nothing, not crash looking for a key.
	snapshot["enabled"] = true;
	snapshot["player_controlled"] = false;
	snapshot["can_configure"] = false;
	snapshot["index"] = static_cast<std::int64_t>(0);
	snapshot["supported"] = godot::Array();
	snapshot["sequence"] = godot::Array();
	snapshot["cooldowns"] = godot::Array();
	if (c == nullptr) {
		return snapshot;
	}
	const open::TechniqueSnapshot state = open::technique_snapshot(*c);
	snapshot["enabled"] = state.enabled;
	snapshot["player_controlled"] = state.player_controlled;
	snapshot["can_configure"] = state.can_configure;
	snapshot["index"] = static_cast<std::int64_t>(state.index);
	snapshot["supported"] = technique_rows(state.supported);
	snapshot["sequence"] = technique_rows(state.sequence);
	snapshot["cooldowns"] = technique_rows(state.cooldowns);
	return snapshot;
}

bool OpenCharacter::configure_techniques(bool enabled, const godot::Array &steps) {
	open::Character *c = sheet();
	if (c == nullptr) {
		return false;
	}
	// Validated in full before anything is sent: a sequence with one bad entry in the middle
	// must not become the player's new sequence up to that entry.
	std::vector<open::TechniqueStep> parsed;
	parsed.reserve(static_cast<std::size_t>(steps.size()));
	for (std::int64_t index = 0; index < steps.size(); ++index) {
		open::TechniqueStep entry;
		if (!read_technique_step(steps[index], entry)) {
			return false;
		}
		parsed.push_back(entry);
	}
	return c->configure_techniques(enabled, parsed);
}

bool OpenCharacter::set_player_controlled(bool controlled) {
	open::Character *c = sheet();
	if (c == nullptr) {
		return false;
	}
	// No validation belongs here: the command carries one bool, and whether this sheet may be
	// handed over at all - a dead one may not - is C++'s rule, returned as-is.
	return c->set_player_controlled(controlled);
}
