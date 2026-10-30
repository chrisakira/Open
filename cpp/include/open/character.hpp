#pragma once

#include "open/attributes.hpp"
#include "open/character_rules.hpp"
#include "open/combat_rules.hpp"
#include "open/equipment.hpp"
#include "open/item_instance.hpp"
#include "open/move_order.hpp"
#include "open/skills.hpp"
#include "open/vitals.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
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

	// The same sheet, able to name what its items are: the catalog is the shared,
	// immutable description set, kept alive by every character that reads it. A
	// null catalog is legal and means the sheet can hold placement-only records.
	static std::optional<Character> create(const CharacterConfig &config,
										   std::shared_ptr<const ItemCatalog> catalog);

	// The definitions this sheet was made against, or nullptr.
	std::shared_ptr<const ItemCatalog> catalog() const;

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
	std::optional<ItemRef> item(ItemId id) const;

	// The trained rank a job actually uses: the chosen specialisation when the
	// caller names that job, otherwise the (frozen) basic. Forwards Skills::resolve,
	// so combat never re-implements the track choice.
	Training resolve_skill(BasicSkill skill, std::optional<AdvancedSkill> option) const;

	// resolved rank x average(linked effective attributes) x parent title bonus.
	// Level is already inside the effective attributes, so it is never applied
	// twice.
	float effective_skill(BasicSkill skill, std::optional<AdvancedSkill> option = std::nullopt) const;
	float target_multiplier(bool target_is_dragon) const;

	void set_name(std::string name);
	bool set_level(int level);
	bool set_base(Attribute attribute, float base);
	// Unlocks one specialization of a basic that is frozen at 100. The chosen
	// track starts at rank 1 with no residual XP, so the job is usable at once.
	bool advance(AdvancedSkill skill);
	bool record_use(BasicSkill skill, std::optional<AdvancedSkill> option, bool successful);
	bool record_title_progress(Title title, bool successful);
	bool spend(ResourceUse use, float quantity = 1.0f);
	bool regenerate(float seconds, bool in_combat);
	bool damage(BodyPart part, float amount);
	bool add_item(const ItemRef &item);
	bool add_instance(const ItemInstance &item);
	std::optional<ItemInstance> instance(ItemId id) const;
	// Durability changes only through the sheet that owns the record, and only on
	// a record that has any: a placement-only item refuses both.
	bool apply_wear(ItemId id, unsigned amount);
	bool restore_durability(ItemId id, unsigned amount);
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

	// The attack order is only an intent: the target ID, never a pointer or a
	// position. Zero, self and a dead actor are refused without touching the
	// order the actor already has; resolving the ID against a roster is a later
	// task's job.
	bool set_attack_target(std::uint64_t target_id);
	std::optional<std::uint64_t> attack_target() const;

	// Pursuit for an attack order: zero when there is no attack, when the target
	// is already in melee reach, or when walking is disabled. Out of reach it
	// walks toward the near side of the target, so the 0.4 arrival band of
	// ordinary point movement lands at melee range. Ordinary velocity_at stays
	// the move-order query.
	Vec3 attack_velocity_at(const Vec3 &self, const Vec3 &target) const;

	// Each sheet owns its own attack and defence clocks, in seconds still to
	// wait: both start ready at 0, only time moves them, and an order change
	// never resets one. Advancing by a finite nonnegative duration is accepted;
	// anything else (negative, NaN, infinite) is refused without mutating, so a
	// bad frame cannot make a character ready or cooldown-struck.
	bool advance_combat_clocks(float seconds);
	float attack_cooldown_remaining() const;
	float defend_cooldown_remaining() const;

	// One melee attempt against `defender`, at the positions the caller observed.
	// The actor must hold an attack order on exactly this defender's id: nothing
	// here resolves a roster. An empty result means no attempt was made - wrong
	// or missing order, either sheet dead, out of reach, no usable attack, or
	// this attack clock still running - and a non-attempt spends, trains and
	// damages nothing, including the defender's clock. An attempt pays 8 stamina
	// when it can and is exhausted for that one swing when it cannot, spends a
	// ready defence whether or not it wins the nullify test, applies positive
	// damage to one sampled part through damage(), then starts the attack clock.
	// Skill XP, Wall progress and a single Duelist/Breaker kill record are the
	// attempt's only other effects; no regeneration and no new target.
	std::optional<SwingResult> try_melee_swing(Character &defender, const Vec3 &self_position,
											   const Vec3 &target_position, std::mt19937_64 &rng);

private:
	Character() = default;

	std::uint64_t id_ = 0;
	std::string name_;
	Faction faction_ = Faction::Friendly;
	Attributes attributes_ = Attributes(1, Attribute::Strength);
	Skills skills_;
	Vitals vitals_ = Vitals(attributes_, skills_);
	Equipment equipment_;
	std::shared_ptr<const ItemCatalog> catalog_;
	bool can_move_ = true;
	MoveOrder order_;
	std::optional<std::uint64_t> attack_target_;
	// Seconds still to wait, per sheet and per role: 0 is ready. A defender's
	// clock belongs to the defender, so two characters fighting each other are
	// never gated by one shared timer.
	float attack_cooldown_ = 0.0f;
	float defend_cooldown_ = 0.0f;
};

}  // namespace open
