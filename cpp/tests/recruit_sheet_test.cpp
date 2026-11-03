#include "open/character.hpp"
#include "open/character_rules.hpp"

#include <cstdint>
#include <optional>

#include "test_support.hpp"

// Task 1 recruit-sheet tests. Break these catches: a sheet that starts out
// recruitable, a corpse that accepts a recruit flag, an AlwaysSucceed that quietly
// joins the squad, and a condition that changes faction or membership on its own.
// Task 2 owns the join itself; nothing here may apply it.

namespace {

open::CharacterConfig config(std::uint64_t id, open::Faction faction) {
	open::CharacterConfig made;
	made.id = id;
	made.name = "";
	made.faction = faction;
	made.level = 1;
	made.blessing = open::Attribute::Strength;
	return made;
}

open::Character make_character(std::uint64_t id = 1,
							   open::Faction faction = open::Faction::Friendly) {
	const std::optional<open::Character> made = open::Character::create(config(id, faction));
	expect_true("the test character is created", made.has_value());
	return *made;
}

// A torso drained to zero is death, the same route the attack-order tests use.
void kill(open::Character &who) {
	expect_true("a torso hit lands", who.damage(open::BodyPart::Torso, 50.0f));
	expect_true("dead", !who.alive());
}

}  // namespace

int main() {
	{
		// create_defaults_are_not_recruitable: nobody starts out asking to be
		// recruited, and the default condition is the one that cannot join.
		open::Character friendly = make_character(1, open::Faction::Friendly);
		expect_true("a fresh sheet is not recruitable", !friendly.recruitable());
		expect_true("a fresh condition always fails",
					friendly.recruit_condition() == open::RecruitCondition::AlwaysFail);
		expect_true("a fresh sheet is not in a squad", !friendly.in_squad());
		expect_true("a fresh condition does not pass", !friendly.recruit_condition_passes());
		expect_true("and the faction is what it was configured as",
					friendly.faction() == open::Faction::Friendly);

		open::Character hostile = make_character(2, open::Faction::Enemy);
		expect_true("an enemy starts not recruitable too", !hostile.recruitable());
		expect_true("an enemy starts with the failing condition",
					hostile.recruit_condition() == open::RecruitCondition::AlwaysFail);
		expect_true("an enemy starts outside the squad", !hostile.in_squad());
		expect_true("an enemy condition does not pass", !hostile.recruit_condition_passes());
		expect_true("its faction is what it was configured as",
					hostile.faction() == open::Faction::Enemy);
	}
	{
		// living_sheet_sets_flag_and_condition: a living sheet takes the flag and
		// the condition, and a dead one refuses both without losing what it had.
		open::Character jane = make_character(2, open::Faction::Friendly);
		expect_true("a living sheet takes the flag", jane.set_recruitable(true));
		expect_true("the flag round-trips", jane.recruitable());
		expect_true("a living sheet takes AlwaysSucceed",
					jane.set_recruit_condition(open::RecruitCondition::AlwaysSucceed));
		expect_true("the condition round-trips",
					jane.recruit_condition() == open::RecruitCondition::AlwaysSucceed);
		expect_true("AlwaysSucceed passes", jane.recruit_condition_passes());

		expect_true("the same sheet takes AlwaysFail",
					jane.set_recruit_condition(open::RecruitCondition::AlwaysFail));
		expect_true("AlwaysFail does not pass", !jane.recruit_condition_passes());
		expect_true("and the flag survives the condition change", jane.recruitable());

		expect_true("AlwaysSucceed comes back",
					jane.set_recruit_condition(open::RecruitCondition::AlwaysSucceed));
		expect_true("a second flag set is accepted", jane.set_recruitable(true));

		const bool flag_before_death = jane.recruitable();
		const open::RecruitCondition condition_before_death = jane.recruit_condition();
		expect_true("it was still a living, recruitable sheet",
					flag_before_death && condition_before_death ==
											 open::RecruitCondition::AlwaysSucceed);
		kill(jane);
		expect_true("a corpse refuses the flag", !jane.set_recruitable(false));
		expect_true("the flag is as it was before death", jane.recruitable() == flag_before_death);
		expect_true("a corpse refuses the condition",
					!jane.set_recruit_condition(open::RecruitCondition::AlwaysFail));
		expect_true("the condition is as it was before death",
					jane.recruit_condition() == condition_before_death);
		expect_true("a corpse's condition never passes", !jane.recruit_condition_passes());
	}
	{
		// condition_is_not_join: passing the check is not joining. Task 2 applies
		// membership and faction; merely reading the condition must not.
		open::Character jane = make_character(2, open::Faction::Enemy);
		expect_true("the flag is set", jane.set_recruitable(true));
		expect_true("the dummy condition is AlwaysSucceed",
					jane.set_recruit_condition(open::RecruitCondition::AlwaysSucceed));
		expect_true("AlwaysSucceed passes for a living sheet", jane.recruit_condition_passes());
		expect_true("passing the check did not join the squad", !jane.in_squad());
		expect_true("passing the check did not change faction",
					jane.faction() == open::Faction::Enemy);
		expect_true("nor did it take the flag away", jane.recruitable());
	}

	return test_summary();
}
