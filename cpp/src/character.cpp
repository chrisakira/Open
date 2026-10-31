#include "open/character.hpp"

#include "open/combat_rules.hpp"
#include "open/weapon_rules.hpp"

#include <cmath>
#include <utility>

namespace open {
namespace {

bool pool_relevant_rank_changed(const Skills &before, const Skills &after) {
	return before.basic(BasicSkill::Endurance).rank != after.basic(BasicSkill::Endurance).rank ||
		   before.basic(BasicSkill::ManaControl).rank != after.basic(BasicSkill::ManaControl).rank;
}

}  // namespace

std::optional<Character> Character::create(const CharacterConfig &config) {
	return create(config, nullptr);
}

std::optional<Character> Character::create(const CharacterConfig &config,
										   std::shared_ptr<const ItemCatalog> catalog) {
	if (config.id == 0) {
		return std::nullopt;
	}
	if (!valid(config.faction) || !valid(config.level) || !valid(config.blessing)) {
		return std::nullopt;
	}

	Character made;
	made.catalog_ = std::move(catalog);
	made.id_ = config.id;
	made.name_ = config.name;
	made.faction_ = config.faction;
	made.attributes_ = Attributes(config.level, config.blessing);
	made.vitals_ = Vitals(made.attributes_, made.skills_);
	return made;
}

std::uint64_t Character::id() const {
	return id_;
}

const std::string &Character::name() const {
	return name_;
}

Faction Character::faction() const {
	return faction_;
}

int Character::level() const {
	return attributes_.level();
}

Attribute Character::blessing() const {
	return attributes_.blessing();
}

float Character::base(Attribute attribute) const {
	return attributes_.base(attribute);
}

float Character::attribute(Attribute attribute) const {
	return attributes_.effective(attribute);
}

Pool Character::pool(PoolKind kind) const {
	return vitals_.pool(kind);
}

Pool Character::body_part(BodyPart part) const {
	return vitals_.part(part);
}

Training Character::basic(BasicSkill skill) const {
	return skills_.basic(skill);
}

std::optional<Training> Character::advanced(AdvancedSkill skill) const {
	return skills_.advanced(skill);
}

std::optional<AdvancedSkill> Character::chosen(BasicSkill skill) const {
	return skills_.chosen(skill);
}

bool Character::has_title(Title title) const {
	return skills_.has_title(title);
}

unsigned Character::title_count(Title title) const {
	return skills_.title_count(title);
}

std::optional<ItemId> Character::equipped(Slot slot) const {
	return equipment_.equipped(slot);
}

std::vector<ItemId> Character::backpack_items() const {
	return equipment_.backpack_items();
}

std::optional<ItemRef> Character::item(ItemId id) const {
	return equipment_.item(id);
}

Training Character::resolve_skill(BasicSkill skill, std::optional<AdvancedSkill> option) const {
	return skills_.resolve(skill, option);
}

float Character::effective_skill(BasicSkill skill, std::optional<AdvancedSkill> option) const {
	const Training track = skills_.resolve(skill, option);
	const SkillRule &rule = skill_rule(skill);
	double link = 0.0;
	for (int i = 0; i < rule.link_count; ++i) {
		link += static_cast<double>(attributes_.effective(rule.links[static_cast<std::size_t>(i)]));
	}
	link /= static_cast<double>(rule.link_count);
	return static_cast<float>(static_cast<double>(track.rank) * link *
							  static_cast<double>(skills_.title_multiplier(skill)));
}

float Character::target_multiplier(bool target_is_dragon) const {
	return skills_.target_multiplier(target_is_dragon);
}

void Character::set_name(std::string name) {
	name_ = std::move(name);
}

bool Character::set_level(int level) {
	Attributes next = attributes_;
	if (!next.set_level(level)) {
		return false;
	}
	Vitals next_vitals = vitals_;
	if (!next_vitals.recalculate(next, skills_)) {
		return false;
	}
	attributes_ = next;
	vitals_ = next_vitals;
	return true;
}

bool Character::set_base(Attribute attribute, float base) {
	Attributes next = attributes_;
	if (!next.set_base(attribute, base)) {
		return false;
	}
	Vitals next_vitals = vitals_;
	if (!next_vitals.recalculate(next, skills_)) {
		return false;
	}
	attributes_ = next;
	vitals_ = next_vitals;
	return true;
}

bool Character::advance(AdvancedSkill skill) {
	if (!alive()) {
		return false;
	}
	return skills_.advance(skill);
}

bool Character::record_use(BasicSkill skill, std::optional<AdvancedSkill> option, bool successful) {
	if (!alive()) {
		return false;
	}
	Skills next_skills = skills_;
	if (!next_skills.record_use(skill, option, successful)) {
		return false;
	}
	if (pool_relevant_rank_changed(skills_, next_skills)) {
		Vitals next_vitals = vitals_;
		if (!next_vitals.recalculate(attributes_, next_skills)) {
			return false;
		}
		vitals_ = next_vitals;
	}
	skills_ = next_skills;
	return true;
}

bool Character::award_work_xp(const SkillJob &job, unsigned amount) {
	if (!alive() || amount == 0) {
		return false;
	}
	const Skills before = skills_;
	if (!skills_.award_work_xp(job.basic, job.option, amount)) {
		return false;
	}
	if (pool_relevant_rank_changed(before, skills_)) {
		vitals_.recalculate(attributes_, skills_);
	}
	return true;
}

bool Character::record_title_progress(Title title, bool successful) {
	return skills_.record_title_progress(title, successful);
}

bool Character::spend(ResourceUse use, float quantity) {
	return vitals_.spend(use, quantity);
}

bool Character::spend_stamina(float amount) {
	return vitals_.spend_stamina(amount);
}

bool Character::regenerate(float seconds, bool in_combat) {
	return vitals_.regenerate(seconds, in_combat, attributes_);
}

bool Character::damage(BodyPart part, float amount) {
	const bool was_alive = alive();
	if (!vitals_.damage(part, amount)) {
		return false;
	}
	// Death ends the fight: a corpse keeps no attack intent.
	if (was_alive && !alive()) {
		attack_target_.reset();
		// And it ends the bench too, paid or not: the dead cannot finish a repair, and no
		// resurrection of the schedule is on offer.
		interrupt_work(WorkFailure::Dead);
	}
	return true;
}

std::shared_ptr<const ItemCatalog> Character::catalog() const {
	return catalog_;
}

bool Character::add_item(const ItemRef &item) {
	if (!alive()) {
		return false;
	}
	return equipment_.add(item, vitals_.can_carry());
}

bool Character::add_instance(const ItemInstance &item) {
	if (!alive()) {
		return false;
	}
	return equipment_.add_instance(item, vitals_.can_carry());
}

std::optional<ItemInstance> Character::instance(ItemId id) const {
	return equipment_.instance(id);
}

bool Character::attach_enchantment(ItemId id, const AppliedEnchantment &enchantment) {
	if (!alive()) {
		return false;
	}
	return equipment_.attach_enchantment(id, enchantment);
}

bool Character::apply_wear(ItemId id, unsigned amount) {
	return equipment_.apply_wear(id, amount);
}

bool Character::restore_durability(ItemId id, unsigned amount) {
	return equipment_.restore_durability(id, amount);
}

bool Character::equip(ItemId id, Slot slot) {
	if (!alive()) {
		return false;
	}
	// A thing under the tool cannot be moved: while a paid job is repairing or enchanting
	// this very item, equipping it - to this slot or any other - is refused. The refusal is
	// not an interruption; the job carries on as if nothing was asked. Queued work has paid
	// nothing and locks nothing.
	if (work_locks(id)) {
		return false;
	}
	return equipment_.equip(id, slot, vitals_.one_hand(), vitals_.two_hands());
}

bool Character::unequip(Slot slot) {
	// Taking the target off the anvil is moving it, and moving it mid-repair is exactly what
	// the lock is for. The player finishes or cancels first.
	if (const std::optional<ItemId> worn = equipment_.equipped(slot);
		worn.has_value() && work_locks(*worn)) {
		return false;
	}
	return equipment_.unequip(slot);
}

bool Character::alive() const {
	return vitals_.alive();
}

bool Character::can_walk() const {
	return can_move_ && vitals_.leg_multiplier() > 0.0f;
}

bool Character::can_carry() const {
	return vitals_.can_carry();
}

bool Character::can_use_one_hand() const {
	return vitals_.one_hand();
}

bool Character::can_use_two_hands() const {
	return vitals_.two_hands();
}

bool Character::held_usable(ItemId id) const {
	return equipment_.held_usable(id, vitals_.one_hand(), vitals_.two_hands());
}

bool Character::can_move() const {
	return can_move_;
}

void Character::set_can_move(bool can_move) {
	can_move_ = can_move;
}

float Character::walk_speed() const {
	// Encumbrance will compose here once weights and the slowdown rule exist;
	// nothing is invented for it yet.
	if (!can_move_) {
		return 0.0f;
	}
	return kWalkingSpeed * vitals_.leg_multiplier();
}

bool Character::set_destination(float x, float y, float z) {
	if (!alive()) {
		return false;
	}
	// Validate first: a refused click must not destroy the attack the actor has.
	if (!order_.set_destination(x, y, z)) {
		return false;
	}
	attack_target_.reset();
	// A walk is a changed mind about the bench, wherever the new destination is - even the
	// square the smith is already standing on.
	interrupt_work(WorkFailure::Movement);
	return true;
}

void Character::clear_order() {
	order_.clear();
	attack_target_.reset();
	interrupt_work(WorkFailure::Movement);
}

std::optional<Vec3> Character::destination() const {
	return order_.destination();
}

Vec3 Character::velocity_at(float px, float py, float pz) const {
	if (is_stunned()) {
		// A stun holds the sheet where it stands. The order it was walking is kept, so the
		// walk resumes on its own when the stun does, which is the same rule
		// `attack_velocity_at` follows and for the same reason: being unable to act is not
		// having been stood down.
		return {};
	}
	return order_.velocity_at(px, py, pz, walk_speed());
}

bool Character::set_attack_target(std::uint64_t target_id) {
	if (!alive()) {
		return false;
	}
	if (target_id == 0 || target_id == id_) {
		return false;
	}
	// An accepted attack owns the actor's intent, so the walk goes with it - and so does the
	// bench, paid or not: swinging is not hammering.
	attack_target_ = target_id;
	order_.clear();
	interrupt_work(WorkFailure::Combat);
	return true;
}

std::optional<std::uint64_t> Character::attack_target() const {
	return attack_target_;
}

Vec3 Character::attack_velocity_at(const Vec3 &self, const Vec3 &target) const {
	if (!attack_target_.has_value() || !can_walk() || is_stunned()) {
		// A stun holds the character where they are and does not take the order away:
		// the walk resumes on its own when the stun does.
		return {};
	}
	// The distance this sheet is trying to close is its weapon's own reach, read now:
	// a spear walks to 2.0 and a dagger to 1.0, and the sheet with both is walking with
	// whichever one it would swing.
	const std::optional<WeaponAttackProfile> attack = weapon_attack_profile(*this);
	if (!attack.has_value()) {
		return {};
	}
	// One rule for both questions: the walk aims at the point this weapon's swing will
	// accept, so it cannot stop somewhere the attack then refuses, or walk past a target
	// the attack already has.
	return pursuit_velocity(self, target, attack->stats.max_reach, walk_speed());
}

}  // namespace open
