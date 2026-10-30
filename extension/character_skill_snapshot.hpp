#pragma once

#include "open/character.hpp"
#include "open/character_rules.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// A read-only, Godot-independent view of one character's effective skill lines,
// for the character popup. It lives with the bridge because it exists only to
// feed presentation, and the bridge is the only consumer. Names come from the
// locked rule tables; nothing here duplicates a rule and nothing here mutates.

namespace open {

struct SkillSnapshotRow {
	std::string name;
	float value;
	// Empty when the basic has no chosen specialization; the advanced track's
	// effective value otherwise. A chosen track starts at rank 1, so this is
	// never the zero an unchosen option leaves here.
	std::string advanced_name;
	float advanced_value;
};

// One row per basic skill, in BasicSkill order.
inline std::vector<SkillSnapshotRow> skill_snapshot(const Character &character) {
	std::vector<SkillSnapshotRow> rows;
	rows.resize(static_cast<std::size_t>(BasicSkill::Count));
	for (std::size_t i = 0; i < rows.size(); ++i) {
		const BasicSkill basic = static_cast<BasicSkill>(i);
		SkillSnapshotRow row;
		row.name = skill_rule(basic).name;
		row.value = character.effective_skill(basic);
		const std::optional<AdvancedSkill> chosen = character.chosen(basic);
		if (chosen.has_value()) {
			row.advanced_name = advanced_rule(*chosen).name;
			row.advanced_value = character.effective_skill(basic, *chosen);
		} else {
			row.advanced_value = 0.0f;
		}
		rows[i] = std::move(row);
	}
	return rows;
}

}  // namespace open
