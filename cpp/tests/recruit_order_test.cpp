#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/combat_rules.hpp"
#include "open/combat_session.hpp"
#include "open/item_instance.hpp"
#include "open/starter_content.hpp"
#include "open/weapon_rules.hpp"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "test_support.hpp"

// Task 2 recruit-order tests. Break these catches: an ask that attacks instead of
// walking, a walk that closes on the weapon's reach instead of the unarmed 1.5, an
// attempt that fires on the click rather than in range, a cancel counted as a failed
// attempt, a swung-at recruiter that takes the attacker as its own order, and a dead
// member that stays in the squad.

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

// A sheet shaped like the scene's Jane: the flag set, and the dummy condition that
// always answers yes. Nothing else about her is special.
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

const float not_a_number = std::numeric_limits<float>::quiet_NaN();

std::vector<open::Combatant> roster(std::initializer_list<open::Combatant> actors) {
	return std::vector<open::Combatant>(actors);
}

bool is_target(const std::optional<std::uint64_t> &got, std::uint64_t want) {
	return got.has_value() && *got == want;
}

// A walk that stands still, and a walk that closes on +X: the recruit walk is only
// ever tested along one axis, so both readings are enough to tell stop from go.
bool stopped(const open::Vec3 &velocity) {
	return close(velocity.x, 0.0f) && close(velocity.y, 0.0f) && close(velocity.z, 0.0f);
}

bool walking_forward(const open::Vec3 &velocity) {
	return velocity.x > 0.0f && close(velocity.y, 0.0f);
}

// A sheet made against the starter catalog: an item definition is only readable by
// the sheet that was created against the catalog that describes it, and a sheet
// without one swings bare hands no matter what it carries.
open::Character starter_sheet(std::uint64_t id, open::Faction faction = open::Faction::Friendly) {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	const std::optional<open::Character> made = open::Character::create(config(id, faction), catalog);
	expect_true("the catalogued sheet is created", made.has_value());
	return *made;
}

// The long-reach fixture: the starter spear, max reach 2.0, in hand through the same
// add-and-equip door a scene uses. Dexterity 5 is its only requirement.
void hand_spear(open::Character &who, open::ItemId id) {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	const std::optional<open::ItemInstance> instance = open::make_item_instance(
		*catalog, open::kStarterSpear, open::kStarterMaterial, id, 1u);
	expect_true("the spear is made", instance.has_value());
	expect_true("the spear is owned", who.add_instance(*instance));
	expect_true("the dexterity requirement is met", who.set_base(open::Attribute::Dexterity, 5.0f));
	expect_true("the spear is held", who.equip(id, open::Slot::RightHand));
}

// What a character says about a join. Every "this was not a failed attempt" is
// checked against this, so a spent flag cannot hide inside a pass.
struct Membership {
	bool recruitable = false;
	open::RecruitCondition condition = open::RecruitCondition::AlwaysFail;
	bool in_squad = false;
	open::Faction faction = open::Faction::Friendly;
	// Asserts under one name that nothing about the join moved, and answers the same.
	bool as_before(const char *name, const open::Character &who) const {
		const Membership after = read(who);
		const bool same = recruitable == after.recruitable && condition == after.condition &&
						  in_squad == after.in_squad && faction == after.faction;
		expect_true(name, same);
		return same;
	}
	static Membership read(const open::Character &who) {
		return Membership{who.recruitable(), who.recruit_condition(), who.in_squad(), who.faction()};
	}
};

// A fresh level-1 sheet starts with every pool full: the reading an attempt must not
// move, because a recruit spends nothing.
open::Character full_pools(std::uint64_t id) {
	return make_character(id);
}

bool same_pools(const open::Character &before, const open::Character &who) {
	return close(who.pool(open::PoolKind::Stamina).current,
				 before.pool(open::PoolKind::Stamina).current) &&
		   close(who.pool(open::PoolKind::Mana).current, before.pool(open::PoolKind::Mana).current);
}

}  // namespace

int main() {
	{
		// issue_recruit_requires_flag: the flag decides who may be asked and faction
		// never does. A refusal leaves the order the actor already had, and the
		// target, exactly as they were.
		open::Character recruiter = make_character(1);
		open::Character ally = jane(2);
		open::Character hostile = make_character(3, open::Faction::Enemy);
		open::Character fallen = make_character(4);
		expect_true("the fallen sheet dies", fallen.damage(open::BodyPart::Torso, 500.0f));
		expect_true("a corpse takes no flag", !fallen.set_recruitable(true));

		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)},
					{&ally, at(6.0f, 0.0f, 0.0f)},
					{&hostile, at(12.0f, 0.0f, 0.0f)},
					{&fallen, at(20.0f, 0.0f, 0.0f)}});
		open::CombatSession session(41);

		expect_true("a recruitable Friendly is accepted", session.issue_recruit(world, 1, 2));
		expect_true("the sheet holds the recruit order", is_target(recruiter.recruit_target(), 2));
		expect_true("an unrecruitable Enemy is refused", !session.issue_recruit(world, 1, 3));
		expect_true("the refusal kept the order it had", is_target(recruiter.recruit_target(), 2));
		expect_true("a zero target is refused", !session.issue_recruit(world, 1, 0));
		expect_true("asking self is refused", !session.issue_recruit(world, 1, 1));
		expect_true("an absent target is refused", !session.issue_recruit(world, 1, 99));
		expect_true("a dead target is refused", !session.issue_recruit(world, 1, 4));
		expect_true("an absent actor is refused", !session.issue_recruit(world, 98, 2));
		expect_true("a dead actor is refused", !session.issue_recruit(world, 4, 2));
		expect_true("a roster with a duplicate id is refused",
					!session.issue_recruit(roster({{&recruiter, at(0.0f, 0.0f, 0.0f)},
												   {&recruiter, at(3.0f, 0.0f, 0.0f)},
												   {&ally, at(6.0f, 0.0f, 0.0f)}}),
										   1, 2));
		expect_true("a roster with a null sheet is refused",
					!session.issue_recruit(roster({{&recruiter, at(0.0f, 0.0f, 0.0f)},
												   {nullptr, at(3.0f, 0.0f, 0.0f)},
												   {&ally, at(6.0f, 0.0f, 0.0f)}}),
										   1, 2));
		expect_true("a roster with a bad position is refused",
					!session.issue_recruit(roster({{&recruiter, at(not_a_number, 0.0f, 0.0f)},
												   {&ally, at(6.0f, 0.0f, 0.0f)}}),
										   1, 2));

		// Faction is not a filter: an Enemy carrying the flag is asked like anyone.
		expect_true("the enemy takes the flag", hostile.set_recruitable(true));
		expect_true("an Enemy with the flag is askable", session.issue_recruit(world, 1, 3));
		expect_true("the new ask replaced the old one", is_target(recruiter.recruit_target(), 3));

		// A refused call mints nothing: an actor who was walking keeps its walk.
		open::Character walker = make_character(5);
		const std::vector<open::Combatant> walking =
			roster({{&walker, at(0.0f, 0.0f, 0.0f)},
					{&ally, at(6.0f, 0.0f, 0.0f)},
					{&hostile, at(12.0f, 0.0f, 0.0f)}});
		expect_true("the walk is issued", walker.set_destination(8.0f, 0.0f, 0.0f));
		expect_true("and a living sheet can lose the flag again", hostile.set_recruitable(false));
		const Membership unaskable = Membership::read(hostile);
		expect_true("a sheet without the flag is refused",
					!session.issue_recruit(walking, 5, 3));
		expect_true("the walk survived the refusal", walker.destination().has_value());
		expect_true("and no recruit order appeared", !walker.recruit_target().has_value());
		expect_true("the refused target says what it said before",
					unaskable.as_before("the refusal wrote nothing on the target", hostile));
	}
	{
		// walk_uses_unarmed_1_5: the recruit walk closes on 1.5, never on the
		// weapon's reach, and an actor who cannot walk keeps the order it was given.
		open::Character recruiter = make_character(1);
		open::Character asked = jane(2);
		open::CombatSession session(43);

		const std::vector<open::Combatant> far =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(10.0f, 7.0f, 0.0f)}});
		expect_true("Jane is asked", session.issue_recruit(far, 1, 2));

		const open::Vec3 closing = session.velocity_at(1, far);
		expect_true("the walk closes on the target", walking_forward(closing));
		expect_close("the walk is the ordinary walk speed",
					 std::sqrt(closing.x * closing.x + closing.z * closing.z), 4.5f, 0.001f);
		expect_true("and the sheet's own door answers the same",
					walking_forward(recruiter.recruit_velocity_at(at(0.0f, 0.0f, 0.0f),
																  at(10.0f, 7.0f, 0.0f))));

		// A weapon reaches 2.0. A walk that listened to that would stop out here and
		// report the distance closed.
		expect_true("1.75 is out of the recruit range",
					!open::melee_in_range(at(0.0f, 0.0f, 0.0f), at(1.75f, 0.0f, 0.0f)));
		const std::vector<open::Combatant> at_1_75 =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(1.75f, 0.0f, 0.0f)}});
		expect_true("the walk keeps closing past a long weapon's reach",
					walking_forward(session.velocity_at(1, at_1_75)));
		const std::vector<open::Combatant> at_1_5 =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(1.5f, 0.0f, 0.0f)}});
		expect_true("exactly 1.5 stops the walk", stopped(session.velocity_at(1, at_1_5)));
		const std::vector<open::Combatant> at_1_4999 =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(1.4999f, 0.0f, 0.0f)}});
		expect_true("1.4999 stops too", stopped(session.velocity_at(1, at_1_4999)));
		const std::vector<open::Combatant> at_1_5001 =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(1.5001f, 0.0f, 0.0f)}});
		expect_true("1.5001 is out, so it is walked", walking_forward(session.velocity_at(1, at_1_5001)));

		// The same distances with a spear in the recruiter's hand: the range the walk
		// answers to did not move with the weapon. The recruiter is made against the
		// starter catalog, because a sheet without one holds nothing it could name.
		open::Character armed = starter_sheet(6);
		open::Character target = jane(7);
		hand_spear(armed, 401);
		const std::optional<open::WeaponAttackProfile> profile = open::weapon_attack_profile(armed);
		expect_true("the spear is in hand", profile.has_value());
		expect_true("and it really reaches 2.0",
					profile.has_value() && std::fabs(profile->stats.max_reach - 2.0) < 0.001);
		const std::vector<open::Combatant> armed_far =
			roster({{&armed, at(0.0f, 0.0f, 0.0f)}, {&target, at(1.75f, 0.0f, 0.0f)}});
		expect_true("the armed recruiter is asked out", session.issue_recruit(armed_far, 6, 7));
		expect_true("1.75 is inside the spear's own reach",
					open::horizontal_in_reach(at(0.0f, 0.0f, 0.0f), at(1.75f, 0.0f, 0.0f), 2.0));
		expect_true("and the recruit walk still closes toward 1.5",
					walking_forward(session.velocity_at(6, armed_far)));
		const std::vector<open::Combatant> armed_close =
			roster({{&armed, at(0.0f, 0.0f, 0.0f)}, {&target, at(1.5f, 0.0f, 0.0f)}});
		expect_true("in 1.5 the armed recruiter stops", stopped(session.velocity_at(6, armed_close)));
		expect_true("the order survived every distance check", is_target(armed.recruit_target(), 7));

		// A standing dummy asked to recruit keeps the order and goes nowhere.
		open::Character statue = make_character(8);
		statue.set_can_move(false);
		const std::vector<open::Combatant> standing =
			roster({{&statue, at(0.0f, 0.0f, 0.0f)}, {&asked, at(10.0f, 0.0f, 0.0f)}});
		expect_true("the standing actor is ordered", session.issue_recruit(standing, 8, 2));
		expect_true("a standing actor does not walk", stopped(session.velocity_at(8, standing)));
		expect_true("and the recruit order is kept", is_target(statue.recruit_target(), 2));

		// No order, no recruit pursuit: the sheet's own query stays the walk's.
		open::Character idle = make_character(9);
		const std::vector<open::Combatant> alone = roster({{&idle, at(0.0f, 0.0f, 0.0f)}});
		expect_true("with no recruit order the walk is zero", stopped(session.velocity_at(9, alone)));
		expect_true("and a sheet with no order answers nothing",
					stopped(idle.recruit_velocity_at(at(0.0f, 0.0f, 0.0f), at(10.0f, 0.0f, 0.0f))));
	}
	{
		// in_range_always_succeed_joins: standing in range, one positive step, and
		// Jane joins: in the squad, Friendly, and no longer recruitable. The recruiter
		// is stood down, and nothing was spent.
		open::Character recruiter = make_character(1);
		open::Character asked = jane(2);
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(47);
		const Membership before_join = Membership::read(asked);
		const open::Character pools_before = full_pools(2);
		const open::Character recruiter_before = full_pools(1);
		expect_true("the ask is accepted in range", session.issue_recruit(world, 1, 2));

		expect_true("a zero-time step reconciles without asking", session.step(0.0f, world));
		expect_true("the zero-time step joined nobody",
					before_join.as_before("a zero frame ran no check", asked));
		expect_true("and the order is still held", is_target(recruiter.recruit_target(), 2));

		expect_true("one positive step runs", session.step(0.1f, world));
		expect_true("Jane is in the squad", asked.in_squad());
		expect_true("Jane stays Friendly", asked.faction() == open::Faction::Friendly);
		expect_true("Jane is no longer recruitable", !asked.recruitable());
		expect_true("the recruiter was stood down", !recruiter.recruit_target().has_value());
		expect_true("the join spent nothing on the recruiter", same_pools(recruiter_before, recruiter));
		expect_true("the join spent nothing on Jane", same_pools(pools_before, asked));
		expect_true("the join kept Jane's own condition",
					asked.recruit_condition() == open::RecruitCondition::AlwaysSucceed);
		expect_true("the join never swung for it", !recruiter.attack_target().has_value());

		expect_true("a second step runs", session.step(0.1f, world));
		expect_true("the join is not counted twice", asked.in_squad() && !asked.recruitable());
		expect_true("re-asking a member is refused", !session.issue_recruit(world, 1, 2));
	}
	{
		// always_fail_is_one_shot: a condition that says no joins nobody, keeps the
		// faction it was given, and spends the flag - one attempt, then Attack-only.
		open::Character recruiter = make_character(1);
		open::Character hostile = make_character(2, open::Faction::Enemy);
		expect_true("the enemy is recruitable", hostile.set_recruitable(true));
		expect_true("and it starts with the failing condition",
					hostile.recruit_condition() == open::RecruitCondition::AlwaysFail);
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&hostile, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(53);
		const open::Character pools_before = full_pools(2);
		expect_true("the enemy is asked in range", session.issue_recruit(world, 1, 2));

		expect_true("one positive step runs", session.step(0.1f, world));
		expect_true("the answer was no", !hostile.in_squad());
		expect_true("an Enemy refused stays Enemy", hostile.faction() == open::Faction::Enemy);
		expect_true("the flag is spent", !hostile.recruitable());
		expect_true("the recruiter is stood down", !recruiter.recruit_target().has_value());
		expect_true("a refusal spends nothing either", same_pools(pools_before, hostile));
		expect_true("a refused sheet is not askable twice", !session.issue_recruit(world, 1, 2));
		expect_true("nor does a second step re-run the check", session.step(0.1f, world));
		expect_true("still no join", !hostile.in_squad());
	}
	{
		// move_and_attack_cancel_without_fail: a walk or a fight chosen instead ends
		// the ask before any attempt runs. Nothing failed, so the flag stays spent-
		// free and nobody joined.
		open::Character recruiter = make_character(1);
		open::Character asked = jane(2);
		open::Character rival = make_character(3, open::Faction::Enemy);
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)},
					{&asked, at(10.0f, 0.0f, 0.0f)},
					{&rival, at(20.0f, 0.0f, 0.0f)}});
		open::CombatSession session(59);
		expect_true("Jane is asked from far away", session.issue_recruit(world, 1, 2));
		const Membership before_cancel = Membership::read(asked);
		expect_true("Jane is recruitable before the cancel", before_cancel.recruitable);

		expect_true("a walk is chosen instead", recruiter.set_destination(5.0f, 0.0f, 0.0f));
		expect_true("the walk replaced the ask", !recruiter.recruit_target().has_value());
		expect_true("and the walk is really a walk", recruiter.destination().has_value());
		expect_true("Jane says what she said before the walk",
					before_cancel.as_before("a walk cancel is not a failed attempt", asked));

		expect_true("the ask comes back", session.issue_recruit(world, 1, 2));
		expect_true("a fight is chosen instead", session.issue_attack(world, 1, 3));
		expect_true("the fight replaced the ask", !recruiter.recruit_target().has_value());
		expect_true("and the fight is really a fight", is_target(recruiter.attack_target(), 3));
		expect_true("Jane is still as she was",
					before_cancel.as_before("an attack cancel is not a failed attempt either", asked));

		// A cancel is not an attempt, so the step after it must not fire one either.
		expect_true("a step after the cancel runs", session.step(0.1f, world));
		expect_true("the cancel stayed a cancel",
					before_cancel.as_before("no attempt ran after a cancel", asked));
		expect_true("and the fight went on being a fight", is_target(recruiter.attack_target(), 3));
	}
	{
		// target_death_cancels_without_fail: the ask ends with the sheet it was
		// pointed at, and a death is not a failed attempt either.
		open::Character recruiter = make_character(1);
		open::Character asked = jane(2);
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(10.0f, 0.0f, 0.0f)}});
		open::CombatSession session(61);
		expect_true("Jane is asked from far away", session.issue_recruit(world, 1, 2));
		const Membership before_death = Membership::read(asked);
		expect_true("Jane was recruitable before she died", before_death.recruitable);

		expect_true("a step runs while she still stands", session.step(0.1f, world));
		expect_true("the recruiter is still walking the ask",
					is_target(recruiter.recruit_target(), 2));
		expect_true("Jane dies on the way", asked.damage(open::BodyPart::Torso, 500.0f));

		expect_true("the next step reconciles", session.step(0.1f, world));
		expect_true("the ask is gone with the target", !recruiter.recruit_target().has_value());
		expect_true("death wrote nothing on the ask",
					before_death.as_before("a target's death is not a failed attempt", asked));
		expect_true("nor did dropping the ask install a walk", !recruiter.destination().has_value());

		// The reconciliation frame alone is enough: the order cannot be honoured, and
		// a zero-time step runs no check in any case.
		const std::vector<open::Combatant> without_body = roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}});
		expect_true("a zero-time step reconciles the removal", session.step(0.0f, without_body));
		expect_true("the ask is still gone", !recruiter.recruit_target().has_value());
		expect_true("a dead sheet is not asked", !session.issue_recruit(world, 1, 2));
	}
	{
		// hit_while_recruiting_does_not_retaliate: being swung at while asking stops
		// the ask and starts no fight. The recruiter takes no attack order back, and
		// the sheet they were asking is untouched.
		open::Character recruiter = make_character(1);
		open::Character asked = jane(2);
		open::Character attacker = make_character(3, open::Faction::Enemy);
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)},
					{&asked, at(10.0f, 0.0f, 0.0f)},
					{&attacker, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(67);
		expect_true("Jane is asked from far away", session.issue_recruit(world, 1, 2));
		expect_true("and the recruiter is walking the ask", is_target(recruiter.recruit_target(), 2));
		const Membership before_hit = Membership::read(asked);
		expect_true("the attacker is ordered at the recruiter", session.issue_attack(world, 3, 1));

		expect_true("one step runs", session.step(0.1f, world));
		const std::vector<open::CombatEvent> swung = session.take_swing_events();
		expect_true("exactly one swing was performed", swung.size() == 1);
		expect_true("and it was the attacker's",
					swung.size() == 1 && swung[0].actor == 3 && swung[0].target == 1);
		expect_true("the ask was cancelled", !recruiter.recruit_target().has_value());
		expect_true("and the recruiter took no attack order back",
					!recruiter.attack_target().has_value());
		expect_true("the sheet it was asking says what it said",
					before_hit.as_before("a hit on the recruiter is no attempt on its target", asked));

		// The stop is only for the one who was asking: a sheet that held no recruit
		// order still answers a blow the ordinary way. A fresh attacker, because the
		// one above already spent its swing and its clock on the recruiter.
		open::Character bystander = make_character(4);
		open::Character fresh_attacker = make_character(5, open::Faction::Enemy);
		const std::vector<open::Combatant> plain =
			roster({{&bystander, at(0.0f, 0.0f, 0.0f)}, {&fresh_attacker, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession control(79);
		expect_true("the bystander holds no ask", !bystander.recruit_target().has_value());
		expect_true("a fresh attacker is ordered at the bystander", control.issue_attack(plain, 5, 4));
		expect_true("a step runs", control.step(0.1f, plain));
		expect_true("the bystander takes the attacker as its order",
					is_target(bystander.attack_target(), 5));
	}
	{
		// death_removes_from_squad: a member leaves the squad when they die, and
		// nothing else about the corpse is decided here.
		open::Character recruiter = make_character(1);
		open::Character asked = jane(2);
		const std::vector<open::Combatant> world =
			roster({{&recruiter, at(0.0f, 0.0f, 0.0f)}, {&asked, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(71);
		expect_true("Jane is asked in range", session.issue_recruit(world, 1, 2));
		expect_true("one positive step runs", session.step(0.1f, world));
		expect_true("Jane joined", asked.in_squad());

		expect_true("Jane is killed", asked.damage(open::BodyPart::Torso, 500.0f));
		expect_true("death took her out of the squad", !asked.in_squad());
		expect_true("no step puts her back", session.step(0.1f, world));
		expect_true("still out", !asked.in_squad());
	}
	{
		// a_sheet_asked_twice_joins_once: two asks on one sheet resolve in roster
		// order, and the spent flag ends the second walk without a second join.
		open::Character first = make_character(1);
		open::Character asked = jane(2);
		open::Character second = make_character(3);
		const std::vector<open::Combatant> world =
			roster({{&first, at(0.0f, 0.0f, 0.0f)},
					{&asked, at(1.0f, 0.0f, 0.0f)},
					{&second, at(2.0f, 0.0f, 0.0f)}});
		open::CombatSession session(73);
		expect_true("the first recruiter is ordered", session.issue_recruit(world, 1, 2));
		expect_true("and the second too", session.issue_recruit(world, 3, 2));

		expect_true("one positive step runs", session.step(0.1f, world));
		expect_true("Jane joined once", asked.in_squad());
		expect_true("the flag is spent", !asked.recruitable());
		expect_true("the first recruiter is stood down", !first.recruit_target().has_value());
		expect_true("the second ask never joined twice", asked.in_squad());
		expect_true("nor did it change faction twice over",
					asked.faction() == open::Faction::Friendly);
		expect_true("nor did it take Jane's own condition away",
					asked.recruit_condition() == open::RecruitCondition::AlwaysSucceed);
		expect_true("and neither recruiter took an attack order",
					!first.attack_target().has_value() && !second.attack_target().has_value());

		// The second walk cannot be honoured any more, so it ends rather than
		// standing in range forever.
		expect_true("the second recruiter is stood down too", !second.recruit_target().has_value());
		expect_true("and its velocity went with the order",
					stopped(session.velocity_at(3, world)));
	}

	return test_summary();
}
