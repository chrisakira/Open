#pragma once

#include "open/attributes.hpp"
#include "open/character_rules.hpp"
#include "open/equipment.hpp"
#include "open/move_order.hpp"
#include "open/skills.hpp"
#include "open/vitals.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace open {

// Everything the simulation needs to know about one actor. Player and NPC are
// the same type: the player is simply the character orders are issued to.
struct CharacterConfig {
	std::uint64_t id = 0;
	std::string name;
	Faction faction = Faction::Friendly;
	int level = 1;
	Attribute blessing = Attribute::Strength;
};

class Character {
public:
	// The only way to make a character: an invalid sheet cannot exist. IDs are
	// unique by contract with the future world, not checked here.
	static std::optional<Character> create(const CharacterConfig &config);

	std::uint64_t id() const;
	const std::string &name() const;
	Faction faction() const;
	int level() const;
	Attribute blessing() const;

	// Checked getters throw std::invalid_argument for a bad id, exactly as the
	// modules they forward to do. The Godot bridge checks ids first.
	float base(Attribute attribute) const;
	float attribute(Attribute attribute) const;
	Pool pool(PoolKind kind) const;
	Pool body_part(BodyPart part) const;
	Training basic(BasicSkill skill) const;
	std::optional<Training> advanced(AdvancedSkill skill) const;
	std::optional<AdvancedSkill> chosen(BasicSkill skill) const;
	bool has_title(Title title) const;
	unsigned title_count(Title title) const;
	std::optional<ItemId> equipped(Slot slot) const;
	std::vector<ItemId> backpack_items() const;

	// resolved rank x average(linked effective attributes) x parent title bonus.
	// Level is already inside the effective attributes, so it is never applied
	// twice.
	float effective_skill(BasicSkill skill, std::optional<AdvancedSkill> option = std::nullopt) const;
	float target_multiplier(bool target_is_dragon) const;

	void set_name(std::string name);
	bool set_level(int level);
	bool set_base(Attribute attribute, float base);
	bool advance(AdvancedSkill skill);
	bool record_use(BasicSkill skill, std::optional<AdvancedSkill> option, bool successful);
	bool record_title_progress(Title title, bool successful);
	bool spend(ResourceUse use, float quantity = 1.0f);
	bool regenerate(float seconds, bool in_combat);
	bool damage(BodyPart part, float amount);
	bool add_item(const ItemRef &item);
	bool equip(ItemId id, Slot slot);
	bool unequip(Slot slot);

	bool alive() const;
	bool can_walk() const;
	bool can_carry() const;
	bool can_use_one_hand() const;
	bool can_use_two_hands() const;
	bool held_usable(ItemId id) const;

	// The raw switch stays separate: legs at zero or death override it, and
	// turning it on cannot bring a character back.
	bool can_move() const;
	void set_can_move(bool can_move);
	float walk_speed() const;

	bool set_destination(float x, float y, float z);
	void clear_order();
	std::optional<Vec3> destination() const;
	Vec3 velocity_at(float px, float py, float pz) const;

private:
	Character() = default;

	std::uint64_t id_ = 0;
	std::string name_;
	Faction faction_ = Faction::Friendly;
	Attributes attributes_ = Attributes(1, Attribute::Strength);
	Skills skills_;
	Vitals vitals_ = Vitals(attributes_, skills_);
	Equipment equipment_;
	bool can_move_ = true;
	MoveOrder order_;
};

}  // namespace open
