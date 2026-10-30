#pragma once

#include "open/character_rules.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace open {

// Trained rank plus the residual XP toward the next rank point.
struct Training {
	unsigned rank;
	unsigned xp;
};

inline constexpr unsigned kBasicRankCap = 100u;
inline constexpr unsigned kAdvancedRankCap = 200u;

// XP needed to raise a basic rank from n to n+1. Throws for a rank outside
// 1..100; returns 0 at the cap.
unsigned basic_xp_to_next(unsigned rank);

// XP needed to raise an advanced rank from n to n+1. The curve resets below
// rank 100 and steepens above it. Throws for a rank outside 0..200; 0 at the
// cap. Rank 0 is answered as arithmetic only: a chosen track starts at 1.
unsigned advanced_xp_to_next(unsigned rank);

// Every basic skill has its own counter. A basic may be advanced once, into one
// of its options; the chosen option then has its own track, rank 1..200, and the
// frozen basic covers every other job under it.
class Skills {
public:
	Skills();

	// Checked getters: throw std::invalid_argument for a bad id.
	Training basic(BasicSkill skill) const;
	// nullopt while the option has not been chosen; once chosen, rank 1..200.
	std::optional<Training> advanced(AdvancedSkill skill) const;
	std::optional<AdvancedSkill> chosen(BasicSkill skill) const;

	// Which track a job actually uses. Throws for a bad id or an option that
	// belongs to a different parent.
	Training resolve(BasicSkill skill, std::optional<AdvancedSkill> option) const;

	// Requires the parent basic at 100 and no existing choice for that parent.
	// On success the chosen track starts at rank 1 with zero residual XP, so the
	// job it overrides is usable at once; it then grows to 200.
	bool advance(AdvancedSkill skill);

	// One successful use adds one residual XP to the track the job resolves to.
	// Failures and capped tracks change nothing but are still accepted.
	bool record_use(BasicSkill skill, std::optional<AdvancedSkill> option, bool successful);

	// Pays a completed action's progress on the track its job resolves to, with the
	// same curve, the same carry, and the same discard-at-cap as record_use. This is
	// the seam the workbench settlement uses; there is no amount the sheet can be told
	// to grant from a script, and an ordinary use is still exactly one point.
	// Invalid jobs and a zero amount are refused without changing anything.
	bool award_work_xp(BasicSkill skill, std::optional<AdvancedSkill> option, unsigned amount);

	// The authoritative action caller reports each relevant achievement once.
	// Counters saturate at the threshold; an earned title never drops, and a
	// title report never grants skill XP.
	bool record_title_progress(Title title, bool successful);
	unsigned title_count(Title title) const;
	bool has_title(Title title) const;

	// Product of every held title that names this skill (×1.1 each).
	float title_multiplier(BasicSkill skill) const;

	// Specialist titles change what a skill does against a target, not the skill
	// itself, so they are asked about separately.
	float target_multiplier(bool target_is_dragon) const;

private:
	std::array<Training, static_cast<std::size_t>(BasicSkill::Count)> basics_{};
	std::array<Training, static_cast<std::size_t>(AdvancedSkill::Count)> advanced_{};
	std::array<std::optional<AdvancedSkill>, static_cast<std::size_t>(BasicSkill::Count)> chosen_{};
	std::array<unsigned, static_cast<std::size_t>(Title::Count)> titles_{};
};

}  // namespace open
