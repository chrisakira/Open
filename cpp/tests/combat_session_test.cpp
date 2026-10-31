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
// one it was just given, a provoked victim that repoints a fighter already ordered,
// and a swing nobody performed counting as provocation.

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
	double meter = 0.0;
	double stun = 0.0;
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
	made.meter = c.stagger_buildup();
	made.stun = c.stun_remaining();
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
	if (before.meter != after.meter) {
		return "the stagger meter";
	}
	if (before.stun != after.stun) {
		return "the stun";
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
		// landed_blow_orders_retaliation: a character hit by a swing takes that
		// attacker as its own order. The provocation is the attempt, not the outcome,
		// and it never repoints a sheet that already holds an attack order. Here the
		// rostered victim stands after the attacker, so the return blow is performed
		// inside the very same step.
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
		expect_true("the defender took the attacker as its order", is_target(target.attack_target(), 1));
		expect_true("the defender has no move order either", !target.destination().has_value());
		// The return swing is real and it is performed with an empty bar: exhausted,
		// doubled, and clamped at the band's five-second floor of slowness.
		expect_close("the defender's own attack clock runs", target.attack_cooldown_remaining(),
					 5.0f, 0.001f);
		// The return blow met a ready, paid answer: the attacker dodged the whole of it.
		expect_close("the attacker is unharmed", hp_lost(before_attacker, attacker), 0.0f);
		expect_close("and the attacker's answer has its clock running too",
					 attacker.defend_cooldown_remaining(), kRank1Cooldown, 0.001f);
		expect_close("the attacker's clock runs", attacker.attack_cooldown_remaining(), kRank1Cooldown,
					 0.001f);
		expect_true("the attacker keeps its own order instead of being repointed",
					is_target(attacker.attack_target(), 2));
		// The answer the defender could not pay for is exhausted, so its clock is the
		// doubled rank-1 figure clamped to the band.
		expect_close("the defender's spent defence clock runs too", target.defend_cooldown_remaining(),
					 5.0f, 0.001f);
		// Retaliation orders the victim; it does not invent a Blocking, because a
		// sheet holding nothing defends by dodging, and it earns no Wall for a lost
		// defence. What the defence does earn - one Dodging XP for the half it turned
		// aside - is a defence rule pinned by the swing tests.
		expect_true("the defender learns no Blocking and earns no Wall",
					target.basic(open::BasicSkill::Blocking).xp == before_target.tracks[kBlocking].xp);
		expect_true("no Wall progress from a defence that lost",
					target.title_count(open::Title::Wall) == 0u);
		expect_true("the attacker trains once for the hit",
					attacker.basic(open::BasicSkill::UnarmedCombat).xp ==
						before_attacker.tracks[kUnarmed].xp + 1u);
		// Both performed swings are reported, in the order the roster asked.
		const std::vector<open::CombatEvent> frame = session.take_swing_events();
		expect_true("one step, two performed swings", frame.size() == 2);
		expect_true("the ordered blow first", frame.size() == 2 && frame[0].actor == 1 &&
					frame[0].target == 2);
		expect_true("the return blow second", frame.size() == 2 && frame[1].actor == 2 &&
					frame[1].target == 1);
		expect_true("and the return blow really met the answer it dodged",
					frame.size() == 2 && frame[1].swing.defense_action.has_value());
	}

	{
		// a_turned_aside_blow_still_provokes: dodged, blocked or zero - the attempt
		// is the provocation. A fresh defence that nullifies the blow completely was
		// still attacked, and it orders the attacker back.
		open::Character attacker = make_character(1);
		open::Character target = make_character(2);
		const std::vector<open::Combatant> world =
			roster({{&attacker, at(0.0f, 0.0f, 0.0f)}, {&target, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(27);
		expect_true("the attack is issued", session.issue_attack(world, 1, 2));

		const Observed before_target = observe(target);
		expect_true("a short step runs", session.step(0.1f, world));

		expect_close("the nullified blow hurt nothing", hp_lost(before_target, target), 0.0f);
		expect_true("and it still provoked", is_target(target.attack_target(), 1));
		// The victim paid the 8 for its answer and the 8 for the return swing it then
		// made, in this same step, at its own full speed.
		expect_true("the defender paid for the answer and for the return",
					target.pool(open::PoolKind::Stamina).current < 92.0f);
		expect_close("the defender's attack clock runs", target.attack_cooldown_remaining(),
					 kRank1Cooldown, 0.001f);
	}

	{
		// refused_attempt_provokes_nobody: a walk toward a target is not yet a
		// swing. Nothing performed, nothing reported, nothing to retaliate against.
		open::Character attacker = make_character(1);
		open::Character target = make_character(2);
		const std::vector<open::Combatant> world =
			roster({{&attacker, at(0.0f, 0.0f, 0.0f)}, {&target, at(5.0f, 0.0f, 0.0f)}});
		open::CombatSession session(31);
		expect_true("the attack is issued", session.issue_attack(world, 1, 2));
		expect_true("a short step runs", session.step(0.1f, world));
		expect_true("nothing was performed", session.take_swing_events().empty());
		expect_true("nobody was provoked", !target.attack_target().has_value());
		expect_close("the would-be defender spent no answer", target.defend_cooldown_remaining(),
					 0.0f, 0.001f);
	}

	{
		// retaliation_never_repoints_a_fighter: a victim already ordered at somebody
		// keeps that target. Getting hit from elsewhere is provocation only for a
		// sheet that held no attack order at all.
		open::Character attacker = make_character(1);
		open::Character target = make_character(2);
		open::Character rival = make_character(3);
		const std::vector<open::Combatant> world =
			roster({{&attacker, at(0.0f, 0.0f, 0.0f)},
					{&target, at(1.0f, 0.0f, 0.0f)},
					{&rival, at(30.0f, 0.0f, 0.0f)}});
		open::CombatSession session(37);
		expect_true("the victim already fights elsewhere", target.set_attack_target(3));
		expect_true("the attack is issued", session.issue_attack(world, 1, 2));
		exhaust_defence(target);

		const Observed before_target = observe(target);
		expect_true("a short step runs", session.step(0.1f, world));

		expect_true("the blow did land", hp_lost(before_target, target) > 0.0f);
		expect_true("and the victim still aims at its own chosen target",
					is_target(target.attack_target(), 3));
	}

	{
		// a_walk_becomes_a_fight: a victim that was walking loses the walk to the
		// retaliation, exactly as an issued attack order clears the walk it replaces.
		open::Character attacker = make_character(1);
		open::Character target = make_character(2);
		const std::vector<open::Combatant> world =
			roster({{&attacker, at(0.0f, 0.0f, 0.0f)}, {&target, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(41);
		expect_true("the victim is walking away", target.set_destination(1.0f, 0.0f, 8.0f));
		expect_true("the attack is issued", session.issue_attack(world, 1, 2));
		exhaust_defence(target);
		expect_true("a short step runs", session.step(0.1f, world));
		expect_true("the defender gave up the walk", !target.destination().has_value());
		expect_true("and took the attacker as its own order", is_target(target.attack_target(), 1));
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
		expect_true("and the killing blow provokes nobody", !victim.attack_target().has_value());
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

	{
		// no_catch_up_on_a_long_frame: one step is one attempt per ordered sheet,
		// however much time it covers. A coordinator that looped its own clocks would
		// swing several times in a 60 s frame and none of its bookkeeping would show it.
		open::Character hitter = make_character(1);
		open::Character target = make_character(2);
		exhaust_defence(hitter);
		exhaust_defence(target);
		const std::vector<open::Combatant> world =
			roster({{&hitter, at(0.0f, 0.0f, 0.0f)}, {&target, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession whole(61);
		expect_true("the fight is issued", whole.issue_attack(world, 1, 2));

		const float spent_blow = kRank1Damage * 0.25f / kToughnessDivisor;
		const Observed before_whole = observe(target);
		expect_true("one 60-second frame runs", whole.step(60.0f, world));
		expect_close("exactly one blow arrives across a minute of game time",
					 hp_lost(before_whole, target), spent_blow, 0.001f);
		expect_true("and the attacker trained once",
					hitter.basic(open::BasicSkill::UnarmedCombat).xp == 1u);
		// The clocks of a step run before the swings, so the minute bought one attempt
		// and one cooldown - not twelve of each. This is the catch-up this case rules
		// out: a coordinator that looped its clocks inside the frame would be ready
		// again, and a second blow would have landed.
		expect_close("the swing this frame took is still cooling at the end of it",
					 hitter.attack_cooldown_remaining(), 5.0f, 0.001f);
		expect_close("and so is the answer it forced",
					 target.defend_cooldown_remaining(), 5.0f, 0.001f);

		// The same wall of time, walked a second at a time, is as many swings as the clock
		// allows and no more. The difference is the point: the cooldown gates swings, and
		// a frame does not re-buy one it already paid for, nor does it bank the time it
		// skipped into extra attempts.
		open::Character stepper = make_character(1);
		open::Character patient = make_character(2);
		exhaust_defence(stepper);
		exhaust_defence(patient);
		const std::vector<open::Combatant> ticked =
			roster({{&stepper, at(0.0f, 0.0f, 0.0f)}, {&patient, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession by_hand(61);
		expect_true("the same fight is issued", by_hand.issue_attack(ticked, 1, 2));
		unsigned ticked_swings = 0;
		for (int second = 0; second < 60; ++second) {
			// Both bars are emptied before every tick. A second of in-combat regen is 5
			// stamina, under the 8 a defence costs, so each tick is met the same way: an
			// unpaid swing and an unpaid answer. Without this the bar regrows, the victim's
			// dodge becomes payable, and a payable dodge turns every later blow aside -
			// which is a defence rule, and not what "how many swings does a clock allow"
			// is about.
			exhaust_defence(stepper);
			exhaust_defence(patient);
			const float hp_pre = patient.pool(open::PoolKind::Hp).current;
			expect_true("a second passes", by_hand.step(1.0f, ticked));
			// Counting the ticks a blow arrived on, rather than dividing total HP by one
			// blow's size, because the blow is not one size: the skill trains as it lands
			// hits and the rank rising makes each later one bigger.
			ticked_swings += patient.pool(open::PoolKind::Hp).current < hp_pre ? 1u : 0u;
		}
		// Every blow of this fight is the same size - unpaid swing, unpaid answer - so the
		// HP the victim lost is an exact count of the swings that arrived. HP is used
		// rather than XP because the weapon skill ranks up as it trains and a rank-up
		// discards the residual progress, which is a skill rule and not this one.
		//
		// The count is the clock's own schedule: an unpaid rank-1 swing is the slowest
		// cooldown in the band, 5 s, so sixty seconds is twelve swings. The single 60 s
		// frame above is the same fight and the same twelve seconds of cooldown, and one
		// swing.
		const float single_hp = hp_lost(before_whole, target);
		expect_true("sixty seconds walked a second at a time is twelve swings, one every 5 s",
					ticked_swings == 12u);
		expect_true("and not one per frame", ticked_swings < 20u);
		expect_close("with exactly one swing for the same time in a single frame", single_hp, spent_blow,
					 0.001f);
	}

	{
		// roster_order_is_who_goes_first: the roster is the order the coordinator acts
		// in, and reversing it reverses who gets the scarce resource. Two fighters start
		// with enough for exactly one paid swing between them; whichever the roster asks
		// first is the one that swings whole, and the other swings unpaid.
		open::Character a = make_character(1);
		open::Character b = make_character(2);
		// One swing's price, and one defence's price, split so that only the first
		// actor in the roster can afford both.
		expect_true("a holds exactly a swing", a.spend(open::ResourceUse::Sprint, (100.0f - 8.0f) / 10.0f));
		expect_true("b holds nothing", b.spend(open::ResourceUse::Sprint, 10.0f));
		expect_close("a can pay for one swing", a.pool(open::PoolKind::Stamina).current, 8.0f, 0.001f);
		expect_close("b cannot", b.pool(open::PoolKind::Stamina).current, 0.0f, 0.001f);

		const std::vector<open::Combatant> a_first =
			roster({{&a, at(0.0f, 0.0f, 0.0f)}, {&b, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(71);
		expect_true("a orders b", session.issue_attack(a_first, 1, 2));
		expect_true("b orders a", session.issue_attack(a_first, 2, 1));
		const Observed before_a = observe(a);
		const Observed before_b = observe(b);
		expect_true("one tick runs", session.step(0.05f, a_first));
		// The paid attacker's blow arrives at the unpaid-defence half; the unpaid
		// attacker's blow is half again on top of that.
		const float paid_blow = kRank1Damage * 0.5f / kToughnessDivisor;
		const float unpaid_blow = kRank1Damage * 0.25f / kToughnessDivisor;
		expect_close("the first fighter in the roster lands the paid blow", hp_lost(before_b, b), paid_blow,
					 0.001f);
		expect_close("the second lands the unpaid one", hp_lost(before_a, a), unpaid_blow, 0.001f);
		expect_close("and the first one spent its whole bar on it",
					 a.pool(open::PoolKind::Stamina).current, 0.25f, 0.01f);

		// Reverse the roster and the swing order moves with it: the same two sheets with
		// the same two bars, the same seed and the same frame, only asked in the other
		// order, and the sheet that got the paid blow is now the one that got the unpaid
		// one. The point is the swap, so the two are compared with each other rather
		// than with a number that would also pass if the halves went the other way.
		open::Character c = make_character(1);
		open::Character d = make_character(2);
		expect_true("c holds exactly a swing", c.spend(open::ResourceUse::Sprint, (100.0f - 8.0f) / 10.0f));
		expect_true("d holds nothing", d.spend(open::ResourceUse::Sprint, 10.0f));
		const std::vector<open::Combatant> d_first =
			roster({{&d, at(1.0f, 0.0f, 0.0f)}, {&c, at(0.0f, 0.0f, 0.0f)}});
		open::CombatSession reversed(71);
		expect_true("c orders d", reversed.issue_attack(d_first, 1, 2));
		expect_true("d orders c", reversed.issue_attack(d_first, 2, 1));
		const Observed before_c = observe(c);
		const Observed before_d = observe(d);
		expect_true("the same tick runs in the other order", reversed.step(0.05f, d_first));
		const float c_was_hit = hp_lost(before_c, c);
		const float d_was_hit = hp_lost(before_d, d);
		// Asked second, the funded sheet meets an unpaid blow it can afford to answer, and
		// a ready dodge it has paid for is worth everything: nothing arrives. Asked first,
		// the same sheet spent the same 8 on the swing instead and both blows landed. One
		// bar, two orders, and the roster decided what it bought.
		expect_close("the sheet asked second turns its unpaid blow aside", c_was_hit, 0.0f, 1e-6);
		expect_close("and lands the unpaid blow the other order never got", d_was_hit, unpaid_blow,
					 0.001f);
		expect_true("where that order had both blows arrive",
					hp_lost(before_a, a) > 0.0f && hp_lost(before_b, b) > 0.0f);
		expect_close("and the funded sheet spent its bar on the answer instead",
					 c.pool(open::PoolKind::Stamina).current, 0.25f, 0.01f);

		// A corpse in the roster is not an actor at all: the sheet it had ordered is
		// reconciled away in the same step, and nobody it pointed at is hit twice.
		open::Character living = make_character(1);
		open::Character corpse = make_character(2);
		const std::vector<open::Combatant> dying =
			roster({{&living, at(0.0f, 0.0f, 0.0f)}, {&corpse, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession after(72);
		expect_true("the living orders the doomed one", after.issue_attack(dying, 1, 2));
		expect_true("the doomed one orders back", after.issue_attack(dying, 2, 1));
		expect_true("and it dies inside the frame", corpse.damage(open::BodyPart::Torso, 500.0f));
		const Observed before_living = observe(living);
		expect_true("one tick runs", after.step(0.05f, dying));
		expect_unchanged("a corpse neither swings nor is swung at", before_living, living, false);
		expect_true("and its order was reconciled, not kept for later",
					!corpse.attack_target().has_value());
	}

	{
		// a_stun_freezes_the_fighter_and_nobody_else: a stun reached through the session
		// by a real fight, and everything it does and must not do.
		//
		// Reaching one is a matter of skill rather than a bigger weapon, which is worth
		// writing down because the meter decays: it loses a tenth of its threshold every
		// second, so a rank-1 blow (0.04 blunt once every 4.7 s) can never fill it, while
		// a rank-100 unarmed sheet lands 4 blunt 1.6 times a second and fills it after
		// about half a minute of exchange with half its health left. Nothing outside a
		// sheet can stun one, so a fight like this is the only way to be stunned in a test
		// - which is exactly why the state is worth pinning here rather than only in the
		// `stagger` case's state machine.
		auto master = [](std::uint64_t id) {
			open::Character made = make_character(id);
			while (made.basic(open::BasicSkill::UnarmedCombat).rank < 100u) {
				made.record_use(open::BasicSkill::UnarmedCombat, std::nullopt, true);
			}
			return made;
		};

		open::Character left = master(1);
		open::Character right = master(2);
		const std::vector<open::Combatant> world =
			roster({{&left, at(0.0f, 0.0f, 0.0f)}, {&right, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession session(91);
		expect_true("the fighter the roster asks to first orders the other",
					session.issue_attack(world, 1, 2));
		expect_true("and the other orders back", session.issue_attack(world, 2, 1));

		// Both bars are emptied before every step so no blow is ever turned aside whole:
		// the meter is then the only thing deciding when this fight changes shape.
		double left_in_step = 0.0;
		double right_in_step = 0.0;
		int steps = 0;
		for (; steps < 2000; ++steps) {
			exhaust_defence(left);
			exhaust_defence(right);
			left_in_step = left.pool(open::PoolKind::Hp).current;
			right_in_step = right.pool(open::PoolKind::Hp).current;
			expect_true("a tenth of a second passes", session.step(0.1f, world));
			left_in_step -= left.pool(open::PoolKind::Hp).current;
			right_in_step -= right.pool(open::PoolKind::Hp).current;
			if (left.is_stunned() || right.is_stunned()) {
				break;
			}
		}
		expect_true("the fight stunned one of them before either died",
					left.is_stunned() != right.is_stunned() && left.alive() && right.alive());
		const bool first_was_stunned = left.is_stunned();
		open::Character &down = first_was_stunned ? left : right;
		open::Character &standing = first_was_stunned ? right : left;
		// What each sheet *took* in the stun step is what the other one landed. The sheet
		// the roster asked first swings first, so its blow is the one that started the
		// stun, and the answer it forced is the attempt the stun then refused.
		const double down_took = first_was_stunned ? left_in_step : right_in_step;
		const double standing_took = first_was_stunned ? right_in_step : left_in_step;
		expect_true("and it is the sheet the roster asked second, whose attempt the stun "
					"refused before it could be made",
					!first_was_stunned);
		expect_true("the one asked first landed the blow that started it", down_took > 0.0);
		expect_close("and the sheet it stunned landed nothing in that same step", standing_took, 0.0,
					 1e-9);
		expect_close("for the promised two seconds of stun", down.stun_remaining(), 2.0, 1e-9);
		expect_true("and the meter reset rather than carrying the overkill", down.stagger_buildup() == 0.0);

		// What the stun holds and what it must not touch: the order, the fight, the
		// clocks and the schedule it was set on.
		const std::optional<std::uint64_t> held_order = down.attack_target();
		const double frozen_attack = down.attack_cooldown_remaining();
		const double frozen_defence = down.defend_cooldown_remaining();
		expect_true("a stunned sheet still holds its attack order", held_order.has_value());
		expect_true("and is still in the fight it is losing", session.in_combat(2, world));
		expect_true("its defence clock was left running by the answer it made", frozen_defence > 0.0);
		expect_vec("a stunned sheet does not walk", session.velocity_at(2, world), 0.0f, 0.0f, 0.0f);

		// Every claim below is checked on every one of those ten-frames: a stun is two
		// seconds of a sheet being frozen while everything else about it keeps running.
		int stunned_frames = 0;
		double health_at_expiry = down.pool(open::PoolKind::Hp).current;
		double health_before_stun_window = health_at_expiry;
		// Blows the stun took without an answer, counted so the claim below is about a
		// swing that really happened rather than about an empty queue.
		int stunned_blows = 0;
		// The same sheet in the frames before the freeze: it answered there, on the very
		// clocks the stun now holds. That is what makes the window's silence the stun's own
		// doing rather than a queue with nothing in it - a value can only come from a
		// defence that was ready and made, and none is reported once the sheet is down.
		int free_answers = 0;
		for (const open::CombatEvent &event : session.take_swing_events()) {
			if (event.target == down.id() && event.swing.defense_action.has_value()) {
				++free_answers;
			}
		}
		expect_true("the sheet the stun is about to hold had been answering", free_answers > 0);
		while (down.is_stunned() && stunned_frames < 60) {
			const double stun_before = down.stun_remaining();
			const double hp_before = down.pool(open::PoolKind::Hp).current;
			exhaust_defence(left);
			exhaust_defence(right);
			// Drain first: the queue is the session's, and a frame can only be read
			// unambiguously once everything older than it has been taken out.
			session.take_swing_events();
			expect_true("a tenth of a second passes", session.step(0.1f, world));
			++stunned_frames;
			// A frame that spent the stun whole is the only kind this claim is about: past
			// the expiry the sheet is free to answer again, and this fixture counts that
			// resumption separately.
			if (stun_before > 0.1) {
				const std::vector<open::CombatEvent> frame = session.take_swing_events();
				for (const open::CombatEvent &event : frame) {
					if (event.target == down.id()) {
						++stunned_blows;
						// A stunned sheet defends with nothing, so the report has nothing
						// to name - the swing was performed, and the answer was not.
						expect_true("a swing at a stunned sheet reports no defence",
									!event.swing.defense_action.has_value());
					}
				}
				expect_true("and that frame was read once", session.take_swing_events().empty());
			}
			// A frame that runs out of the stun is not a frozen frame: the clocks of the
			// unstunned remainder run, and so may the swing, which is the resumption
			// asserted below.
			if (down.is_stunned()) {
				expect_close("the attack clock is frozen", down.attack_cooldown_remaining(), frozen_attack,
							 1e-9);
				expect_close("the defence clock is frozen", down.defend_cooldown_remaining(),
							 frozen_defence, 1e-9);
				expect_close("the meter holds still", down.stagger_buildup(), 0.0, 1e-9);
			}
			// Regen is not frozen with the clocks: a stun is two seconds of being hit, not
			// a lockout from recovering while hit, and it happens once per step however
			// many clocks it touches.
			expect_close("one step regrows one step's stamina",
						 static_cast<double>(down.pool(open::PoolKind::Stamina).current), 0.5, 0.01);
			expect_close("the stun counts down by exactly the frame it was given, and stops at zero",
						 down.stun_remaining(), stun_before > 0.1 ? stun_before - 0.1 : 0.0, 1e-6);
			expect_vec("and holds the sheet in place", session.velocity_at(2, world), 0.0f, 0.0f, 0.0f);
			expect_true("with its order intact", down.attack_target() == held_order);
			expect_true("and still worth hitting", session.in_combat(2, world));
			if (down.pool(open::PoolKind::Hp).current < hp_before) {
				health_at_expiry = down.pool(open::PoolKind::Hp).current;
			}
		}
		expect_true("the stun expired", !down.is_stunned());
		expect_true("on the two-second schedule it started on, and not the frame count",
					stunned_frames >= 19 && stunned_frames <= 21);
		expect_true("blows kept landing on a stunned sheet: a stun is not a shield",
					health_at_expiry < health_before_stun_window);
		// The claim above is only interesting if there were swings to read, and the queue
		// was not simply empty all the way through the freeze.
		expect_true("and the frozen window held swings worth reporting", stunned_blows > 0);

		// And the freeze was the whole of it: the moment the stun is gone, the clocks the
		// stun held start running again, and so does the sheet.
		// The held clocks are not asserted here: the blow the other sheet lands on the
		// way out restarts the defence clock, so the resume arithmetic belongs to the
		// `stagger` case and what belongs here is that the sheet is acting again.
		const double standing_hp_before = standing.pool(open::PoolKind::Hp).current;
		for (int tick = 0; tick < 20 && standing.pool(open::PoolKind::Hp).current == standing_hp_before;
			 ++tick) {
			exhaust_defence(left);
			exhaust_defence(right);
			expect_true("a tenth of a second passes", session.step(0.1f, world));
		}
		expect_true("the freed sheet swings again, which is the clock running again",
					standing.pool(open::PoolKind::Hp).current < standing_hp_before);
		expect_true("and the fight was never in doubt about who was in it",
					session.in_combat(1, world) && session.in_combat(2, world));
	}

	{
		// a_stun_holds_a_fighter_in_place_without_taking_the_order_away: an order is
		// not cancelled by being stunned, it is only not being done, and it resumes.
		// The order under test is the retaliation the first landed blow installed -
		// a walk no longer survives being swung at, and that replacement is pinned
		// here too, by the same fight.
		auto master = [](std::uint64_t id) {
			open::Character made = make_character(id);
			while (made.basic(open::BasicSkill::UnarmedCombat).rank < 100u) {
				made.record_use(open::BasicSkill::UnarmedCombat, std::nullopt, true);
			}
			return made;
		};
		open::Character hitter = master(1);
		open::Character target = make_character(2);
		const std::vector<open::Combatant> world =
			roster({{&hitter, at(0.0f, 0.0f, 0.0f)}, {&target, at(0.0f, 0.0f, 1.0f)}});
		open::CombatSession session(93);
		expect_true("the attack is issued", session.issue_attack(world, 1, 2));
		expect_true("the victim was walking away", target.set_destination(0.0f, 0.0f, 30.0f));

		int steps = 0;
		while (!target.is_stunned() && steps < 2000) {
			++steps;
			exhaust_defence(hitter);
			exhaust_defence(target);
			expect_true("a tenth of a second passes", session.step(0.1f, world));
		}
		expect_true("the fighter was stunned mid-fight", target.is_stunned());
		expect_true("the first landed blow had taken the walk away and given a "
					"retaliation in its place",
					!target.destination().has_value() && is_target(target.attack_target(), 1));
		expect_vec("the stun holds them in place", session.velocity_at(2, world), 0.0f, 0.0f, 0.0f);
		expect_true("and does not make them a bystander", session.in_combat(2, world));

		int to_expiry = 0;
		while (target.is_stunned() && to_expiry < 60) {
			++to_expiry;
			exhaust_defence(hitter);
			exhaust_defence(target);
			expect_true("a tenth of a second passes", session.step(0.1f, world));
		}
		expect_true("the stun ended", !target.is_stunned());
		expect_true("the retaliation order outlived the stun", is_target(target.attack_target(), 1));

		const float hitter_hp_before = hitter.pool(open::PoolKind::Hp).current;
		int resumed = 0;
		while (hitter.pool(open::PoolKind::Hp).current == hitter_hp_before && resumed < 120) {
			++resumed;
			exhaust_defence(hitter);
			exhaust_defence(target);
			expect_true("a tenth of a second passes", session.step(0.1f, world));
		}
		expect_true("and the freed sheet swings its attacker again",
					hitter.pool(open::PoolKind::Hp).current < hitter_hp_before);
	}

	{
		// clocks_answer_to_time_and_nothing_else: retargeting, losing a target to a
		// removal, and killing a target all leave a cooling clock cooling. An order is
		// intent, and intent is not a way to swing twice or to hurry a swing up.
		open::Character swing1 = make_character(1);
		open::Character foe_a = make_character(2);
		open::Character foe_b = make_character(3);
		const std::vector<open::Combatant> world =
			roster({{&swing1, at(0.0f, 0.0f, 0.0f)},
					{&foe_a, at(1.0f, 0.0f, 0.0f)},
					{&foe_b, at(2.0f, 0.0f, 0.0f)}});
		open::CombatSession session(95);
		expect_true("the first fight is issued", session.issue_attack(world, 1, 2));
		exhaust_defence(foe_a);
		expect_true("one tick swings", session.step(0.05f, world));
		const double cooling = swing1.attack_cooldown_remaining();
		expect_true("and starts its clock", cooling > 0.0);
		expect_close("the blow arrived", 100.0 - foe_a.pool(open::PoolKind::Hp).current,
					 kRank1Damage * 0.5f / kToughnessDivisor, 0.001f);

		const Observed before_retarget = observe(swing1);
		expect_true("the order is moved to another target", session.issue_attack(world, 1, 3));
		expect_true("the new order is the one held", is_target(swing1.attack_target(), 3));
		expect_unchanged("and retargeting spends nothing and hurries nothing", before_retarget, swing1,
						 false);

		// And the clock is still the same clock a moment later, with the same time gone.
		exhaust_defence(foe_b);
		expect_true("the same tick runs against the new target", session.step(0.0f, world));
		expect_close("a zero-time step leaves the clock exactly where it was",
					 static_cast<double>(swing1.attack_cooldown_remaining()), cooling, 1e-9);
		expect_close("and the new target is untouched by it",
					 static_cast<double>(foe_b.pool(open::PoolKind::Hp).current), 100.0, 1e-6);

		// Removal: reconciliation drops the order and nothing else.
		const std::vector<open::Combatant> no_foes = roster({{&swing1, at(0.0f, 0.0f, 0.0f)}});
		expect_true("a zero-time step with nobody in it is accepted", session.step(0.0f, no_foes));
		expect_true("the order on the absent sheet is gone", !swing1.attack_target().has_value());
		expect_close("and the clock it left behind is the same clock",
					 static_cast<double>(swing1.attack_cooldown_remaining()), cooling, 1e-9);

		// Death: the same, through a fight that ended rather than a roster that shrank.
		open::Character killer = make_character(1);
		open::Character doomed = make_character(2);
		const std::vector<open::Combatant> pair =
			roster({{&killer, at(0.0f, 0.0f, 0.0f)}, {&doomed, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession killer_session(96);
		expect_true("the killing fight is issued", killer_session.issue_attack(pair, 1, 2));
		exhaust_defence(doomed);
		expect_true("one tick swings", killer_session.step(0.05f, pair));
		const double after_kill = killer.attack_cooldown_remaining();
		expect_true("the killer's clock is running", after_kill > 0.0);
		expect_true("the victim is dying or dead", doomed.pool(open::PoolKind::Hp).current < 100.0f);
		doomed.damage(open::BodyPart::Torso, 1.0e30f);
		const Observed killer_after = observe(killer);
		expect_true("a tick with a corpse in it is accepted", killer_session.step(0.0f, pair));
		expect_true("the order on the corpse is cancelled", !killer.attack_target().has_value());
		expect_close("and the clock still reads what the swing set",
					 static_cast<double>(killer.attack_cooldown_remaining()),
					 static_cast<double>(killer_after.attack_clock), 1e-9);
	}

	{
		// swings_are_reported_once_to_whoever_asks: what the session performed, for
		// presentation to react to. A refused attempt is not an event; an event is a report of
		// a blow already applied, not a second application of it; and a drained queue stays
		// drained. Those three are what stop a swing animation playing for a swing that was
		// never taken, playing twice, or adding damage the simulation already added.
		open::Character hitter = make_character(1);
		open::Character second = make_character(3);
		open::Character target = make_character(2);
		const std::vector<open::Combatant> world =
			roster({{&hitter, at(0.0f, 0.0f, 0.0f)},
					{&target, at(1.0f, 0.0f, 0.0f)},
					{&second, at(0.5f, 0.0f, 0.0f)}});
		open::CombatSession session(141);
		expect_true("a fresh session has performed nothing", session.take_swing_events().empty());

		expect_true("a step with no orders runs", session.step(0.1f, world));
		expect_true("and reports no swing", session.take_swing_events().empty());

		exhaust_defence(hitter);
		exhaust_defence(target);
		expect_true("the fight is issued", session.issue_attack(world, 1, 2));
		const Observed before_hit = observe(target);
		expect_true("the first frame runs", session.step(0.1f, world));
		const std::vector<open::CombatEvent> first = session.take_swing_events();
		// The ordered blow, plus the retaliation it provoked: the victim is rostered
		// second, so its return swing is performed inside the same frame.
		expect_true("one performed blow, plus the return it provoked, is two events",
					first.size() == 2u);
		if (first.size() == 2u) {
			expect_true("the second event is the retaliation, aimed back at its provoker",
						first[1].actor == 2u && first[1].target == 1u);
		}
		if (!first.empty()) {
			expect_true("named by the two sheets it happened between",
						first[0].actor == 1u && first[0].target == 2u);
			// The same blow, twice: what the event reports and what the sheets were left
			// with. The tolerance is the subtraction's, not the claim's - both sides are
			// float, and taking two numbers that start near 100 apart leaves about five
			// digits to argue about. (The size itself is a quarter of an unanswered rank-1
			// blow here because both bars were drained: the swing was performed exhausted
			// and the answer unpaid, and each half is a rule with its own case.)
			expect_close("carrying the damage the target really took", first[0].swing.damage,
						 hp_lost(before_hit, target), 1e-5f);
			expect_true("and the part it was charged to, which is the engine's draw",
						first[0].swing.part.has_value());
			expect_true("with the technique it performed, which here is none",
						!first[0].swing.technique.has_value());
			// Both bars were drained, so the answer this blow met was a real but unpaid
			// dodge - and the event carries it, which is the whole reason the field is
			// copied with the swing rather than rebuilt downstream.
			expect_true("and with the defence it really met: the unpaid dodge",
						first[0].swing.defense_action.has_value() &&
							*first[0].swing.defense_action == open::DefendAction::Dodge);
			// The clock the swing started, carried whole: both bars were drained, so this
			// is the rank-1 exhausted doubling clamped to the band's top - presentation
			// scales its clip to this, not to whatever the clock reads at drain.
			expect_close("and the full five seconds its own clock was set to",
						 first[0].swing.attack_cooldown, 5.0f, 1e-6f);
		}
		expect_true("and asking again reports nothing: a drained queue is not a replay",
					session.take_swing_events().empty());

		const float hp_after_drain = target.pool(open::PoolKind::Hp).current;
		expect_true("a zero-time step is accepted", session.step(0.0f, world));
		expect_true("it reports nothing", session.take_swing_events().empty());
		expect_close("and reading events applied nothing of its own",
					 target.pool(open::PoolKind::Hp).current, hp_after_drain, 1e-9f);

		// A cooling clock performs nothing, so it reports nothing.
		expect_true("the next frame runs", session.step(0.1f, world));
		expect_true("with the attack still cooling, no event is queued",
					session.take_swing_events().empty());

		// Out of reach is not a swing, and neither is a corpse or a sheet with no order.
		open::Character distant = make_character(4);
		const std::vector<open::Combatant> far_world =
			roster({{&hitter, at(0.0f, 0.0f, 0.0f)}, {&distant, at(30.0f, 0.0f, 0.0f)}});
		open::CombatSession far(142);
		expect_true("the unreachable fight is issued", far.issue_attack(far_world, 1, 4));
		hitter.advance_combat_clocks(60.0f);
		expect_true("a frame runs", far.step(0.1f, far_world));
		expect_true("and no swing is reported", far.take_swing_events().empty());

		// A wholly turned-aside blow was still performed: it spent the swing, the stamina and
		// the clock, which is exactly why presentation has to be told about it.
		open::Character blocker = make_character(5);
		open::Character blocked = make_character(6);
		const std::vector<open::Combatant> duel =
			roster({{&blocker, at(0.0f, 0.0f, 0.0f)}, {&blocked, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession guarded(143);
		expect_true("the guarded fight is issued", guarded.issue_attack(duel, 5, 6));
		expect_true("a frame runs", guarded.step(0.1f, duel));
		const std::vector<open::CombatEvent> turned = guarded.take_swing_events();
		// Nullified is not unprovoking: the dodged sheet takes the attacker as its
		// order and, rostered second, returns the blow inside the same frame.
		expect_true("the turned-aside blow is reported, plus the return it provoked",
					turned.size() == 2u);
		if (!turned.empty()) {
			expect_true("reported as nullified, with no damage to show",
						turned[0].swing.nullified && turned[0].swing.damage == 0.0f);
			// The zero has a reason, and the event says which: this target was fresh, paid
			// for its answer, and the answer was a dodge. Nothing here can be guessed from
			// the damage figure, which is zero for a dozen other reasons too.
			expect_true("and as the dodge that turned it aside",
						turned[0].swing.defense_action.has_value() &&
							*turned[0].swing.defense_action == open::DefendAction::Dodge);
			// What a dodge worth everything prevents is the whole offensive - before the
			// Toughness soak a blow that arrives would have paid, which it now does not.
			// `weapon_combat` pins this same equality on a bigger blow.
			expect_close("and the defence the other sheet paid for prevented the whole swing",
						 turned[0].swing.prevented, kRank1Damage, 1e-6f);
			// Nullified is not unperformed: the paid clock started in full, and a
			// zero-damage event still carries the duration the swing spent cooling. The
			// number itself is this sheet's own (its effectiveness is not exactly 1), so
			// the oracle is the clock the swing set, same frame, untouched since.
			expect_true("the turned-aside blow reports a positive duration",
						turned[0].swing.attack_cooldown > 0.0f);
			expect_close("and it is exactly the clock that swing started",
						 turned[0].swing.attack_cooldown, blocker.attack_cooldown_remaining(), 1e-6f);
		}

		// Two attackers on one target: two events, in the order the roster was given, because
		// that is the order the swings happened in.
		open::Character left = make_character(7);
		open::Character right = make_character(8);
		open::Character middle = make_character(9);
		const std::vector<open::Combatant> three =
			roster({{&left, at(0.0f, 0.0f, 0.0f)},
					{&right, at(0.4f, 0.0f, 0.0f)},
					{&middle, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession crowded(144);
		exhaust_defence(left);
		exhaust_defence(right);
		exhaust_defence(middle);
		expect_true("the first attacker is ordered", crowded.issue_attack(three, 7, 9));
		expect_true("and the second", crowded.issue_attack(three, 8, 9));
		expect_true("a frame runs", crowded.step(0.1f, three));
		const std::vector<open::CombatEvent> pair = crowded.take_swing_events();
		// Two ordered blows in roster order, and the retaliation the first one
		// provoked: middle is rostered last, so its return swing is performed in the
		// same frame, after both attackers had their turn.
		expect_true("two performed blows and the provoked return are three events",
					pair.size() == 3u);
		if (pair.size() == 3u) {
			expect_true("in roster order", pair[0].actor == 7u && pair[1].actor == 8u);
			expect_true("both against the sheet they were ordered at",
						pair[0].target == 9u && pair[1].target == 9u);
			// One defence clock, shared, spent by the first blow: the frame's two events
			// differ in exactly the one way the clocks say they should, in the order the
			// roster was given. A downstream listener that inferred the answer from damage
			// would have to know this to tell them apart.
			expect_true("the first met the defence that was ready",
						pair[0].swing.defense_action.has_value() &&
							*pair[0].swing.defense_action == open::DefendAction::Dodge);
			expect_true("and the second met the same defence cooling",
						!pair[1].swing.defense_action.has_value());
			expect_true("the third is middle's retaliation, aimed at the first sheet that hit it",
						pair[2].actor == 9u && pair[2].target == 7u);
		}
		expect_true("a frame reported once: the pair is not queued twice",
					crowded.take_swing_events().empty());

		// A dead sheet is not swung at, so a kill cannot keep generating events afterwards.
		open::Character killer = make_character(10);
		open::Character victim = make_character(11);
		const std::vector<open::Combatant> last =
			roster({{&killer, at(0.0f, 0.0f, 0.0f)}, {&victim, at(1.0f, 0.0f, 0.0f)}});
		open::CombatSession ending(145);
		exhaust_defence(killer);
		exhaust_defence(victim);
		expect_true("the killing fight is issued", ending.issue_attack(last, 10, 11));
		expect_true("a frame runs", ending.step(0.1f, last));
		// The blow and the return it provoked: the victim is only a corpse two lines
		// from here, so at the swing it still had every reason to order itself back.
		expect_true("the blow and its provoked return are reported together",
					ending.take_swing_events().size() == 2u);
		victim.damage(open::BodyPart::Torso, 1.0e30f);
		killer.advance_combat_clocks(60.0f);
		expect_true("the next frame runs", ending.step(0.1f, last));
		expect_true("with a corpse and a cancelled order, nothing is reported",
					ending.take_swing_events().empty());
	}

	return test_summary();
}
