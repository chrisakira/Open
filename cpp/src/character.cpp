#include "open/character.hpp"

#include <utility>

namespace open {
namespace {

bool pool_relevant_rank_changed(const Skills &before, const Skills &after) {
	return before.basic(BasicSkill::Endurance).rank != after.basic(BasicSkill::Endurance).rank ||
		   before.basic(BasicSkill::ManaControl).rank != after.basic(BasicSkill::ManaControl).rank;
}

}  // namespace

std::optional<Character> Character::create(const CharacterConfig &config) {
	if (config.id == 0) {
		return std::nullopt;
	}
	if (!valid(config.faction) || !valid(config.level) || !valid(config.blessing)) {
		return std::nullopt;
	}

	Character made;
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

bool Character::record_title_progress(Title title, bool successful) {
	return skills_.record_title_progress(title, successful);
}

bool Character::spend(ResourceUse use, float quantity) {
	return vitals_.spend(use, quantity);
}

bool Character::regenerate(float seconds, bool in_combat) {
	return vitals_.regenerate(seconds, in_combat, attributes_);
}

bool Character::damage(BodyPart part, float amount) {
	return vitals_.damage(part, amount);
}

bool Character::add_item(const ItemRef &item) {
	if (!alive()) {
		return false;
	}
	return equipment_.add(item, vitals_.can_carry());
}

bool Character::equip(ItemId id, Slot slot) {
	if (!alive()) {
		return false;
	}
	return equipment_.equip(id, slot, vitals_.one_hand(), vitals_.two_hands());
}

bool Character::unequip(Slot slot) {
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
	return order_.set_destination(x, y, z);
}

void Character::clear_order() {
	order_.clear();
}

std::optional<Vec3> Character::destination() const {
	return order_.destination();
}

Vec3 Character::velocity_at(float px, float py, float pz) const {
	return order_.velocity_at(px, py, pz, walk_speed());
}

}  // namespace open
