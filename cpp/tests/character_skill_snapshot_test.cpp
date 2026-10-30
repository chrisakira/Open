#include "open/character.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../../extension/character_skill_snapshot.hpp"
#include "test_support.hpp"

// The skill snapshot the popup consumes. Break these catches: a snapshot that
// drops rows or reorders them, exposes an unchosen advanced name, shows ranks
// instead of effective values, lets level or blessing miss the numbers, or
// reports a chosen specialization as something other than its own effective
// value (including zero).

namespace {

open::Character make_sheet(std::uint64_t id, int level, open::Attribute blessing) {
	open::CharacterConfig made_config;
	made_config.id = id;
	made_config.name = "Snapshot";
	made_config.faction = open::Faction::Friendly;
	made_config.level = level;
	made_config.blessing = blessing;
	const std::optional<open::Character> made = open::Character::create(made_config);
	expect_true("the test sheet is created", made.has_value());
	return *made;
}

const open::SkillSnapshotRow *find_row(const std::vector<open::SkillSnapshotRow> &rows,
									   const char *name) {
	for (const open::SkillSnapshotRow &row : rows) {
		if (row.name == name) {
			return &row;
		}
	}
	return nullptr;
}

void expect_no_chosen(const char *name, const std::vector<open::SkillSnapshotRow> &rows) {
	for (const open::SkillSnapshotRow &row : rows) {
		if (!row.advanced_name.empty() || row.advanced_value != 0.0f) {
			std::cerr << "FAIL " << name << ": " << row.name << " reports an unchosen "
					  << "advanced option\n";
			g_failed++;
			return;
		}
	}
}

}  // namespace

int main() {
	// A fresh level-1 Strength-blessed sheet: every basic at rank 1, no choices.
	open::Character blessed = make_sheet(1, 1, open::Attribute::Strength);
	const std::vector<open::SkillSnapshotRow> rows = open::skill_snapshot(blessed);
	expect_true("the snapshot has 35 rows", rows.size() == 35u);
	for (int i = 0; i < static_cast<int>(open::BasicSkill::Count) && i < static_cast<int>(rows.size()); ++i) {
		const std::string want = open::skill_rule(static_cast<open::BasicSkill>(i)).name;
		if (rows[static_cast<std::size_t>(i)].name != want) {
			std::cerr << "FAIL the snapshot is in BasicSkill order: row " << i << " is "
					  << rows[static_cast<std::size_t>(i)].name << ", want " << want << "\n";
			g_failed++;
		}
	}
	const open::SkillSnapshotRow *one_handed = find_row(rows, "One-Handed Weapons");
	expect_true("One-Handed Weapons is present", one_handed != nullptr);
	if (one_handed != nullptr) {
		// rank 1 x average(1.25 blessed Strength, 1.0 Dexterity)
		expect_close("One-Handed Weapons is the effective value", one_handed->value, 1.125f);
	}
	const open::SkillSnapshotRow *dodging = find_row(rows, "Dodging");
	expect_true("Dodging is present", dodging != nullptr);
	if (dodging != nullptr) {
		expect_close("Dodging is the effective value", dodging->value, 1.0f);
	}
	expect_no_chosen("a fresh sheet", rows);

	// Train One-Handed to 100, choose Katana (which starts at rank 1), then train
	// Katana to rank 2.
	for (unsigned uses = 0u; blessed.basic(open::BasicSkill::OneHandedWeapons).rank < open::kBasicRankCap;
		 ++uses) {
		if (uses > 100000u) {
			expect_true("the training loop terminates", false);
			break;
		}
		blessed.record_use(open::BasicSkill::OneHandedWeapons, std::nullopt, true);
	}
	expect_true("advance into Katana is accepted", blessed.advance(open::AdvancedSkill::Katana));
	const std::optional<open::Training> fresh_katana = blessed.advanced(open::AdvancedSkill::Katana);
	expect_true("the unlocked track starts at rank 1 with no XP",
				fresh_katana.has_value() && fresh_katana->rank == 1u && fresh_katana->xp == 0u);
	const std::vector<open::SkillSnapshotRow> chosen_rows = open::skill_snapshot(blessed);
	const open::SkillSnapshotRow *chosen_row = find_row(chosen_rows, "One-Handed Weapons");
	expect_true("the trained sheet still has the One-Handed row", chosen_row != nullptr);
	if (chosen_row != nullptr) {
		expect_true("the chosen option is named", chosen_row->advanced_name == "Katana");
		expect_close("the frozen basic keeps its own effective value", chosen_row->value, 112.5f);
		expect_close("a fresh specialization shows its rank-one effective value",
					 chosen_row->advanced_value, 1.125f);
	}
	for (unsigned uses = 0u; uses < 8u; ++uses) {
		blessed.record_use(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana, true);
	}
	expect_true("the unlocked track has now paid its second rank",
				blessed.advanced(open::AdvancedSkill::Katana)->rank == 2u);
	const std::vector<open::SkillSnapshotRow> trained_rows = open::skill_snapshot(blessed);
	const open::SkillSnapshotRow *trained_row = find_row(trained_rows, "One-Handed Weapons");
	if (trained_row != nullptr) {
		expect_close("the trained specialization shows its own effective value",
					 trained_row->advanced_value, 2.25f);
		expect_close("the basic row is still its own effective value", trained_row->value, 112.5f);
	}
	for (const open::SkillSnapshotRow &row : trained_rows) {
		if (row.name == "One-Handed Weapons") {
			continue;
		}
		if (!row.advanced_name.empty() || row.advanced_value != 0.0f) {
			std::cerr << "FAIL only the chosen parent reports an advanced option: " << row.name
					  << " does too\n";
			g_failed++;
			break;
		}
	}

	// Level 7 with a non-Strength blessing: level and blessing ride into the
	// displayed effective values.
	open::Character veteran = make_sheet(2, 7, open::Attribute::Profanity);
	const std::vector<open::SkillSnapshotRow> veteran_rows = open::skill_snapshot(veteran);
	expect_true("the veteran snapshot has 35 rows", veteran_rows.size() == 35u);
	const open::SkillSnapshotRow *veteran_one_handed = find_row(veteran_rows, "One-Handed Weapons");
	if (veteran_one_handed != nullptr) {
		// rank 1 x average(100 Strength, 100 Dexterity); Profanity is not a link here.
		expect_close("level 7 lifts an unblessed two-link skill", veteran_one_handed->value, 100.0f);
	}
	const open::SkillSnapshotRow *veteran_elemental = find_row(veteran_rows, "Elemental Magic");
	if (veteran_elemental != nullptr) {
		// rank 1 x 125 effective Profanity (level 100 x blessing 1.25).
		expect_close("the non-Strength blessing rides into its linked skill",
					 veteran_elemental->value, 125.0f);
	}
	const open::SkillSnapshotRow *veteran_dodging = find_row(veteran_rows, "Dodging");
	if (veteran_dodging != nullptr) {
		expect_close("level 7 lifts a single-link skill", veteran_dodging->value, 100.0f);
	}
	expect_no_chosen("the veteran sheet", veteran_rows);

	return test_summary();
}
