#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/combat_session.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

#include "test_support.hpp"

// The squad's own two doors: who sits in it before anybody is asked, and whose ask an
// answer was. Break these catches: a founding that also flips faction or spends a flag
// (a join written by the wrong door), a founding that stamps an answer nobody gave, a
// refusal that leaves no trace of who was turned down, a cancel that reads as an answer,
// and a founder whose own ask was answered by somebody else reported as its own.

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

// A sheet shaped like the scene's Jane: the flag set, and the dummy condition that always
// answers yes.
open::Character jane(std::uint64_t id = 2, open::Faction faction = open::Faction::Friendly) {
	open::Character made = make_character(id, faction);
	expect_true("Jane is recruitable", made.set_recruitable(true));
	expect_true("Jane's condition always succeeds",
				made.set_recruit_condition(open::RecruitCondition::AlwaysSucceed));
	return made;
}

open::Vec3 at(float x, float y, float z) {
	return open::Vec3{x, y, z};
}

std::vector<open::Combatant> roster(std::initializer_list<open::Combatant> actors) {
	return std::vector<open::Combatant>(actors);
}

bool is_answered_to(const std::optional<std::uint64_t> &got, std::uint64_t want) {
	return got.has_value() && *got == want;
}

// The answer's own content: who it was answered to says *whose* ask got an answer, and whether
// it was accepted says what that answer said. A display that guessed the content by reading
// membership afterwards would print "recruited" for a seated sheet that refused, so the stamp
// carries both.
bool answers_yes(const open::Character &sheet) {
	const std::optional<bool> answer = sheet.answered_accepted();
	return answer.has_value() && *answer;
}

bool answers_no(const open::Character &sheet) {
	const std::optional<bool> answer = sheet.answered_accepted();
	return answer.has_value() && !*answer;
}

}  // namespace

int main() {
	{
		// nobody_seated_or_answered_at_create: a fresh sheet is outside the squad and has
		// answered to nobody. Seating is the coordinator's founding, and an answer is only
		// stamped by an attempt - neither happens on its own.
		open::Character fresh = make_character(1);
		expect_true("a fresh sheet is not seated", !fresh.in_squad());
		expect_true("a fresh sheet answered to nobody", !fresh.answered_to().has_value());
		expect_true("and it neither accepted nor refused anybody",
					!fresh.answered_accepted().has_value());
	}
	{
		// found_squad_seats_the_founder: the coordinator seats one sheet, and seating writes
		// membership and nothing else. A founder that happened to be Enemy stays Enemy, and
		// a founder that happened to be askable stays askable - a seat is not a join, and it
		// is not an answer either.
		open::Character founder = make_character(1, open::Faction::Enemy);
		expect_true("the founder may be asked", founder.set_recruitable(true));
		const std::vector<open::Combatant> world = roster({{&founder, at(0.0f, 0.0f, 0.0f)}});
		open::CombatSession session(7);

		expect_true("the coordinator seats its founder", session.found_squad(world, 1));
		expect_true("the founder is in the squad", founder.in_squad());
		expect_true("seating never flipped the faction",
					founder.faction() == open::Faction::Enemy);
		expect_true("seating never spent the recruitable flag", founder.recruitable());
		expect_true("seating never changed the condition",
					founder.recruit_condition() == open::RecruitCondition::AlwaysFail);
		expect_true("seating stamped no answer, because nobody was asked",
					!founder.answered_to().has_value());
		expect_true("and stamped no content either, because there was nothing to say",
					!founder.answered_accepted().has_value());

		// Founding twice seats once: the second call answers the same and moves nothing.
		expect_true("founding the seated founder again is accepted",
					session.found_squad(world, 1));
		expect_true("and the seat is the one seat it was", founder.in_squad());
	}
	{
		// found_squad_refuses_what_it_cannot_seat: an absent id, a zero id, a dead sheet and
		// a broken roster all answer false and leave the membership exactly as it was.
		open::Character living = make_character(1);
		open::Character fallen = make_character(2);
		expect_true("the second sheet dies", fallen.damage(open::BodyPart::Torso, 500.0f));
		const std::vector<open::Combatant> world =
			roster({{&living, at(0.0f, 0.0f, 0.0f)}, {&fallen, at(3.0f, 0.0f, 0.0f)}});
		open::CombatSession session(11);

		expect_true("an absent id cannot be seated", !session.found_squad(world, 99));
		expect_true("a zero id cannot be seated", !session.found_squad(world, 0));
		expect_true("a dead sheet cannot be seated", !session.found_squad(world, 2));
		expect_true("the refused founder stays outside the squad", !living.in_squad());
		expect_true("a roster with a null sheet is refused",
					!session.found_squad(roster({{&living, at(0.0f, 0.0f, 0.0f)},
												 {nullptr, at(3.0f, 0.0f, 0.0f)}}),
										 1));
		expect_true("a roster with a duplicate id is refused",
					!session.found_squad(roster({{&living, at(0.0f, 0.0f, 0.0f)},
												 {&living, at(3.0f, 0.0f, 0.0f)}}),
										 1));
		expect_true("and neither refusal seated anybody", !living.in_squad());
	}
	{
		// a_yes_is_answered_to_the_recruiter: the sheet that said yes remembers whose ask it
		// answered, which is how a later issuer tells its own answer from somebody else's.
		open::Character recruiter = make_character(1);
		open::Character asked = jane(2);
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(23);

		expect_true("the ask is issued in range", session.issue_recruit(world, 1, 2));
		expect_true("nothing was answered before the attempt ran",
					!asked.answered_to().has_value());
		expect_true("one positive step answers it", session.step(0.1f, world));
		expect_true("the yes joined", asked.in_squad());
		expect_true("and the yes was answered to the recruiter",
					is_answered_to(asked.answered_to(), 1));
		expect_true("and the yes said yes, not merely somebody answered", answers_yes(asked));
		expect_true("a second step changes nobody's answer", session.step(0.1f, world));
		expect_true("the answer is still the recruiter's",
					is_answered_to(asked.answered_to(), 1));
		expect_true("and still the same yes", answers_yes(asked));
	}
	{
		// a_no_is_answered_too: a refusal is an answer too, and the sheet that refused
		// remembers who turned it down. Nothing joined, but the stamp is what makes the
		// refusal readable as one answer rather than an ask that never happened.
		open::Character recruiter = make_character(1);
		open::Character hostile = make_character(2, open::Faction::Enemy);
		expect_true("the enemy is recruitable", hostile.set_recruitable(true));
		expect_true("and the enemy cannot pass", !hostile.recruit_condition_passes());
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&hostile, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(29);

		expect_true("the enemy is asked in range", session.issue_recruit(world, 1, 2));
		expect_true("one positive step answers", session.step(0.1f, world));
		expect_true("the answer was no, so nobody joined", !hostile.in_squad());
		expect_true("the refused sheet still answers to the recruiter who asked it",
					is_answered_to(hostile.answered_to(), 1));
		expect_true("and its stamp says no, which is the whole content of a refusal",
					answers_no(hostile));
		expect_true("and a no still spends the flag", !hostile.recruitable());
	}
	{
		// one_answer_two_asks: two recruiters ask one sheet. The first gets the answer; the
		// second is stood down by reconciliation, and its own ask is the one that must read
		// as unanswered - the stamp belongs to the first recruiter, not to both.
		open::Character first = make_character(1);
		open::Character second = make_character(3);
		open::Character asked = jane(2);
		const std::vector<open::Combatant> world =
			roster({{&first, at(0.0f, 0.0f, 0.0f)},
					{&asked, at(1.0f, 0.0f, 0.0f)},
					{&second, at(2.0f, 0.0f, 0.0f)}});
		open::CombatSession session(31);

		expect_true("the first recruiter asks", session.issue_recruit(world, 1, 2));
		expect_true("and so does the second", session.issue_recruit(world, 3, 2));
		expect_true("one positive step resolves both", session.step(0.1f, world));
		expect_true("Jane joined", asked.in_squad());
		expect_true("the answer belongs to the first recruiter",
					is_answered_to(asked.answered_to(), 1));
		expect_true("the first recruiter is stood down", !first.recruit_target().has_value());
		expect_true("the second recruiter is stood down too", !second.recruit_target().has_value());
		expect_true("the second ask never got an answer of its own",
					!is_answered_to(asked.answered_to(), 3));
	}
	{
		// a_cancelled_ask_stamps_no_answer: a walk ordered away ends the ask before any
		// attempt runs, and an ended ask is not an answer to anybody - not even to a sheet
		// that had once been seated by founding.
		open::Character recruiter = make_character(1);
		open::Character asked = jane(2);
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(10.0f, 0.0f, 0.0f)}});
		open::CombatSession session(37);

		expect_true("the ask is issued out of range", session.issue_recruit(world, 1, 2));
		expect_true("a new walk is chosen instead", recruiter.set_destination(5.0f, 0.0f, 0.0f));
		expect_true("the ask went with the walk", !recruiter.recruit_target().has_value());
		expect_true("the walk-away left no answer stamped", !asked.answered_to().has_value());
		expect_true("nor did it join anybody", !asked.in_squad());
		expect_true("the flag of whoever was asked stays set", asked.recruitable());

		expect_true("a positive step still runs after the cancel", session.step(0.1f, world));
		expect_true("and it stamps nothing, because nobody refused",
					!asked.answered_to().has_value());
		expect_true("so there is no yes-or-no to read either",
					!asked.answered_accepted().has_value());
	}
	{
		// death_leaves_the_stamped_answer_behind: a member leaves the squad the only way a
		// member can, and leaving does not rewrite the answer it gave. The seat goes; the
		// answer stays whose answer it was.
		open::Character recruiter = make_character(1);
		open::Character asked = jane(2);
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(43);

		expect_true("the ask is issued in range", session.issue_recruit(world, 1, 2));
		expect_true("one positive step answers it", session.step(0.1f, world));
		expect_true("the yes joined", asked.in_squad());
		expect_true("and answered to the recruiter", is_answered_to(asked.answered_to(), 1));

		expect_true("the member dies", asked.damage(open::BodyPart::Torso, 500.0f));
		expect_true("death leaves the squad", !asked.in_squad());
		expect_true("death does not take the answer away",
					is_answered_to(asked.answered_to(), 1));
		expect_true("nor does it turn a yes into a no", answers_yes(asked));
	}
	{
		// founding_is_not_an_answer_for_the_seated: a founder seated by the coordinator is
		// never the sheet an answer was stamped on, so an issuer comparing its own ask's
		// recruiter id against a founder's stamp reads "nobody answered", which is true.
		open::Character founder = make_character(1);
		const std::vector<open::Combatant> world = roster({{&founder, at(0.0f, 0.0f, 0.0f)}});
		open::CombatSession session(47);

		expect_true("the coordinator seats its founder", session.found_squad(world, 1));
		expect_true("the founder is seated", founder.in_squad());
		expect_true("and the founder answered to nobody", !founder.answered_to().has_value());
		expect_true("which means there is no answer content on a founder either",
					!founder.answered_accepted().has_value());
	}

	return test_summary();
}
