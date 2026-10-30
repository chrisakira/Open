#include "open/combat_session.hpp"
#include "open/character.hpp"
#include "open/combat_rules.hpp"
#include "open/move_order.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "test_support.hpp"

// Task 4 coordinator tests. Break these catches: a coordinator that owns a copy
// of a sheet, an order that survives its target's death or removal, a fighter
// that quietly re-picks a bystander, in_combat read from a scene flag instead of
// C++ orders, clocks or regeneration advanced twice in one step, a roster with a
// duplicate id, a null sheet or a NaN position that half-applies, a corpse that
// swings or regens, pursuit from a position the coordinator kept instead of the
// one it was just given, and a passive defence that turns into retaliation.

namespace {

constexpr open::BodyPart kParts[] = {open::BodyPart::Head, open::BodyPart::Torso, open::BodyPart::LeftArm,
									 open::BodyPart::RightArm, open::BodyPart::LeftLeg, open::BodyPart::RightLeg};
constexpr std::size_t kPartCount = sizeof(kParts) / sizeof(kParts[0]);

constexpr open::BasicSkill kTracks[] = {open::BasicSkill::UnarmedCombat, open::BasicSkill::OneHandedWeapons,
										open::BasicSkill::TwoHandedWeapons, open::BasicSkill::Polearms,
										open::BasicSkill::Blocking, open::BasicSkill::Dodging};
constexpr std::size_t kTrackCount = sizeof(kTracks) / sizeof(kTracks[0]);
constexpr std::size_t kUnarmed = 0;
constexpr std::size_t kBlocking = 4;
constexpr std::size_t kDodging = 5;

constexpr open::Title kTitles[] = {open::Title::Duelist, open::Title::Breaker, open::Title::Wall};
constexpr std::size_t kTitleCount = sizeof(kTitles) / sizeof(kTitles[0]);

// Fresh sheets swing at trained rank 1: 8 x 1/200, one swing per 1/0.214 s, and
// nothing here sets a rank, a pool or a clock by hand. The typed path divides the blow
// by the defender's base Toughness (1.05) and, when they can pay for a defence, halves
// it; both are named here rather than written at each use.
constexpr float kRank1Damage = 0.04f;
constexpr float kToughnessDivisor = 1.05f;
constexpr float kWholeBlow = kRank1Damage / kToughnessDivisor;
constexpr float kAnsweredBlow = kRank1Damage * 0.5f / kToughnessDivisor;
constexpr float kRank1Cooldown = 1.0f / 0.214f;

// A level-1 sheet's pools all start at 100, and out of combat they refill at 10
// stamina and 4 mana a second; in combat C++ halves stamina and quarters mana.
constexpr float kPoolMax = 100.0f;
constexpr float kDrainedStamina = 90.0f;
constexpr float kDrainedMana = 80.0f;

open::Vec3 at(float x, float y, float z) {
	return open::Vec3{x, y, z};
}

open::CharacterConfig config(std::uint64_t id, open::Faction faction) {
	open::CharacterConfig made;
	made.id = id;
	made.name = "";
	made.faction = faction;
	made.level = 1;
	made.blessing = open::Attribute::Strength;
	return made;
}

open::Character make_character(std::uint64_t id, open::Faction faction = open::Faction::Friendly) {
	const std::optional<open::Character> made = open::Character::create(config(id, faction));
	expect_true("the test character is created", made.has_value());
	return *made;
}

// The roster is a snapshot the test owns: sheets by pointer plus where they are
// standing right now. Nothing here is a second copy of a sheet.
std::vector<open::Combatant> roster(std::initializer_list<open::Combatant> actors) {
	return std::vector<open::Combatant>(actors);
}

bool is_target(const std::optional<std::uint64_t> &got, std::uint64_t want) {
	return got.has_value() && *got == want;
}

float distance(const open::Vec3 &a, const open::Vec3 &b) {
	const float dx = a.x - b.x;
	const float dz = a.z - b.z;
	return std::sqrt(dx * dx + dz * dz);
}

// Destroys a limb without killing the sheet: a limb holds 0.4 of main HP, so one
// hit at its own maximum empties it and leaves the character alive.
void break_leg(open::Character &c) {
	expect_true("a leg is destroyed", c.damage(open::BodyPart::LeftLeg, kPoolMax * 0.4f));
	expect_true("the sheet survives the limb", c.alive());
}

void break_both_legs(open::Character &c) {
	c.damage(open::BodyPart::LeftLeg, kPoolMax * 0.4f);
	expect_true("both legs are destroyed", c.damage(open::BodyPart::RightLeg, kPoolMax * 0.4f));
	expect_true("a legless sheet is still alive", c.alive());
	expect_close("and it cannot walk", c.walk_speed(), 0.0f);
}

// Leaves a sheet with nothing to pay a defence with. The blow then still meets an
// answer, at half, or - once that answer's clock is running - none at all; either way
// the test knows which, and does not have to model the fight to say what it is worth.
void exhaust_defence(open::Character &who) {
	const float available = who.pool(open::PoolKind::Stamina).current;
	if (available > 0.0f) {
		expect_true("the defence is drained", who.spend_stamina(available));
	}
}

// Takes the same resources off every sheet in a block, so a tick's regrowth is
// visible instead of hidden by a full pool.
void drain(open::Character &c) {
	expect_true("stamina is spent", c.spend(open::ResourceUse::Sprint, 1.0f));
	expect_true("mana is spent", c.spend(open::ResourceUse::Spell));
	expect_close("the sheet starts drained", c.pool(open::PoolKind::Stamina).current, kDrainedStamina);
	expect_close("with its mana gone too", c.pool(open::PoolKind::Mana).current, kDrainedMana);
}

// Brings main HP just below one more hit while every part stays alive, so the
// next swing - whichever part it draws - is the one that ends the sheet.
void drain_main_below_one_hit(open::Character &victim, float hit) {
	const open::BodyPart spare[] = {open::BodyPart::LeftArm, open::BodyPart::RightArm, open::BodyPart::LeftLeg,
									open::BodyPart::RightLeg, open::BodyPart::Head};
	for (const open::BodyPart part : spare) {
		const float need = victim.pool(open::PoolKind::Hp).current - 0.015f;
		if (need <= 0.0f) {
			break;
		}
		const float room = victim.body_part(part).current - (hit + 0.02f);
		if (room <= 0.0f) {
			continue;
		}
		expect_true("main HP is worn down", victim.damage(part, room < need ? room : need));
	}
	expect_true("the victim is still alive", victim.alive());
	expect_true("main HP is left below one more hit",
				victim.pool(open::PoolKind::Hp).current > 0.0f &&
					victim.pool(open::PoolKind::Hp).current < hit);
}

// Everything the coordinator may touch on one sheet, so a refused call can be
// compared with the state before it.
struct Observed {
	float hp = 0.0f;
	float stamina = 0.0f;
	float mana = 0.0f;
	std::array<float, kPartCount> parts{};
	std::array<open::Training, kTrackCount> tracks{};
	std::array<unsigned, kTitleCount> titles{};
	float attack_clock = 0.0f;
	float defend_clock = 0.0f;
	int level = 0;
	bool alive = false;
	std::uint64_t order = 0;
	bool has_move = false;
};

Observed observe(const open::Character &c) {
	Observed made;
	made.hp = c.pool(open::PoolKind::Hp).current;
	made.stamina = c.pool(open::PoolKind::Stamina).current;
	made.mana = c.pool(open::PoolKind::Mana).current;
	for (std::size_t i = 0; i < kPartCount; ++i) {
		made.parts[i] = c.body_part(kParts[i]).current;
	}
	for (std::size_t i = 0; i < kTrackCount; ++i) {
		made.tracks[i] = c.basic(kTracks[i]);
	}
	for (std::size_t i = 0; i < kTitleCount; ++i) {
		made.titles[i] = c.title_count(kTitles[i]);
	}
	made.attack_clock = c.attack_cooldown_remaining();
	made.defend_clock = c.defend_cooldown_remaining();
	made.level = c.level();
	made.alive = c.alive();
	made.order = c.attack_target().value_or(0);
	made.has_move = c.destination().has_value();
	return made;
}

// Names the first field that moved, or nullptr when nothing did. Reconciliation
// legitimately changes the order, so the caller says whether to look at it.
const char *first_difference(const Observed &before, const Observed &after, bool check_order) {
	if (!close(before.hp, after.hp)) {
		return "main HP";
	}
	if (!close(before.stamina, after.stamina)) {
		return "stamina";
	}
	if (!close(before.mana, after.mana)) {
		return "mana";
	}
	for (std::size_t i = 0; i < kPartCount; ++i) {
		if (!close(before.parts[i], after.parts[i])) {
			return "a body part";
		}
	}
	for (std::size_t i = 0; i < kTrackCount; ++i) {
		if (before.tracks[i].rank != after.tracks[i].rank || before.tracks[i].xp != after.tracks[i].xp) {
			return "a trained track";
		}
	}
	for (std::size_t i = 0; i < kTitleCount; ++i) {
		if (before.titles[i] != after.titles[i]) {
			return "a title counter";
		}
	}
	if (!close(before.attack_clock, after.attack_clock)) {
		return "the attack clock";
	}
	if (!close(before.defend_clock, after.defend_clock)) {
		return "the defence clock";
	}
	if (before.level != after.level) {
		return "the level";
	}
	if (before.alive != after.alive) {
		return "whether the sheet is alive";
	}
	if (check_order && before.order != after.order) {
		return "the attack order";
	}
	if (check_order && before.has_move != after.has_move) {
		return "the move order";
	}
	return nullptr;
}

void expect_unchanged(const char *name, const Observed &before, const open::Character &c,
					  bool check_order = true) {
	const char *what = first_difference(before, observe(c), check_order);
	if (what != nullptr) {
		std::cerr << "FAIL " << name << ": " << what << " moved\n";
		g_failed++;
	}
}

// The landed damage of one step, counted as main HP lost plus the parts that
// moved, so a swing cannot be charged to main HP twice or spread over parts.
float hp_lost(const Observed &before, const open::Character &c) {
	return before.hp - c.pool(open::PoolKind::Hp).current;
}

int parts_hurt(const Observed &before, const open::Character &c, float tolerance = 0.001f) {
	int hurt = 0;
	for (std::size_t i = 0; i < kPartCount; ++i) {
		if (!close(before.parts[i] - c.body_part(kParts[i]).current, 0.0f, tolerance)) {
			hurt++;
		}
	}
	return hurt;
}

}  // namespace

int main() {
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();

	{
		// resolve_ids_and_preserve_rejected_orders: the coordinator turns ids into
		// sheets, with no faction gate, and a refusal never destroys the intent the
		// actor already had.
		open::Character player = make_character(1, open::Faction::Friendly);
		open::Character ally = make_character(2, open::Faction::Friendly);
		open::Character foe = make_character(3, open::Faction::Enemy);
		open::Character fallen = make_character(4, open::Faction::Friendly);
		expect_true("the fallen sheet dies", fallen.damage(open::BodyPart::Torso, 500.0f));

		const std::vector<open::Combatant> world =
			roster({{&player, at(0.0f, 0.0f, 0.0f)},
					{&ally, at(6.0f, 0.0f, 0.0f)},
					{&foe, at(12.0f, 0.0f, 0.0f)},
					{&fallen, at(20.0f, 0.0f, 0.0f)}});
		open::CombatSession session(11);

		expect_true("a Friendly target is a legal target", session.issue_attack(world, 1, 2));
		expect_true("the sheet holds the resolved order", is_target(player.attack_target(), 2));
		expect_true("an Enemy target is equally legal", session.issue_attack(world, 1, 3));
		expect_true("retargeting through the coordinator works", is_target(player.attack_target(), 3));
		expect_true("the accepted attack leaves no move order", !player.destination().has_value());

		const Observed before = observe(player);
		expect_true("a zero target is refused", !session.issue_attack(world, 1, 0));
		expect_true("attacking self is refused", !session.issue_attack(world, 1, 1));
		expect_true("a target absent from the roster is refused", !session.issue_attack(world, 1, 99));
		expect_true("a dead target is refused", !session.issue_attack(world, 1, 4));
		expect_true("an actor absent from the roster is refused", !session.issue_attack(world, 98, 2));
		expect_true("a dead actor is refused", !session.issue_attack(world, 4, 2));
		expect_unchanged("a refused command changes nothing on the actor", before, player);
		expect_true("the valid order survives every refusal", is_target(player.attack_target(), 3));
		expect_true("no refused target gained an order",
					!ally.attack_target().has_value() && !foe.attack_target().has_value() &&
						!fallen.attack_target().has_value());

		// A rejected attack must not eat a working move order either.
		player.clear_order();
		expect_true("a move is issued to the sheet", player.set_destination(30.0f, 0.0f, 0.0f));
		const Observed walking = observe(player);
		expect_true("an attack on an absent id is refused", !session.issue_attack(world, 1, 99));
		expect_unchanged("the refused attack leaves the move order alone", walking, player);
		expect_vec("the coordinator still reports the walk", session.velocity_at(1, world), 4.5f, 0.0f, 0.0f);
		expect_true("an unregistered id is not a character", !session.in_combat(99, world));
		expect_vec("and has no velocity", session.velocity_at(99, world), 0.0f, 0.0f, 0.0f);
	}

	{
		// incoming_order_marks_passive_defender: pursuit is a fight for both sides,
		// the defender never orders itself, and a doomed order is dropped rather
		// than handed to the nearest bystander.
		open::Character hunter = make_character(1);
		open::Character quarry = make_character(2);
		open::Character bystander = make_character(3);
		const std::vector<open::Combatant> world =
			roster({{&hunter, at(0.0f, 0.0f, 0.0f)},
					{&quarry, at(10.0f, 0.0f, 0.0f)},
					{&bystander, at(30.0f, 0.0f, 0.0f)}});
		open::CombatSession session(12);

		expect_true("the attack is issued", session.issue_attack(world, 1, 2));
		expect_true("the attacker is in combat", session.in_combat(1, world));
		expect_true("the pursued target is in combat while out of reach", session.in_combat(2, world));
		expect_true("a bystander is not", !session.in_combat(3, world));
		expect_true("the passive defender holds no attack order", !quarry.attack_target().has_value());
		expect_true("and no move order either", !quarry.destination().has_value());

		expect_true("a move order cancels the fight", hunter.set_destination(40.0f, 0.0f, 0.0f));
		expect_true("the attacker left combat", !session.in_combat(1, world));
		expect_true("so did the defender", !session.in_combat(2, world));

		expect_true("the attack is re-issued", session.issue_attack(world, 1, 2));
		expect_true("the defender is in combat again", session.in_combat(2, world));
		hunter.clear_order();
		expect_true("clearing the order ends the fight", !session.in_combat(1, world));
		expect_true("for the defender as well", !session.in_combat(2, world));

		// A present, living target keeps the order across a zero-duration step.
		expect_true("the attack is issued once more", session.issue_attack(world, 1, 2));
		expect_true("a zero-time step is accepted", session.step(0.0f, world));
		expect_true("the order is still there while the target is present",
					is_target(hunter.attack_target(), 2));

		// Removal from the roster reconciles on the next step, and does not retarget.
		const std::vector<open::Combatant> without_quarry =
			roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&bystander, at(30.0f, 0.0f, 0.0f)}});
		expect_true("a zero-time step reconciles the removal", session.step(0.0f, without_quarry));
		expect_true("the pursuit of a removed target is cancelled", !hunter.attack_target().has_value());
		expect_true("the actor never picked up the bystander", !hunter.attack_target().has_value());
		expect_true("nobody is in combat after the cancellation",
					!session.in_combat(1, without_quarry) && !session.in_combat(3, without_quarry));

		// Death of the target reconciles the same way.
		expect_true("the attack is issued against the returned target", session.issue_attack(world, 1, 2));
		expect_true("the target dies", quarry.damage(open::BodyPart::Torso, 500.0f));
		expect_vec("a dead target is not chased while the order waits to be reconciled",
				   session.velocity_at(1, world), 0.0f, 0.0f, 0.0f);
		expect_true("a zero-time step reconciles the death", session.step(0.0f, world));
		expect_true("the dead target's order is cancelled", !hunter.attack_target().has_value());
		expect_true("and no bystander takes its place", !hunter.attack_target().has_value());
		expect_true("the corpse is not in combat", !session.in_combat(2, world));
	}

	{
		// pursuit_uses_live_positions: the coordinator walks toward the position it
		// was handed this call, never one it kept from the last one.
		open::Character hunter = make_character(1);
		open::Character runner = make_character(2);
		std::vector<open::Combatant> world =
			roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&runner, at(10.0f, 7.0f, 0.0f)}});
		open::CombatSession session(13);
		expect_true("the attack is issued", session.issue_attack(world, 1, 2));
		expect_vec("the hunter walks at the current target position", session.velocity_at(1, world), 4.5f, 0.0f,
				   0.0f);

		world = roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&runner, at(0.0f, 3.0f, 10.0f)}});
		expect_vec("the pursuit follows a moved snapshot", session.velocity_at(1, world), 0.0f, 0.0f, 4.5f);
		world = roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&runner, at(-10.0f, 0.0f, 0.0f)}});
		expect_vec("including backwards", session.velocity_at(1, world), -4.5f, 0.0f, 0.0f);
		world = roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&runner, at(1.5f, 90.0f, 0.0f)}});
		expect_vec("and stops at melee range, height ignored", session.velocity_at(1, world), 0.0f, 0.0f, 0.0f);
		world = roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&runner, at(1.5001f, 0.0f, 0.0f)}});
		expect_close("one step outside reach walks again", session.velocity_at(1, world).x, 4.5f);
		world = roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&runner, at(10.0f, 7.0f, 0.0f)}});

		// A dead or absent target is a cancelled order, not stale pursuit.
		open::Character ghost = make_character(9);
		const std::vector<open::Combatant> missing =
			roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&ghost, at(10.0f, 0.0f, 0.0f)}});
		expect_vec("an order on an absent id never pursues", session.velocity_at(1, missing), 0.0f, 0.0f, 0.0f);
		expect_true("the order is still the stale one until a step reconciles it",
					is_target(hunter.attack_target(), 2));

		// Standing dummies: no velocity, no outgoing order, still a valid target.
		open::Character post = make_character(3);
		post.set_can_move(false);
		const std::vector<open::Combatant> standing =
			roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&post, at(10.0f, 0.0f, 0.0f)}});
		expect_true("the attack is retargeted onto the standing dummy", session.issue_attack(standing, 1, 3));
		expect_vec("the standing dummy has no velocity", session.velocity_at(3, standing), 0.0f, 0.0f, 0.0f);
		expect_true("the standing dummy has no outgoing order", !post.attack_target().has_value());
		expect_true("both are in combat while the hunter is out of reach",
					session.in_combat(1, standing) && session.in_combat(3, standing));
		expect_vec("and the hunter closes", session.velocity_at(1, standing), 4.5f, 0.0f, 0.0f);

		// Limb destruction is a capability gate, the can_move switch is not.
		open::Character cripple = make_character(1);
		open::Character walker = make_character(2);
		const std::vector<open::Combatant> limping =
			roster({{&cripple, at(0.0f, 0.0f, 0.0f)}, {&walker, at(9.0f, 0.0f, 0.0f)}});
		open::CombatSession limp_session(14);
		expect_true("the attack is issued", limp_session.issue_attack(limping, 1, 2));
		expect_vec("an unhurt cripple pursues", limp_session.velocity_at(1, limping), 4.5f, 0.0f, 0.0f);
		break_leg(cripple);
		expect_vec("one sound leg walks at half speed", limp_session.velocity_at(1, limping), 2.25f, 0.0f,
				   0.0f);
		expect_true("the tick runs", limp_session.step(0.1f, limping));
		expect_true("one sound leg is enough to keep the pursuit",
					is_target(cripple.attack_target(), 2));
		expect_close("and it still walks at half speed", limp_session.velocity_at(1, limping).x, 2.25f);
		expect_true("the other leg goes too", cripple.damage(open::BodyPart::RightLeg, kPoolMax * 0.4f));
		expect_vec("no leg, no pursuit", limp_session.velocity_at(1, limping), 0.0f, 0.0f, 0.0f);
		expect_true("the tick runs", limp_session.step(0.1f, limping));
		expect_true("an out-of-range fighter with no legs cancels its pursuit",
					!cripple.attack_target().has_value());
		expect_true("and leaves the fight", !limp_session.in_combat(1, limping));
		expect_true("the target is out of it as well", !limp_session.in_combat(2, limping));
		expect_true("the target never gained an order", !walker.attack_target().has_value());

		// The same injuries, already in reach: the fight goes on.
		open::Character groundling = make_character(1);
		break_both_legs(groundling);
		open::Character opponent = make_character(2);
		const std::vector<open::Combatant> close =
			roster({{&groundling, at(0.0f, 0.0f, 0.0f)}, {&opponent, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession near_session(15);
		expect_true("the in-reach attack is issued", near_session.issue_attack(close, 1, 2));
		expect_vec("a legless fighter cannot walk", near_session.velocity_at(1, close), 0.0f, 0.0f, 0.0f);
		exhaust_defence(opponent);
		const Observed before_opponent = observe(opponent);
		expect_true("the tick runs", near_session.step(0.1f, close));
		expect_true("in reach, the order survives the lost legs",
					is_target(groundling.attack_target(), 2));
		expect_close("a legless fighter with an arm still swings", hp_lost(before_opponent, opponent),
					 kAnsweredBlow, 0.001f);
		expect_true("and lands on exactly one part", parts_hurt(before_opponent, opponent) == 1);

		// can_move(false) on the attacker is not a lost leg: the order stays.
		open::Character statue = make_character(1);
		statue.set_can_move(false);
		open::Character patient = make_character(2);
		const std::vector<open::Combatant> still =
			roster({{&statue, at(0.0f, 0.0f, 0.0f)}, {&patient, at(9.0f, 0.0f, 0.0f)}});
		open::CombatSession statue_session(16);
		expect_true("the attack is issued", statue_session.issue_attack(still, 1, 2));
		expect_vec("the immobile attacker reports no velocity", statue_session.velocity_at(1, still), 0.0f,
				   0.0f, 0.0f);
		expect_true("the tick runs", statue_session.step(0.1f, still));
		expect_true("the raw switch never cancels the order", is_target(statue.attack_target(), 2));
		expect_true("and both are still in combat",
					statue_session.in_combat(1, still) && statue_session.in_combat(2, still));
	}

	{
		// tick_is_single_owner: one step, one clock advance, one regeneration per
		// sheet, using the C++ combat state that the step itself produced.
		open::Character fighter = make_character(1);
		open::Character duel = make_character(2);
		open::Character idle = make_character(3);
		drain(fighter);
		drain(duel);
		drain(idle);
		const std::vector<open::Combatant> world =
			roster({{&fighter, at(0.0f, 0.0f, 0.0f)},
					{&duel, at(14.0f, 0.0f, 0.0f)},
					{&idle, at(30.0f, 0.0f, 0.0f)}});
		open::CombatSession session(21);

		expect_true("the fight is issued out of reach", session.issue_attack(world, 1, 2));
		expect_true("one second passes", session.step(1.0f, world));
		expect_close("the attacker regrows in-combat stamina",
					 fighter.pool(open::PoolKind::Stamina).current, kDrainedStamina + 5.0f, 0.001f);
		expect_close("the attacker regrows in-combat mana", fighter.pool(open::PoolKind::Mana).current,
					 kDrainedMana + 1.0f, 0.001f);
		expect_close("the passive defender regrows in-combat stamina",
					 duel.pool(open::PoolKind::Stamina).current, kDrainedStamina + 5.0f, 0.001f);
		expect_close("the passive defender regrows in-combat mana", duel.pool(open::PoolKind::Mana).current,
					 kDrainedMana + 1.0f, 0.001f);
		expect_close("the bystander regrows at the full out-of-combat rate",
					 idle.pool(open::PoolKind::Stamina).current, kDrainedStamina + 10.0f, 0.001f);
		expect_close("including its mana", idle.pool(open::PoolKind::Mana).current, kDrainedMana + 4.0f,
					 0.001f);
		expect_close("nothing was spent by a fight that never met", fighter.attack_cooldown_remaining(), 0.0f);
		expect_close("nor was a defence spent waiting for it", duel.defend_cooldown_remaining(), 0.0f);
		expect_close("and nobody was hurt at range", duel.pool(open::PoolKind::Hp).current, kPoolMax, 0.001f);

		// After the cancellation the same sheets use the out-of-combat rate, clamped.
		fighter.clear_order();
		expect_true("the fight is over", !session.in_combat(1, world));
		expect_true("another second passes", session.step(1.0f, world));
		expect_close("the former attacker regrows out of combat",
					 fighter.pool(open::PoolKind::Stamina).current, kPoolMax, 0.001f);
		expect_close("and its mana, which has room left", fighter.pool(open::PoolKind::Mana).current,
					 kDrainedMana + 1.0f + 4.0f, 0.001f);
		expect_close("the former defender regrows out of combat too",
					 duel.pool(open::PoolKind::Stamina).current, kPoolMax, 0.001f);
		expect_close("with the same mana", duel.pool(open::PoolKind::Mana).current,
					 kDrainedMana + 1.0f + 4.0f, 0.001f);
		expect_true("the stamina is clamped to the maximum, never past it",
					fighter.pool(open::PoolKind::Stamina).current <=
						fighter.pool(open::PoolKind::Stamina).maximum);

		// A dead actor swings, spends and regrows nothing.
		open::Character doomed = make_character(1);
		open::Character survivor = make_character(2);
		drain(doomed);
		drain(survivor);
		const std::vector<open::Combatant> dying =
			roster({{&doomed, at(0.0f, 0.0f, 0.0f)}, {&survivor, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession dead_session(22);
		expect_true("the attack is issued", dead_session.issue_attack(dying, 1, 2));
		expect_true("the attacker dies first", doomed.damage(open::BodyPart::Torso, 500.0f));
		const Observed before_doomed = observe(doomed);
		const Observed before_survivor = observe(survivor);
		expect_true("one second passes", dead_session.step(1.0f, dying));
		expect_unchanged("the corpse spends, swings and regrows nothing", before_doomed, doomed);
		expect_close("the would-be defender is unharmed", hp_lost(before_survivor, survivor), 0.0f);
		expect_close("the survivor regrows out of combat, not as a fighter",
					 survivor.pool(open::PoolKind::Mana).current, kDrainedMana + 4.0f, 0.001f);
		expect_true("nobody is in combat around a corpse",
					!dead_session.in_combat(1, dying) && !dead_session.in_combat(2, dying));
	}

	{
		// passive_defense_is_not_retaliation: a landed hit trains and orders nobody
		// on the defending side.
		open::Character attacker = make_character(1);
		open::Character target = make_character(2);
		const std::vector<open::Combatant> world =
			roster({{&attacker, at(0.0f, 0.0f, 0.0f)}, {&target, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(23);
		expect_true("the attack is issued", session.issue_attack(world, 1, 2));
		exhaust_defence(target);

		const Observed before_target = observe(target);
		const Observed before_attacker = observe(attacker);
		expect_true("a short step runs", session.step(0.1f, world));

		expect_close("the blow lands", hp_lost(before_target, target), kAnsweredBlow, 0.001f);
		expect_true("exactly one part is hurt", parts_hurt(before_target, target) == 1);
		expect_close("the attacker is unharmed", hp_lost(before_attacker, attacker), 0.0f);
		expect_true("the defender still has no attack order", !target.attack_target().has_value());
		expect_true("the defender has no move order either", !target.destination().has_value());
		expect_close("the defender never starts its own attack clock", target.attack_cooldown_remaining(),
					 0.0f);
		expect_close("the attacker's clock runs", attacker.attack_cooldown_remaining(), kRank1Cooldown,
					 0.001f);
		// The answer it could not pay for is exhausted, so its clock is the doubled
		// rank-1 figure clamped to the band.
		expect_close("the defender's spent defence clock runs too", target.defend_cooldown_remaining(),
					 5.0f, 0.001f);
		// Passive still means passive: no retaliation, and no Blocking either, because a
		// dodge is what a sheet with nothing held defends with. What it does earn - one
		// Dodging XP for the half it turned aside - is a defence rule, not a coordinator
		// one, and is pinned by the swing tests.
		expect_true("the defender learns no Blocking and earns no Wall",
					target.basic(open::BasicSkill::Blocking).xp == before_target.tracks[kBlocking].xp);
		expect_true("no Wall progress from a defence that lost",
					target.title_count(open::Title::Wall) == 0u);
		expect_true("the attacker trains once for the hit",
					attacker.basic(open::BasicSkill::UnarmedCombat).xp ==
						before_attacker.tracks[kUnarmed].xp + 1u);
	}

	{
		// A kill inside one step is reconciled inside that same step: the winner
		// does not carry a dead target's id into the next frame, and the corpse is
		// never swung at twice.
		open::Character killer = make_character(1);
		open::Character victim = make_character(2);
		const std::vector<open::Combatant> world =
			roster({{&killer, at(0.0f, 0.0f, 0.0f)}, {&victim, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(24);
		expect_true("the attack is issued", session.issue_attack(world, 1, 2));
		exhaust_defence(victim);
		drain_main_below_one_hit(victim, kAnsweredBlow);

		expect_true("a short step runs", session.step(0.1f, world));
		expect_close("the swing finishes the victim", victim.pool(open::PoolKind::Hp).current, 0.0f, 0.001f);
		expect_true("the victim is dead", !victim.alive());
		expect_true("the killing order is already cancelled", !killer.attack_target().has_value());
		expect_true("the winner is out of combat at once", !session.in_combat(1, world));
		expect_true("the corpse is out of combat", !session.in_combat(2, world));

		const Observed settled_killer = observe(killer);
		const float hp_after_kill = victim.pool(open::PoolKind::Hp).current;
		expect_true("another step runs", session.step(0.1f, world));
		expect_close("the corpse is not hit again", victim.pool(open::PoolKind::Hp).current, hp_after_kill);
		expect_close("nobody swung at the winner", killer.pool(open::PoolKind::Hp).current, settled_killer.hp);
		expect_true("the killer trained once, not twice",
					killer.basic(open::BasicSkill::UnarmedCombat).xp == settled_killer.tracks[kUnarmed].xp);
		expect_true("and it never took a replacement target", !killer.attack_target().has_value());
	}

	{
		// invalid_snapshots_and_time_are_atomic: a bad roster or a bad frame changes
		// nothing at all, and a zero-length frame reconciles without acting.
		open::Character a = make_character(1);
		open::Character b = make_character(2);
		open::Character c = make_character(3);
		const std::vector<open::Combatant> world =
			roster({{&a, at(0.0f, 0.0f, 0.0f)}, {&b, at(1.0f, 0.0f, 0.0f)}, {&c, at(20.0f, 0.0f, 0.0f)}});
		open::CombatSession session(31);
		expect_true("the fight is issued", session.issue_attack(world, 1, 2));

		const Observed before_a = observe(a);
		const Observed before_b = observe(b);
		const Observed before_c = observe(c);

		const std::vector<open::Combatant> duplicates =
			roster({{&a, at(0.0f, 0.0f, 0.0f)}, {&b, at(1.0f, 0.0f, 0.0f)}, {&a, at(2.0f, 0.0f, 0.0f)}});
		expect_true("a roster with a duplicate id cannot step", !session.step(0.1f, duplicates));
		expect_true("nor be ordered into", !session.issue_attack(duplicates, 1, 3));
		expect_true("nor be read", !session.in_combat(1, duplicates));
		expect_vec("and its velocity query is zero", session.velocity_at(1, duplicates), 0.0f, 0.0f, 0.0f);

		const std::vector<open::Combatant> with_null =
			roster({{nullptr, at(0.0f, 0.0f, 0.0f)}, {&b, at(1.0f, 0.0f, 0.0f)}});
		expect_true("a roster with a null sheet cannot step", !session.step(0.1f, with_null));
		expect_true("nor be ordered into", !session.issue_attack(with_null, 1, 2));
		expect_true("nor be read", !session.in_combat(1, with_null));
		expect_vec("and its velocity query is zero", session.velocity_at(1, with_null), 0.0f, 0.0f, 0.0f);

		const std::vector<open::Combatant> with_nan =
			roster({{&a, at(nan, 0.0f, 0.0f)}, {&b, at(1.0f, 0.0f, 0.0f)}});
		expect_true("a roster with a nan position cannot step", !session.step(0.1f, with_nan));
		expect_true("nor be ordered into", !session.issue_attack(with_nan, 1, 2));
		expect_true("nor be read", !session.in_combat(1, with_nan));
		expect_vec("and its velocity query is zero", session.velocity_at(1, with_nan), 0.0f, 0.0f, 0.0f);

		const std::vector<open::Combatant> with_inf =
			roster({{&a, at(0.0f, 0.0f, 0.0f)}, {&b, at(1.0f, inf, 0.0f)}});
		expect_true("an infinite height is just as invalid", !session.step(0.1f, with_inf));
		expect_vec("and yields no velocity", session.velocity_at(1, with_inf), 0.0f, 0.0f, 0.0f);

		expect_true("negative time is refused", !session.step(-0.1f, world));
		expect_true("nan time is refused", !session.step(nan, world));
		expect_true("infinite time is refused", !session.step(inf, world));
		expect_unchanged("no bad snapshot or frame hurt the attacker", before_a, a);
		expect_unchanged("nor the target", before_b, b);
		expect_unchanged("nor the bystander", before_c, c);
		expect_true("the order is still standing", is_target(a.attack_target(), 2));

		// Zero time reconciles, and only reconciles.
		const std::vector<open::Combatant> without_b =
			roster({{&a, at(0.0f, 0.0f, 0.0f)}, {&c, at(20.0f, 0.0f, 0.0f)}});
		expect_true("a zero-time step is accepted", session.step(0.0f, without_b));
		expect_true("it cancels the order on the absent target", !a.attack_target().has_value());
		expect_unchanged("reconciliation spends nothing on the actor", before_a, a, false);
		expect_unchanged("and nothing on the bystander", before_c, c);

		const Observed settled_a = observe(a);
		const Observed settled_c = observe(c);
		expect_true("another zero-time step is accepted", session.step(0.0f, without_b));
		expect_unchanged("zero time never opens a swing", settled_a, a);
		expect_unchanged("zero time never moves a clock", settled_c, c);

		// Zero time with both fighters standing in reach, clocks ready: reconciling
		// is still all it may do.
		open::Character boxer = make_character(1);
		open::Character spar = make_character(2);
		const std::vector<open::Combatant> ready =
			roster({{&boxer, at(0.0f, 0.0f, 0.0f)}, {&spar, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession ready_session(33);
		expect_true("the in-reach attack is issued", ready_session.issue_attack(ready, 1, 2));
		const Observed before_boxer = observe(boxer);
		const Observed before_spar = observe(spar);
		expect_true("a zero-time step is accepted", ready_session.step(0.0f, ready));
		expect_unchanged("a ready fighter does not swing for free", before_boxer, boxer);
		expect_unchanged("and nobody is hurt by it", before_spar, spar);
		expect_true("the order is untouched by a zero-time step", is_target(boxer.attack_target(), 2));

		// A dead target is reconciled by the same zero-time step, without a hit.
		open::Character hunter = make_character(1);
		open::Character quarry = make_character(2);
		const std::vector<open::Combatant> pair =
			roster({{&hunter, at(0.0f, 0.0f, 0.0f)}, {&quarry, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession death_session(32);
		expect_true("the in-reach attack is issued", death_session.issue_attack(pair, 1, 2));
		expect_true("the target dies before the step", quarry.damage(open::BodyPart::Torso, 500.0f));
		const Observed before_hunter = observe(hunter);
		const Observed before_quarry = observe(quarry);
		expect_true("a zero-time step is accepted", death_session.step(0.0f, pair));
		expect_true("the order on the corpse is cancelled", !hunter.attack_target().has_value());
		expect_unchanged("and nobody was swung at", before_hunter, hunter, false);
		expect_unchanged("nobody was hurt", before_quarry, quarry);
	}

	{
		// two_orders_use_two_clocks: reciprocal orders each swing on their own
		// clock, and small frames do not become extra swings.
		open::Character first = make_character(1);
		open::Character second = make_character(2);
		const std::vector<open::Combatant> world =
			roster({{&first, at(0.0f, 0.0f, 0.0f)}, {&second, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(41);
		expect_true("the first order is issued", session.issue_attack(world, 1, 2));
		expect_true("the reciprocal order is issued", session.issue_attack(world, 2, 1));
		expect_true("both are in combat", session.in_combat(1, world) && session.in_combat(2, world));

		// Both bars start at exactly one swing's price: the swing is paid, and so whole
		// and unhurried, while a defence at 8 is not affordable and halves whatever it
		// turns aside. That pins every blow in this block to one size without needing
		// the fight to be modelled.
		// Both bars start empty. That makes the first exchange symmetric, which is the
		// point of the block: whoever the roster asks first cannot hold an advantage in
		// the numbers, because neither can pay for a swing or for a defence. Each blow is
		// a quarter of a whole typed hit - half for the unpaid attacker, half again for
		// the unpaid answer - and every clock in the fight starts on the exhausted rank-1
		// figure.
		exhaust_defence(first);
		exhaust_defence(second);
		const Observed before_first = observe(first);
		const Observed before_second = observe(second);
		const float spent_blow = kRank1Damage * 0.25f / kToughnessDivisor;
		const float whole_blow = kRank1Damage / kToughnessDivisor;
		expect_true("one short tick runs", session.step(0.05f, world));
		expect_close("the first fighter lands one hit", hp_lost(before_second, second), spent_blow,
					 0.001f);
		expect_close("the second fighter lands one hit in the same tick", hp_lost(before_first, first),
					 spent_blow, 0.001f);
		expect_close("the first fighter's attack clock is its own", first.attack_cooldown_remaining(),
					 5.0f, 0.001f);
		expect_close("the second fighter's attack clock is its own", second.attack_cooldown_remaining(),
					 5.0f, 0.001f);
		expect_close("both defence clocks started", first.defend_cooldown_remaining(), 5.0f, 0.001f);
		expect_close("including the other's", second.defend_cooldown_remaining(), 5.0f, 0.001f);
		// An unpaid swing and an unpaid defence charge nothing, so the bar holds the
		// frame's own in-combat regrowth and nothing else.
		expect_close("each regrows once per frame", first.pool(open::PoolKind::Stamina).current, 0.25f,
					 0.01f);
		expect_close("for the other fighter as well",
					 second.pool(open::PoolKind::Stamina).current, 0.25f, 0.01f);

		// 100 more ticks of 0.05 s: the cooldown gates the swings, not the frame rate.
		// The bars refill and empty on their own schedule now, so the blows are not all
		// one size and the count is bounded rather than summed - but 5 s is a couple of
		// swings, nowhere near the hundred this loop would have produced per frame.
		for (int tick = 0; tick < 100; ++tick) {
			expect_true("a small tick is accepted", session.step(0.05f, world));
		}
		const float first_lost = hp_lost(before_first, first);
		const float second_lost = hp_lost(before_second, second);
		expect_true("the first fighter was hit a few times across 5 s, not once per frame",
					first_lost >= spent_blow - 1e-4f && first_lost <= 4.0f * whole_blow);
		expect_true("the second fighter was hit a comparable number of times",
					second_lost >= spent_blow - 1e-4f && second_lost <= 4.0f * whole_blow);
		expect_true("each fighter trained its attack for the blows it landed",
					first.basic(open::BasicSkill::UnarmedCombat).xp > before_first.tracks[kUnarmed].xp &&
						second.basic(open::BasicSkill::UnarmedCombat).xp >
							before_second.tracks[kUnarmed].xp);
		expect_true("neither level moved", first.level() == 1 && second.level() == 1);
		expect_true("both orders are still standing",
					is_target(first.attack_target(), 2) && is_target(second.attack_target(), 1));
		expect_true("both are still in combat", session.in_combat(1, world) && session.in_combat(2, world));
		expect_close("the distance is unchanged: the coordinator never moved anyone",
					 distance(world[0].position, world[1].position), 1.0f);
	}

	return test_summary();
}
