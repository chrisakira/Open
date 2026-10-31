#pragma once

#include "open/attributes.hpp"
#include "open/character_rules.hpp"
#include "open/combat_rules.hpp"
#include "open/equipment.hpp"
#include "open/item_instance.hpp"
#include "open/move_order.hpp"
#include "open/skills.hpp"
#include "open/stagger.hpp"
#include "open/techniques.hpp"
#include "open/vitals.hpp"
#include "open/work.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <span>
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
	// Settlement-only: pays a completed work action's progress on the job it resolved
	// to. Not exposed to the bridge, so no script can train a skill with an arbitrary
	// number, and a dead sheet earns nothing.
	bool award_work_xp(const SkillJob &job, unsigned amount);
	bool spend(ResourceUse use, float quantity = 1.0f);
	bool spend_stamina(float amount);
	bool regenerate(float seconds, bool in_combat);
	bool damage(BodyPart part, float amount);
	bool add_item(const ItemRef &item);
	bool add_instance(const ItemInstance &item);
	std::optional<ItemInstance> instance(ItemId id) const;
	// Durability changes only through the sheet that owns the record, and only on
	// a record that has any: a placement-only item refuses both.
	bool apply_wear(ItemId id, unsigned amount);
	// Fixes an enchantment to an item this sheet owns. Settlement-only: the bench
	// action that pays for the work is Task 16's, and nothing in the bridge reaches
	// either call.
	bool attach_enchantment(ItemId id, const AppliedEnchantment &enchantment);
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
	// One step for this sheet's clocks. With no stun running that is the whole
	// elapsed time; once stunned, the clocks freeze and only the unstunned remainder of
	// a step that crosses the end counts, so a long tick cannot buy back a swing.
	bool advance_combat_clocks(float seconds);

	// How close the next staggering blow is, and whether one has already landed. Reads
	// only: nothing outside the simulation can stun a character or take a stun away.
	double stagger_buildup() const;
	double stun_remaining() const;
	bool is_stunned() const;
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

	// --- techniques -----------------------------------------------------------
	//
	// The player's technique configuration, and who is driving it. `technique_state()` is
	// what a popup reads; these two are the only ways to change it.
	//
	// Configuring writes the saved sequence and the enabled switch. It is an edit, so the
	// sequence restarts at its first entry - and it is nothing else: the clocks the last
	// fight started keep running, and the controller is whoever the last handover left.
	// Refused, changing nothing, for an entry that names no technique (id 0), and for any
	// change to a dead sheet, which has no popup and no next attack.
	bool configure_techniques(bool enabled, std::span<const TechniqueStep> sequence);
	// Handing the sheet to the player or back to the simulation. The sequence, its position
	// and every clock survive the swap: switching who chooses does not refund what was
	// chosen. Refused for a dead sheet.
	bool set_player_controlled(bool player_controlled);
	TechniqueState technique_state() const;

	// --- work -------------------------------------------------------------------
	//
	// One bench job at a time, as a copy: who is walking, who is paying the fifteen seconds,
	// what was ranked and sampled at the start, and what the last finished job became. The
	// schedule itself is private - the session advances it while it holds the frame, and the
	// order setters interrupt it as a side effect of doing their own job - because a work
	// queue you can push from the outside is a work queue with a cheat in it.
	WorkStatus work_status() const;

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
	// The threshold the next blow is measured against: 10 x effective Toughness, asked
	// for when a hit is resolved so an attribute change takes effect on the next
	// exchange and never retroactively. Private because a threshold is not a lever:
	// nothing outside the sheet can stun anything with it.
	double stagger_threshold() const;

	// Clears the meter and the stun together. Used when a sheet dies, so a corpse holds
	// neither, and never as a cure: nothing outside the simulation can take a stun away.
	void clear_stagger();

	// Blunt is remembered here, not in Vitals: a stun is a pause with a schedule, and
	// it neither heals nor injures. The hits that fill it arrive with the typed switch.
	StaggerState stagger_;
	// What this character has configured, who is driving it, and what it owes. On the sheet
	// and not on the weapon: swapping a sword does not reset a technique clock, and neither
	// does switching control or reordering the sequence. Advanced by
	// `advance_combat_clocks`, which is the one clock owner, frozen by a stun with the rest.
	TechniqueState techniques_;

	// Writes back the technique clocks after one step of time, in the order they were
	// handed over, and forgets the ones that reached ready.
	void apply_ran_technique_clocks(std::vector<double> ran);
	float attack_cooldown_ = 0.0f;
	float defend_cooldown_ = 0.0f;

	// The sheet's one bench job, with everything the start sampled. Private because a work
	// schedule is an order and a transaction at once: only the session that holds the frame
	// advances it, and only this sheet's own order setters interrupt it.
	WorkRuntime work_;

	// The work engine, kept behind the sheet: `begin_work` and `queue_work` park an already
	// validated request (started now, or on arrival), `advance_work` is the whole per-step
	// life of the job - arrival start, the combat reading of this frame, time, settle, and
	// the stun that freezes it like every other clock - and `interrupt_work` ends a busy
	// job for a reason, free while queued and paid-for while active. All four are the
	// session's to call; `work_status()` above is everybody else's to read.
	bool begin_work(const WorkRequest &request, const Vec3 &self_position, WorkContext &env);
	bool queue_work(const WorkRequest &request);
	bool advance_work(float seconds, const Vec3 &self_position, WorkContext &env, bool engaged);
	bool interrupt_work(WorkFailure reason);
	// True while this id is the item a paid job is working on: the lock the equip path
	// consults so a thing under the tool cannot be worn elsewhere or taken away mid-repair.
	bool work_locks(ItemId id) const;

	friend class CombatSession;
};

}  // namespace open
