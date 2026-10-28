#include "open/skills.hpp"

#include <cstddef>
#include <stdexcept>

namespace open {
namespace {

std::size_t basic_slot(BasicSkill skill) {
	return static_cast<std::size_t>(static_cast<int>(skill));
}

std::size_t advanced_slot(AdvancedSkill skill) {
	return static_cast<std::size_t>(static_cast<int>(skill));
}

std::size_t title_slot(Title title) {
	return static_cast<std::size_t>(static_cast<int>(title));
}

}  // namespace

unsigned basic_xp_to_next(unsigned rank) {
	if (rank < 1u || rank > kBasicRankCap) {
		throw std::invalid_argument("basic_xp_to_next: rank must be 1..100");
	}
	if (rank == kBasicRankCap) {
		return 0u;
	}
	return 8u * rank;
}

unsigned advanced_xp_to_next(unsigned rank) {
	if (rank > kAdvancedRankCap) {
		throw std::invalid_argument("advanced_xp_to_next: rank must be 0..200");
	}
	if (rank == kAdvancedRankCap) {
		return 0u;
	}
	if (rank < 100u) {
		return 8u * (rank < 1u ? 1u : rank);
	}
	return 40u * rank;
}

Skills::Skills() {
	for (Training &entry : basics_) {
		entry = Training{1u, 0u};
	}
	for (Training &entry : advanced_) {
		entry = Training{0u, 0u};
	}
}

Training Skills::basic(BasicSkill skill) const {
	if (!valid(skill)) {
		throw std::invalid_argument("Skills::basic: unknown basic skill id");
	}
	return basics_[basic_slot(skill)];
}

std::optional<Training> Skills::advanced(AdvancedSkill skill) const {
	if (!valid(skill)) {
		throw std::invalid_argument("Skills::advanced: unknown advanced skill id");
	}
	const BasicSkill parent = advanced_rule(skill).parent;
	if (chosen_[basic_slot(parent)] != skill) {
		return std::nullopt;
	}
	return advanced_[advanced_slot(skill)];
}

std::optional<AdvancedSkill> Skills::chosen(BasicSkill skill) const {
	if (!valid(skill)) {
		throw std::invalid_argument("Skills::chosen: unknown basic skill id");
	}
	return chosen_[basic_slot(skill)];
}

Training Skills::resolve(BasicSkill skill, std::optional<AdvancedSkill> option) const {
	if (!valid(skill)) {
		throw std::invalid_argument("Skills::resolve: unknown basic skill id");
	}
	if (option.has_value()) {
		if (!valid(*option)) {
			throw std::invalid_argument("Skills::resolve: unknown advanced skill id");
		}
		if (advanced_rule(*option).parent != skill) {
			throw std::invalid_argument("Skills::resolve: option belongs to another basic skill");
		}
		if (chosen_[basic_slot(skill)] == *option) {
			return advanced_[advanced_slot(*option)];
		}
	}
	return basics_[basic_slot(skill)];
}

bool Skills::advance(AdvancedSkill skill) {
	if (!valid(skill)) {
		return false;
	}
	const BasicSkill parent = advanced_rule(skill).parent;
	if (basics_[basic_slot(parent)].rank < kBasicRankCap) {
		return false;
	}
	if (chosen_[basic_slot(parent)].has_value()) {
		return false;
	}
	chosen_[basic_slot(parent)] = skill;
	advanced_[advanced_slot(skill)] = Training{0u, 0u};
	return true;
}

bool Skills::record_use(BasicSkill skill, std::optional<AdvancedSkill> option, bool successful) {
	if (!valid(skill)) {
		return false;
	}
	if (option.has_value()) {
		if (!valid(*option) || advanced_rule(*option).parent != skill) {
			return false;
		}
	}
	if (!successful) {
		return true;
	}

	const bool trained = option.has_value() && chosen_[basic_slot(skill)] == *option;
	Training &entry = trained ? advanced_[advanced_slot(*option)] : basics_[basic_slot(skill)];
	const unsigned cap = trained ? kAdvancedRankCap : kBasicRankCap;
	if (entry.rank >= cap) {
		return true;  // a capped track discards the progress
	}
	const unsigned cost = trained ? advanced_xp_to_next(entry.rank) : basic_xp_to_next(entry.rank);
	entry.xp += 1u;
	if (entry.xp >= cost) {
		entry.xp -= cost;
		entry.rank += 1u;
	}
	return true;
}

bool Skills::record_title_progress(Title title, bool successful) {
	if (!valid(title)) {
		return false;
	}
	if (!successful) {
		return true;
	}
	const unsigned threshold = title_rule(title).threshold;
	if (titles_[title_slot(title)] >= threshold) {
		return true;  // saturated; an earned title never drops
	}
	titles_[title_slot(title)] += 1u;
	return true;
}

unsigned Skills::title_count(Title title) const {
	if (!valid(title)) {
		throw std::invalid_argument("Skills::title_count: unknown title id");
	}
	return titles_[title_slot(title)];
}

bool Skills::has_title(Title title) const {
	if (!valid(title)) {
		throw std::invalid_argument("Skills::has_title: unknown title id");
	}
	return titles_[title_slot(title)] >= title_rule(title).threshold;
}

float Skills::title_multiplier(BasicSkill skill) const {
	if (!valid(skill)) {
		throw std::invalid_argument("Skills::title_multiplier: unknown basic skill id");
	}
	float multiplier = 1.0f;
	for (int t = 0; t < static_cast<int>(Title::Count); ++t) {
		const Title title = static_cast<Title>(t);
		const std::optional<BasicSkill> &named = title_rule(title).skill;
		if (named.has_value() && named.value() == skill && has_title(title)) {
			multiplier *= kTitleSkillMultiplier;
		}
	}
	return multiplier;
}

float Skills::target_multiplier(bool target_is_dragon) const {
	if (target_is_dragon && has_title(Title::Dragonslayer)) {
		return kSpecialistTargetMultiplier;
	}
	return 1.0f;
}

}  // namespace open
