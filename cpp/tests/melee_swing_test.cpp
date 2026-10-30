#include "open/character.hpp"
#include "open/combat_rules.hpp"
#include "open/move_order.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>

#include "test_support.hpp"

// Task 3 swing tests. Break these catches: a refused swing that still pays
// stamina or spends the defender's clock, a defence clock only spent when it
// wins, XP granted for a nullify or a zero-damage swing, a kill credited twice
// or credited to an unarmed or polearm swing, main HP charged twice for one hit,
// exhaustion lasting past the swing that could not pay, a part draw that is not
// the six parts, and a clock reset because an order changed rather than because
// time passed.

namespace {

constexpr open::BodyPart kParts[] = {open::BodyPart::Head, open::BodyPart::Torso, open::BodyPart::LeftArm,
									 open::BodyPart::RightArm, open::BodyPart::LeftLeg, open::BodyPart::RightLeg};
constexpr std::size_t kPartCount = sizeof(kParts) / sizeof(kParts[0]);

// The tracks a swing may train, in one fixed order so an observation can name a
// slot instead of re-reading the enum at every assertion.
constexpr open::BasicSkill kTracks[] = {open::BasicSkill::UnarmedCombat, open::BasicSkill::OneHandedWeapons,
										open::BasicSkill::TwoHandedWeapons, open::BasicSkill::Polearms,
										open::BasicSkill::Blocking, open::BasicSkill::Dodging};
constexpr std::size_t kTrackCount = sizeof(kTracks) / sizeof(kTracks[0]);
constexpr std::size_t kUnarmed = 0;
constexpr std::size_t kOneHanded = 1;
constexpr std::size_t kBlocking = 4;
constexpr std::size_t kDodging = 5;

constexpr open::Title kTitles[] = {open::Title::Duelist, open::Title::Breaker, open::Title::Wall};
constexpr std::size_t kTitleCount = sizeof(kTitles) / sizeof(kTitles[0]);
constexpr std::size_t kDuelist = 0;
constexpr std::size_t kBreaker = 1;

// A fresh sheet swings at trained rank 1: weapon 8 x 1/200, one swing every
// 1/0.214 s. Nothing here sets a rank, a pool or a clock by hand.
constexpr float kRank1Damage = 0.04f;
constexpr float kRank1Cooldown = 1.0f / 0.214f;

// Rank 100 is 1/1.6 = 0.625 s and, with armour 0, weapon 8 x 100/200 = 4 damage.
constexpr float kRank100Damage = 4.0f;
constexpr float kRank100Cooldown = 0.625f;

// A part or main HP left with less than one more hit on it, so the next swing
// is what ends it.
constexpr float kAlmostDead = 0.03f;

open::Vec3 at(float x, float y, float z) {
	return open::Vec3{x, y, z};
}

open::CharacterConfig config(std::uint64_t id) {
	open::CharacterConfig made;
	made.id = id;
	made.name = "";
	made.faction = open::Faction::Friendly;
	made.level = 1;
	made.blessing = open::Attribute::Strength;
	return made;
}

open::Character make_character(std::uint64_t id) {
	const std::optional<open::Character> made = open::Character::create(config(id));
	expect_true("the test character is created", made.has_value());
	return *made;
}

open::ItemRef held(open::ItemId id, open::HandUse use, std::optional<open::BasicSkill> skill) {
	open::ItemRef item;
	item.id = id;
	item.hand_use = use;
	item.allowed_slots.set(static_cast<std::size_t>(static_cast<int>(open::Slot::LeftHand)));
	item.allowed_slots.set(static_cast<std::size_t>(static_cast<int>(open::Slot::RightHand)));
	item.combat_skill = skill;
	return item;
}

open::ItemRef one_handed(open::ItemId id, std::optional<open::BasicSkill> skill) {
	return held(id, open::HandUse::OneHanded, skill);
}

open::ItemRef two_handed(open::ItemId id, std::optional<open::BasicSkill> skill) {
	return held(id, open::HandUse::TwoHanded, skill);
}

open::ItemRef shield(open::ItemId id) {
	return held(id, open::HandUse::OneHanded, open::BasicSkill::Blocking);
}

// Training uses the existing progression APIs only.
void train(open::Character &c, open::BasicSkill basic, unsigned target) {
	for (int guard = 0; guard < 2000000 && c.basic(basic).rank < target; ++guard) {
		if (!c.record_use(basic, std::nullopt, true)) {
			expect_true("training the combat track is accepted", false);
			return;
		}
	}
	expect_true("the track reached the rank this test needs", c.basic(basic).rank >= target);
}

bool same_training(const std::optional<open::Training> &got, const std::optional<open::Training> &want) {
	if (got.has_value() != want.has_value()) {
		return false;
	}
	return !got.has_value() || (got->rank == want->rank && got->xp == want->xp);
}

// Everything a swing may touch on one sheet, so a refused attempt can be checked
// against the state before it.
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
	return made;
}

// Names the first field that moved, or nullptr when nothing did.
const char *first_difference(const Observed &before, const Observed &after) {
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
	if (before.order != after.order) {
		return "the attack order";
	}
	return nullptr;
}

void expect_unchanged(const char *name, const Observed &before, const open::Character &c) {
	const char *what = first_difference(before, observe(c));
	if (what != nullptr) {
		std::cerr << "FAIL " << name << ": " << what << " moved\n";
		g_failed++;
	}
}

// The pools this sheet owns: a defence may pay its clock and never these.
void expect_no_new_wounds(const char *name, const Observed &before, const open::Character &c) {
	const Observed after = observe(c);
	if (!close(after.hp, before.hp)) {
		std::cerr << "FAIL " << name << ": main HP " << before.hp << " -> " << after.hp << "\n";
		g_failed++;
	}
	if (!close(after.stamina, before.stamina)) {
		std::cerr << "FAIL " << name << ": stamina " << before.stamina << " -> " << after.stamina << "\n";
		g_failed++;
	}
	if (!close(after.mana, before.mana)) {
		std::cerr << "FAIL " << name << ": mana " << before.mana << " -> " << after.mana << "\n";
		g_failed++;
	}
	for (std::size_t i = 0; i < kPartCount; ++i) {
		if (!close(after.parts[i], before.parts[i])) {
			std::cerr << "FAIL " << name << ": body part " << static_cast<int>(kParts[i]) << " "
					  << before.parts[i] << " -> " << after.parts[i] << "\n";
			g_failed++;
		}
	}
}

bool is_part(const std::optional<open::BodyPart> &got, open::BodyPart want) {
	return got.has_value() && *got == want;
}

// The seed whose first part draw is the one this test wants: a swing that lands
// draws its part and nothing else, so the same seed drives the same hit.
std::uint64_t seed_for_first_part(open::BodyPart want) {
	for (std::uint64_t seed = 1; seed < 100000; ++seed) {
		std::mt19937_64 probe(seed);
		if (open::random_melee_part(probe) == want) {
			return seed;
		}
	}
	expect_true("a seed draws the part this test needs", false);
	return 1;
}

void expect_no_attempt(const char *name, open::Character &attacker, open::Character &defender,
					   const open::Vec3 &self, const open::Vec3 &other, std::uint64_t seed) {
	const Observed before_attacker = observe(attacker);
	const Observed before_defender = observe(defender);
	std::mt19937_64 rng(seed);
	const std::optional<open::SwingResult> swing = attacker.try_melee_swing(defender, self, other, rng);
	expect_true(name, !swing.has_value());
	expect_unchanged("a refused swing changes nothing on the attacker", before_attacker, attacker);
	expect_unchanged("a refused swing changes nothing on the defender", before_defender, defender);
}

// Brings one part to just under the next hit, with existing damage calls only,
// so the part and main HP move together exactly as the sheet says they do.
void leave_part_below_one_hit(open::Character &victim, open::BodyPart part, float hit) {
	const float room = victim.body_part(part).current - kAlmostDead;
	expect_true("the part still has health to remove", room > 0.0f);
	expect_true("the part is worn down", victim.damage(part, room));
	expect_close("the part is left just below one more hit", victim.body_part(part).current, kAlmostDead, 0.001f);
	expect_true("the part is below the next hit", victim.body_part(part).current < hit);
}

// Brings main HP to just under the next hit while keeping every part alive, so
// only the swing under test can finish the job.
void drain_main_hp_below_one_hit(open::Character &victim, float hit) {
	const open::BodyPart spare[] = {open::BodyPart::Head, open::BodyPart::Torso, open::BodyPart::LeftArm,
									open::BodyPart::RightArm};
	for (const open::BodyPart part : spare) {
		const float need = victim.pool(open::PoolKind::Hp).current - kAlmostDead;
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
	expect_true("main HP is left just below one more hit",
				victim.pool(open::PoolKind::Hp).current > 0.0f &&
					victim.pool(open::PoolKind::Hp).current < hit);
}

// The one thing every landed hit must do: charge main HP once, never twice, and
// never charge the difference left over from a clamped body part.
void expect_one_hp_charge(const open::Character &victim, const Observed &before, float hit, const char *what) {
	const float want = before.hp - hit > 0.0f ? before.hp - hit : 0.0f;
	const float got = victim.pool(open::PoolKind::Hp).current;
	if (!close(got, want, 0.001f)) {
		std::cerr << "FAIL " << what << ": main HP " << before.hp << " -> " << got << ", one hit of " << hit
				  << " would give " << want << "\n";
		g_failed++;
	}
}

}  // namespace

int main() {
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();

	{
		// rank_one_unarmed_hit: one attempt, one part, one HP charge, one XP, and
		// both participants left with their own clock. Height is not range.
		open::Character attacker = make_character(1);
		open::Character target = make_character(2);
		const open::Vec3 self = at(0.0f, 0.0f, 0.0f);
		const open::Vec3 other = at(1.5f, 25.0f, 0.0f);
		expect_true("the attack order names the defender", attacker.set_attack_target(2));

		const Observed before_target = observe(target);
		const Observed before_attacker = observe(attacker);

		std::mt19937_64 rng(7);
		const std::optional<open::SwingResult> swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("a ready in-range swing is an attempt", swing.has_value());
		expect_true("the swing lands", swing.has_value() && !swing->nullified);
		expect_close("a rank-1 unarmed swing does 0.04", swing.has_value() ? swing->damage : 0.0f, kRank1Damage);
		expect_true("a landed hit names a part", swing.has_value() && swing->part.has_value());

		int hurt = 0;
		for (std::size_t i = 0; i < kPartCount; ++i) {
			const float lost = before_target.parts[i] - target.body_part(kParts[i]).current;
			if (!close(lost, 0.0f)) {
				hurt++;
				expect_close("that part takes exactly the swing", lost, kRank1Damage, 0.001f);
				expect_true("the part that moved is the part the swing named",
							is_part(swing->part, kParts[i]));
			}
		}
		expect_true("exactly one body part is hurt", hurt == 1);
		expect_one_hp_charge(target, before_target, kRank1Damage, "main HP is charged once for the hit");

		expect_close("the swing pays 8 stamina", attacker.pool(open::PoolKind::Stamina).current, 92.0f);
		expect_close("the target spends nothing to be hit", target.pool(open::PoolKind::Stamina).current,
					 before_target.stamina);
		const open::Training punch = attacker.basic(open::BasicSkill::UnarmedCombat);
		expect_true("the landed swing adds one residual XP and no rank",
					punch.rank == before_attacker.tracks[kUnarmed].rank &&
						punch.xp == before_attacker.tracks[kUnarmed].xp + 1u);
		expect_true("the failed defence trains nothing",
					target.basic(open::BasicSkill::Dodging).xp == before_target.tracks[kDodging].xp);
		expect_true("neither level moves", attacker.level() == 1 && target.level() == 1);
		expect_true("no kill or block title is earned by a scratch",
					attacker.title_count(open::Title::Duelist) == 0u &&
						attacker.title_count(open::Title::Breaker) == 0u &&
						target.title_count(open::Title::Wall) == 0u);
		expect_close("the attack clock starts at the rank-1 cooldown", attacker.attack_cooldown_remaining(),
					 kRank1Cooldown);
		expect_close("the ready defence is spent even though it lost", target.defend_cooldown_remaining(),
					 kRank1Cooldown);
		expect_close("the passive defender never gains an attack clock", target.attack_cooldown_remaining(), 0.0f);
		expect_true("the passive defender gains no attack order", !target.attack_target().has_value());
		expect_close("the attacker's own defence clock is untouched", attacker.defend_cooldown_remaining(), 0.0f);

		expect_no_attempt("retrying before the clock is ready is not an attempt", attacker, target, self, other,
						  7);
	}

	{
		// Refused swings change nothing: no order, the wrong order, out of reach,
		// a dead attacker, a dead target, or an unusable observation.
		const open::Vec3 self = at(0.0f, 0.0f, 0.0f);
		const open::Vec3 in_range = at(1.5f, 0.0f, 0.0f);
		{
			open::Character attacker = make_character(1);
			open::Character target = make_character(2);
			expect_no_attempt("no attack order is not an attempt", attacker, target, self, in_range, 3);
			expect_true("an order on someone else is accepted", attacker.set_attack_target(3));
			expect_no_attempt("an order on another id is not an attempt at this target", attacker, target, self,
							  in_range, 3);
		}
		{
			open::Character attacker = make_character(1);
			open::Character target = make_character(2);
			expect_true("the attack order is accepted", attacker.set_attack_target(2));
			expect_no_attempt("just outside reach is not an attempt", attacker, target, self,
							  at(1.5001f, 0.0f, 0.0f), 4);
			expect_no_attempt("far outside reach is not an attempt", attacker, target, self, at(30.0f, 0.0f, 0.0f),
							  4);
			expect_no_attempt("a non-finite target position is not an attempt", attacker, target, self,
							  at(nan, 0.0f, 0.0f), 4);
			expect_no_attempt("a non-finite own position is not an attempt", attacker, target, at(inf, 0.0f, 0.0f),
							  in_range, 4);
		}
		{
			open::Character attacker = make_character(1);
			open::Character target = make_character(2);
			expect_true("the attack order is accepted", attacker.set_attack_target(2));
			expect_true("the attacker dies", attacker.damage(open::BodyPart::Torso, 500.0f));
			expect_no_attempt("a dead attacker does not swing", attacker, target, self, in_range, 5);
		}
		{
			open::Character attacker = make_character(1);
			open::Character target = make_character(2);
			expect_true("the attack order is accepted", attacker.set_attack_target(2));
			expect_true("the target dies first", target.damage(open::BodyPart::Torso, 500.0f));
			expect_no_attempt("a dead target is not swung at", attacker, target, self, in_range, 6);
			expect_close("the living attacker is still ready", attacker.attack_cooldown_remaining(), 0.0f);
		}
	}

	{
		// below_eight_is_per_swing: exactly 8 pays and is normal; 7 and 0 swing
		// unpaid, at half the offensive term and the doubled-then-clamped clock,
		// and the very next payable swing is normal again.
		const open::Vec3 self = at(0.0f, 0.0f, 0.0f);
		const open::Vec3 other = at(1.4f, 0.0f, 0.0f);

		open::Character attacker = make_character(1);
		open::Character target = make_character(2);
		std::mt19937_64 rng(21);
		expect_true("the attack order is accepted", attacker.set_attack_target(2));

		expect_true("stamina is drained to exactly 8", attacker.spend(open::ResourceUse::Sprint, 9.2f));
		expect_close("the fixture starts this swing at 8 stamina",
					 attacker.pool(open::PoolKind::Stamina).current, 8.0f);
		Observed before_hit = observe(target);
		std::optional<open::SwingResult> swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("a swing at exactly 8 stamina is still an attempt", swing.has_value());
		expect_true("a payable swing is not exhausted", swing.has_value() && !swing->nullified);
		expect_close("the payable swing does full damage", swing.has_value() ? swing->damage : 0.0f, kRank1Damage);
		expect_one_hp_charge(target, before_hit, kRank1Damage, "the payable swing charges main HP once");
		expect_close("and pays all its 8 stamina", attacker.pool(open::PoolKind::Stamina).current, 0.0f);
		expect_close("a payable swing is not slowed", attacker.attack_cooldown_remaining(), kRank1Cooldown);

		expect_true("stamina regenerates out of the swing", attacker.regenerate(1.0f, false));
		expect_true("stamina is drained to 7", attacker.spend(open::ResourceUse::Sprint, 0.3f));
		expect_close("the fixture starts the next swing at 7", attacker.pool(open::PoolKind::Stamina).current,
					 7.0f);
		expect_true("the attack clock is waited out", attacker.advance_combat_clocks(kRank1Cooldown));
		before_hit = observe(target);
		swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("an unpaid swing still happens", swing.has_value());
		expect_close("an unpaid swing does half the offensive term", swing.has_value() ? swing->damage : 0.0f,
					 0.02f);
		expect_one_hp_charge(target, before_hit, 0.02f, "the unpaid swing charges main HP once");
		expect_close("an unpaid swing spends nothing", attacker.pool(open::PoolKind::Stamina).current, 7.0f);
		expect_close("a rank-1 unpaid swing is clamped to the slowest clock",
					 attacker.attack_cooldown_remaining(), 5.0f);

		expect_true("stamina is drained to nothing", attacker.spend(open::ResourceUse::Sprint, 0.7f));
		expect_close("the fixture is empty", attacker.pool(open::PoolKind::Stamina).current, 0.0f);
		expect_true("the slow clock is waited out", attacker.advance_combat_clocks(5.0f));
		before_hit = observe(target);
		swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("an empty swing still happens", swing.has_value());
		expect_close("an empty swing does half the offensive term", swing.has_value() ? swing->damage : 0.0f,
					 0.02f);
		expect_one_hp_charge(target, before_hit, 0.02f, "the empty swing charges main HP once");
		expect_close("an empty swing spends nothing it does not have",
					 attacker.pool(open::PoolKind::Stamina).current, 0.0f);
		expect_close("an empty swing is still on the slowest clock", attacker.attack_cooldown_remaining(), 5.0f);

		// Recovered by the sheet's own regeneration, the next swing is normal: the
		// half damage and half speed belonged to that one swing.
		expect_true("stamina regenerates", attacker.regenerate(1.0f, false));
		expect_close("one second restores 10 stamina", attacker.pool(open::PoolKind::Stamina).current, 10.0f);
		expect_true("the slow clock is waited out again", attacker.advance_combat_clocks(5.0f));
		before_hit = observe(target);
		swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("the recovered swing is an attempt", swing.has_value());
		expect_close("the recovered swing does full damage again", swing.has_value() ? swing->damage : 0.0f,
					 kRank1Damage);
		expect_one_hp_charge(target, before_hit, kRank1Damage, "the recovered swing charges main HP once");
		expect_close("the recovered swing pays again", attacker.pool(open::PoolKind::Stamina).current, 2.0f);
		expect_close("the recovered swing is back on the normal clock", attacker.attack_cooldown_remaining(),
					 kRank1Cooldown);
	}

	{
		// exhausted_defence_pays_nothing: the plan forbids inventing a defence
		// stamina cost or a defence exhaustion penalty, so two guards are tried. A
		// guard who can afford a cost (20 stamina) must still end the attempt at 20,
		// and a tired guard (7) must still block on the plain rank formula rather
		// than the doubled-then-clamped one - a tired rank-1 block would clamp to 5
		// s against 1/0.214 here, so the two cannot be confused. Break these
		// catches: a block or dodge that charges stamina, gates on it, or slows down
		// because the defender is tired.
		const open::Vec3 self = at(0.0f, 0.0f, 0.0f);
		const open::Vec3 other = at(1.5f, 0.0f, 0.0f);
		std::mt19937_64 rng(211);

		for (const float drained : {8.0f, 9.3f}) {
			open::Character attacker = make_character(1);
			open::Character guard = make_character(2);
			expect_true("a shield is carried", guard.add_item(shield(905)));
			expect_true("the shield is held", guard.equip(905, open::Slot::LeftHand));
			expect_true("the guard sprints itself down", guard.spend(open::ResourceUse::Sprint, drained));
			const float start = 100.0f - drained * 10.0f;
			expect_close("the guard starts at the intended stamina",
						 guard.pool(open::PoolKind::Stamina).current, start);
			expect_true("the attack order is accepted", attacker.set_attack_target(2));

			const std::optional<open::SwingResult> swing =
				attacker.try_melee_swing(guard, self, other, rng);
			expect_true("the guard still attempts", swing.has_value());
			expect_true("and still nullifies", swing.has_value() && swing->nullified);
			expect_close("defending spends no stamina", guard.pool(open::PoolKind::Stamina).current, start);
			expect_close("a defence clock is never doubled for tiredness",
						 guard.defend_cooldown_remaining(), kRank1Cooldown);
			expect_true("the guard owes no attack order", !guard.attack_target().has_value());
			expect_true("and the guard is untouched", guard.pool(open::PoolKind::Hp).current == 100.0f);
		}
	}

	{
		// unlocked_specialisation_hits_and_trains_itself: the job the player just
		// chose is usable immediately. Training Unarmed Combat to its cap and
		// unlocking Boxing leaves a rank-1 Boxing track with nothing banked, so
		// the next swing at a fresh unarmoured dummy does positive damage, pays
		// its stamina, starts both clocks, and awards exactly one Boxing XP. The
		// frozen basic and the main level do not move.
		open::Character attacker = make_character(1);
		train(attacker, open::BasicSkill::UnarmedCombat, 100u);
		expect_true("the parent is frozen at its cap",
					attacker.basic(open::BasicSkill::UnarmedCombat).rank == 100u);
		expect_true("boxing is chosen", attacker.advance(open::AdvancedSkill::Boxing));
		const std::optional<open::Training> fresh_boxing = attacker.advanced(open::AdvancedSkill::Boxing);
		expect_true("the new specialisation starts at rank 1 with no XP",
					fresh_boxing.has_value() && fresh_boxing->rank == 1u && fresh_boxing->xp == 0u);
		expect_true("a swing with it resolves to that rank 1",
					attacker.resolve_skill(open::BasicSkill::UnarmedCombat, open::AdvancedSkill::Boxing).rank ==
						1u);

		open::Character target = make_character(2);
		expect_true("the attack order is accepted", attacker.set_attack_target(2));
		const Observed before_target = observe(target);
		std::mt19937_64 rng(22);
		const std::optional<open::SwingResult> swing = attacker.try_melee_swing(target, at(0.0f, 0.0f, 0.0f),
																			   at(1.5f, 0.0f, 0.0f), rng);
		expect_true("the freshly unlocked specialisation is still an attempt", swing.has_value());
		expect_true("a rank-1 dodge does not turn a rank-1 swing aside",
					swing.has_value() && !swing->nullified);
		expect_true("the unlocked specialisation does positive damage",
					swing.has_value() && swing->damage > 0.0f);
		expect_close("at the rank-1 unarmed number", swing.has_value() ? swing->damage : -1.0f, kRank1Damage);
		expect_true("and it names a part", swing.has_value() && swing->part.has_value());
		expect_one_hp_charge(target, before_target, kRank1Damage, "the unlocked swing charges main HP once");
		expect_close("the swing pays its stamina",
					 attacker.pool(open::PoolKind::Stamina).current, 92.0f);
		expect_close("a rank-1 specialisation swings on the rank-1 clock",
					 attacker.attack_cooldown_remaining(), kRank1Cooldown);
		expect_close("the rank-1 defence spends its own clock", target.defend_cooldown_remaining(),
					 kRank1Cooldown);

		const std::optional<open::Training> boxing = attacker.advanced(open::AdvancedSkill::Boxing);
		expect_true("exactly one Boxing XP is awarded and no rank",
					boxing.has_value() && boxing->rank == fresh_boxing->rank && boxing->xp == 1u);
		expect_true("the frozen basic stays at 100 with no residual XP",
					attacker.basic(open::BasicSkill::UnarmedCombat).rank == 100u &&
						attacker.basic(open::BasicSkill::UnarmedCombat).xp == 0u);
		expect_true("a losing defence trains nothing",
					target.basic(open::BasicSkill::Dodging).xp == before_target.tracks[kDodging].xp);
		expect_true("neither level moves", attacker.level() == 1 && target.level() == 1);
		expect_true("no kill or block title is earned here",
					attacker.title_count(open::Title::Duelist) == 0u &&
						attacker.title_count(open::Title::Breaker) == 0u &&
						target.title_count(open::Title::Wall) == 0u);

		// And the track the swing trained is the one the sheet keeps: a second
		// attempt inside the same clock is refused and pays nothing to it.
		expect_no_attempt("retrying before the clock is ready is not an attempt", attacker, target,
						  at(0.0f, 0.0f, 0.0f), at(1.5f, 0.0f, 0.0f), 22);
		expect_true("a refused attempt trains the specialisation not at all",
					same_training(attacker.advanced(open::AdvancedSkill::Boxing), boxing));
	}

	{
		// ready_defense_is_consumed_even_when_it_loses: a ready defence burns its
		// clock on the attempt, and the next swing then meets a cooling one.
		open::Character attacker = make_character(1);
		train(attacker, open::BasicSkill::UnarmedCombat, 100u);
		open::Character target = make_character(2);
		expect_true("the attack order is accepted", attacker.set_attack_target(2));
		const open::Vec3 self = at(0.0f, 0.0f, 0.0f);
		const open::Vec3 other = at(1.5f, 0.0f, 0.0f);

		const Observed before_target = observe(target);
		std::mt19937_64 rng(23);
		const std::optional<open::SwingResult> first = attacker.try_melee_swing(target, self, other, rng);
		expect_true("the first swing is an attempt", first.has_value());
		expect_true("the first swing lands", first.has_value() && !first->nullified);
		expect_close("a rank-100 unarmed swing does 4", first.has_value() ? first->damage : 0.0f, kRank100Damage);
		expect_one_hp_charge(target, before_target, kRank100Damage, "the rank-100 swing charges main HP once");
		expect_close("a losing defence still spends its clock", target.defend_cooldown_remaining(), kRank1Cooldown);
		expect_true("a losing defence trains nothing",
					target.basic(open::BasicSkill::Dodging).xp == before_target.tracks[kDodging].xp);

		expect_true("the attacker's clock is waited out", attacker.advance_combat_clocks(kRank100Cooldown));
		expect_true("the defender feels the same time", target.advance_combat_clocks(kRank100Cooldown));
		expect_close("the attacker is ready again", attacker.attack_cooldown_remaining(), 0.0f);
		expect_true("the defender is still cooling", target.defend_cooldown_remaining() > 0.0f);

		const std::optional<open::SwingResult> second = attacker.try_melee_swing(target, self, other, rng);
		expect_true("a cooling defence cannot stop the next swing", second.has_value() && !second->nullified);
		expect_close("the cooling defence lets the hit land in full", second.has_value() ? second->damage : 0.0f,
					 kRank100Damage);
		expect_close("both hits are charged once each", before_target.hp - target.pool(open::PoolKind::Hp).current,
					 kRank100Damage * 2.0f, 0.001f);
		expect_close("a cooling defence is not restarted by the attempt", target.defend_cooldown_remaining(),
					 kRank1Cooldown - kRank100Cooldown, 0.001f);
		expect_true("a cooling defence still earns nothing",
					target.basic(open::BasicSkill::Dodging).xp == before_target.tracks[kDodging].xp);
		expect_close("the second swing restarts the attack clock", attacker.attack_cooldown_remaining(),
					 kRank100Cooldown);
		expect_close("both swings paid 8 stamina", attacker.pool(open::PoolKind::Stamina).current, 84.0f);
		expect_true("combat never raises a capped weapon track past 100",
					attacker.basic(open::BasicSkill::UnarmedCombat).rank == 100u);
		expect_true("neither swing raises a level", attacker.level() == 1 && target.level() == 1);
	}

	{
		// nullify_awards_only_defender: a usable shield turns a rank-1 punch aside
		// with no part, no attacker XP, one Blocking XP and one Wall record.
		open::Character attacker = make_character(1);
		open::Character guard = make_character(2);
		expect_true("a shield is carried", guard.add_item(shield(901)));
		expect_true("the shield is held", guard.equip(901, open::Slot::LeftHand));
		expect_true("the attack order is accepted", attacker.set_attack_target(2));

		const Observed before_guard = observe(guard);
		const Observed before_attacker = observe(attacker);
		std::mt19937_64 rng(24);
		const std::optional<open::SwingResult> swing = attacker.try_melee_swing(guard, at(0.0f, 0.0f, 0.0f),
																			   at(1.5f, 0.0f, 0.0f), rng);
		expect_true("the swing against a shield is still an attempt", swing.has_value());
		expect_true("the shield nullifies it", swing.has_value() && swing->nullified);
		expect_close("a nullified swing does no damage", swing.has_value() ? swing->damage : -1.0f, 0.0f);
		expect_true("a nullified swing names no part", swing.has_value() && !swing->part.has_value());
		expect_no_new_wounds("a nullify costs the defender no pools", before_guard, guard);
		expect_close("the shield's owner blocks on its own rank", guard.defend_cooldown_remaining(),
					 kRank1Cooldown);
		expect_true("the blocker gains no attack order", !guard.attack_target().has_value());
		expect_close("the attacker still pays for a turned-aside swing",
					 attacker.pool(open::PoolKind::Stamina).current, 92.0f);
		expect_close("the attacker still starts its clock", attacker.attack_cooldown_remaining(), kRank1Cooldown);
		expect_true("the attacker trains nothing on a nullify",
					attacker.basic(open::BasicSkill::UnarmedCombat).xp == before_attacker.tracks[kUnarmed].xp);
		const open::Training blocking = guard.basic(open::BasicSkill::Blocking);
		expect_true("a nullifying block trains Blocking once",
					blocking.rank == before_guard.tracks[kBlocking].rank &&
						blocking.xp == before_guard.tracks[kBlocking].xp + 1u);
		expect_true("a nullifying block records Wall once", guard.title_count(open::Title::Wall) == 1u);
		expect_true("a nullify credits no kill title",
					attacker.title_count(open::Title::Duelist) == 0u &&
						attacker.title_count(open::Title::Breaker) == 0u);

		// A trained unarmed dodge nullifies the same way, and is never a Wall block.
		open::Character kicker = make_character(3);
		train(kicker, open::BasicSkill::Dodging, 20u);
		open::Character puncher = make_character(4);
		expect_true("the attack order is accepted", puncher.set_attack_target(3));
		const Observed before_kicker = observe(kicker);
		const std::optional<open::SwingResult> dodged = puncher.try_melee_swing(kicker, at(0.0f, 0.0f, 0.0f),
																			   at(1.5f, 0.0f, 0.0f), rng);
		expect_true("the swing at a trained dodger is an attempt", dodged.has_value());
		expect_true("a trained dodge nullifies a rank-1 punch", dodged.has_value() && dodged->nullified);
		expect_close("the dodged swing does no damage", dodged.has_value() ? dodged->damage : -1.0f, 0.0f);
		expect_no_new_wounds("a dodge costs the dodger no pools", before_kicker, kicker);
		expect_true("a dodger gains no attack order", !kicker.attack_target().has_value());
		const open::Training dodge = kicker.basic(open::BasicSkill::Dodging);
		expect_true("a nullifying dodge trains Dodging once",
					dodge.rank == before_kicker.tracks[kDodging].rank &&
						dodge.xp == before_kicker.tracks[kDodging].xp + 1u);
		expect_true("a dodge is not a Wall block", kicker.title_count(open::Title::Wall) == 0u);
		// Rank 20 is rate 0.2 + 2.8 x 0.1 = 0.48/s.
		expect_close("the dodge cools on its own rank", kicker.defend_cooldown_remaining(), 1.0f / 0.48f, 0.001f);
		expect_true("the puncher trains nothing when dodged",
					puncher.basic(open::BasicSkill::UnarmedCombat).xp == 0u);
		expect_close("the dodged puncher still cools", puncher.attack_cooldown_remaining(), kRank1Cooldown);
	}

	{
		// independent_clocks_survive_order_changes: two opposed orders each own
		// their clocks, and only time moves one character's clocks.
		open::Character a = make_character(1);
		open::Character b = make_character(2);
		const open::Vec3 self = at(0.0f, 0.0f, 0.0f);
		const open::Vec3 other = at(1.5f, 0.0f, 0.0f);
		expect_true("a orders an attack on b", a.set_attack_target(2));
		expect_true("b orders an attack on a", b.set_attack_target(1));

		std::mt19937_64 rng(25);
		const std::optional<open::SwingResult> from_a = a.try_melee_swing(b, self, other, rng);
		expect_true("a's swing is an attempt", from_a.has_value());
		expect_close("a starts its own attack clock", a.attack_cooldown_remaining(), kRank1Cooldown);
		expect_close("b's defence clock starts", b.defend_cooldown_remaining(), kRank1Cooldown);
		expect_close("b's own attack clock is untouched", b.attack_cooldown_remaining(), 0.0f);

		const std::optional<open::SwingResult> from_b = b.try_melee_swing(a, other, self, rng);
		expect_true("b swings on its own clock without waiting", from_b.has_value());
		expect_true("b's swing lands on a ready a", from_b.has_value() && !from_b->nullified);
		expect_close("b's swing does the same damage", from_b.has_value() ? from_b->damage : 0.0f, kRank1Damage);
		expect_close("b starts its own attack clock", b.attack_cooldown_remaining(), kRank1Cooldown);
		expect_close("a's attack clock is not restarted by b", a.attack_cooldown_remaining(), kRank1Cooldown);
		expect_close("a's defence clock is spent by b's swing", a.defend_cooldown_remaining(), kRank1Cooldown);

		expect_true("time is advanced for a alone", a.advance_combat_clocks(1.0f));
		const float attack_left = kRank1Cooldown - 1.0f;
		const float defend_left = kRank1Cooldown - 1.0f;
		expect_close("a's attack clock counted down", a.attack_cooldown_remaining(), attack_left, 0.001f);
		expect_close("a's defence clock counted down", a.defend_cooldown_remaining(), defend_left, 0.001f);
		expect_close("b's attack clock did not move", b.attack_cooldown_remaining(), kRank1Cooldown);
		expect_close("b's defence clock did not move", b.defend_cooldown_remaining(), kRank1Cooldown);

		// Cancelling, re-issuing, moving and retargeting never reset a clock.
		expect_true("the order is still there", a.attack_target() == std::optional<std::uint64_t>(2));
		a.clear_order();
		expect_close("clearing the order does not reset the attack clock", a.attack_cooldown_remaining(),
					 attack_left);
		expect_close("clearing the order does not reset the defence clock", a.defend_cooldown_remaining(),
					 defend_left);
		expect_true("a new attack order is accepted", a.set_attack_target(2));
		expect_close("a new attack order does not reset the attack clock", a.attack_cooldown_remaining(),
					 attack_left);
		expect_true("a move order is accepted", a.set_destination(20.0f, 0.0f, 0.0f));
		expect_close("a move order does not reset the attack clock", a.attack_cooldown_remaining(), attack_left);
		expect_true("retargeting is accepted", a.set_attack_target(3));
		expect_close("retargeting does not reset the attack clock", a.attack_cooldown_remaining(), attack_left);
		expect_close("retargeting does not reset the defence clock", a.defend_cooldown_remaining(), defend_left);
		expect_true("b changing its own order is accepted", b.set_destination(9.0f, 0.0f, 0.0f));
		expect_close("b keeps its own attack clock", b.attack_cooldown_remaining(), kRank1Cooldown);
		expect_close("b keeps its own defence clock", b.defend_cooldown_remaining(), kRank1Cooldown);

		// Invalid time never mutates; the exact remaining time leaves a clock ready.
		expect_true("negative time is refused", !a.advance_combat_clocks(-1.0f));
		expect_true("NaN time is refused", !a.advance_combat_clocks(nan));
		expect_true("infinite time is refused", !a.advance_combat_clocks(inf));
		expect_close("no invalid time moved the attack clock", a.attack_cooldown_remaining(), attack_left);
		expect_close("no invalid time moved the defence clock", a.defend_cooldown_remaining(), defend_left);
		expect_true("zero time is accepted", a.advance_combat_clocks(0.0f));
		expect_close("zero time changes nothing", a.attack_cooldown_remaining(), attack_left);
		expect_true("the exact remaining time is accepted", a.advance_combat_clocks(attack_left));
		expect_close("the exact remaining time leaves the clock ready", a.attack_cooldown_remaining(), 0.0f);
		expect_close("the other clock counted the same time", a.defend_cooldown_remaining(),
					 defend_left - attack_left, 0.001f);
		expect_true("more time than is left is accepted", a.advance_combat_clocks(1000.0f));
		expect_close("a clock never runs past ready", a.defend_cooldown_remaining(), 0.0f);
	}

	{
		// uniform_parts_are_seeded_and_cover_six: the sampler draws the six parts
		// and nothing else, about evenly, and an identical seed draws the identical
		// sequence.
		std::mt19937_64 first(42);
		std::mt19937_64 second(42);
		std::mt19937_64 other_seed(43);
		int counts[kPartCount] = {0};
		bool only_six = true;
		bool same_sequence = true;
		bool other_seed_differs = false;
		for (int draw = 0; draw < 6000; ++draw) {
			const open::BodyPart got = open::random_melee_part(first);
			if (!open::valid(got) || got == open::BodyPart::Count) {
				only_six = false;
				continue;
			}
			counts[static_cast<std::size_t>(static_cast<int>(got))]++;
			if (open::random_melee_part(second) != got) {
				same_sequence = false;
			}
			if (open::random_melee_part(other_seed) != got) {
				other_seed_differs = true;
			}
		}
		expect_true("only the six body parts come out of the sampler", only_six);
		for (std::size_t i = 0; i < kPartCount; ++i) {
			expect_true("every part is drawn about a sixth of the time", counts[i] >= 800 && counts[i] <= 1200);
		}
		expect_true("an identical seed draws an identical part sequence", same_sequence);
		expect_true("another seed does not draw the same sequence", other_seed_differs);
	}

	{
		// one_damage_call_and_one_kill_credit: every body part can be the lethal
		// one, each hit clamps its part and charges main HP once, and the kill is
		// reported exactly once and only to a weapon class that owns a title.
		const open::Vec3 self = at(0.0f, 0.0f, 0.0f);
		const open::Vec3 other = at(1.5f, 0.0f, 0.0f);

		for (std::size_t i = 0; i < kPartCount; ++i) {
			const open::BodyPart part = kParts[i];
			open::Character attacker = make_character(1);
			expect_true("a sword is carried",
						attacker.add_item(one_handed(950, open::BasicSkill::OneHandedWeapons)));
			expect_true("the sword is held", attacker.equip(950, open::Slot::LeftHand));
			expect_true("the attack order is accepted", attacker.set_attack_target(2));

			open::Character victim = make_character(2);
			const bool limb = part == open::BodyPart::LeftArm || part == open::BodyPart::RightArm ||
							  part == open::BodyPart::LeftLeg || part == open::BodyPart::RightLeg;
			if (limb) {
				// A limb cannot kill on its own: main HP is what runs out here.
				drain_main_hp_below_one_hit(victim, kRank1Damage);
			} else {
				leave_part_below_one_hit(victim, part, kRank1Damage);
			}
			const Observed before_victim = observe(victim);
			const Observed before_attacker = observe(attacker);

			const std::uint64_t seed = seed_for_first_part(part);
			std::mt19937_64 rng(seed);
			const std::optional<open::SwingResult> swing = attacker.try_melee_swing(victim, self, other, rng);
			expect_true("the kill swing is an attempt", swing.has_value());
			expect_true("the swing names the part the seed drew", is_part(swing->part, part));
			expect_close("the swing does its rank-1 damage", swing.has_value() ? swing->damage : 0.0f,
						 kRank1Damage);
			if (limb) {
				// A limb at zero is not a death, so the lethal outcome for a limb
				// hit is main HP running out: clamped at zero, and the limb itself
				// keeps whatever the one hit left on it.
				expect_close("main HP is clamped at zero, never negative", victim.pool(open::PoolKind::Hp).current,
							 0.0f, 0.001f);
				expect_true("the hit limb is reduced by one hit only",
							close(victim.body_part(part).current, before_victim.parts[i] - kRank1Damage, 0.001f));
			} else {
				// Head and torso at zero are deaths of their own, and the part
				// clamps instead of going negative.
				expect_close("the hit part is clamped at zero, never negative", victim.body_part(part).current,
							 0.0f, 0.001f);
			}
			expect_one_hp_charge(victim, before_victim, kRank1Damage, "the kill charges main HP once");
			expect_true("the victim dies from this one hit", !victim.alive());
			expect_true("exactly one Duelist kill is recorded",
						attacker.title_count(open::Title::Duelist) == before_attacker.titles[kDuelist] + 1u);
			expect_true("no other title moves",
						attacker.title_count(open::Title::Breaker) == before_attacker.titles[kBreaker] &&
							attacker.title_count(open::Title::Wall) == 0u);
			expect_true("the kill trains the weapon once",
						attacker.basic(open::BasicSkill::OneHandedWeapons).xp ==
							before_attacker.tracks[kOneHanded].xp + 1u);
			expect_true("the dead victim is credited nothing",
						victim.title_count(open::Title::Wall) == 0u &&
							victim.basic(open::BasicSkill::Dodging).xp == before_victim.tracks[kDodging].xp);

			// A dead target is not a swing: no damage, no XP, no title, and the
			// attacker keeps its stale order instead of acquiring a new one.
			expect_no_attempt("a swing at the fresh corpse is refused", attacker, victim, self, other, seed);
			expect_true("the attacker does not acquire a new target after a kill",
						attacker.attack_target() == std::optional<std::uint64_t>(2));
		}

		{
			// A two-handed weapon reports Breaker, not Duelist.
			open::Character attacker = make_character(1);
			expect_true("a great sword is carried",
						attacker.add_item(two_handed(951, open::BasicSkill::TwoHandedWeapons)));
			expect_true("the great sword is held", attacker.equip(951, open::Slot::LeftHand));
			expect_true("the attack order is accepted", attacker.set_attack_target(2));
			open::Character victim = make_character(2);
			leave_part_below_one_hit(victim, open::BodyPart::Torso, kRank1Damage);
			std::mt19937_64 rng(seed_for_first_part(open::BodyPart::Torso));
			const std::optional<open::SwingResult> swing = attacker.try_melee_swing(victim, self, other, rng);
			expect_true("the two-handed kill swing lands", swing.has_value() && swing->damage > 0.0f);
			expect_true("the victim dies", !victim.alive());
			expect_true("exactly one Breaker kill is recorded",
						attacker.title_count(open::Title::Breaker) == 1u);
			expect_true("a two-handed kill is not a Duelist kill",
						attacker.title_count(open::Title::Duelist) == 0u);
		}

		{
			// Unarmed and polearm kills add no title: nothing is invented here.
			open::Character bare = make_character(1);
			expect_true("the attack order is accepted", bare.set_attack_target(2));
			open::Character victim = make_character(2);
			leave_part_below_one_hit(victim, open::BodyPart::Head, kRank1Damage);
			std::mt19937_64 rng(seed_for_first_part(open::BodyPart::Head));
			const std::optional<open::SwingResult> swing = bare.try_melee_swing(victim, self, other, rng);
			expect_true("the unarmed kill swing lands", swing.has_value() && swing->damage > 0.0f);
			expect_true("the victim dies", !victim.alive());
			expect_true("an unarmed kill records no title",
						bare.title_count(open::Title::Duelist) == 0u && bare.title_count(open::Title::Breaker) == 0u);

			open::Character spearman = make_character(3);
			expect_true("a spear is carried", spearman.add_item(two_handed(952, open::BasicSkill::Polearms)));
			expect_true("the spear is held", spearman.equip(952, open::Slot::LeftHand));
			expect_true("the attack order is accepted", spearman.set_attack_target(4));
			open::Character other_victim = make_character(4);
			leave_part_below_one_hit(other_victim, open::BodyPart::Torso, kRank1Damage);
			std::mt19937_64 polearm_rng(seed_for_first_part(open::BodyPart::Torso));
			const std::optional<open::SwingResult> poked = spearman.try_melee_swing(other_victim, self, other,
																					polearm_rng);
			expect_true("the polearm kill swing lands", poked.has_value() && poked->damage > 0.0f);
			expect_true("the polearm victim dies", !other_victim.alive());
			expect_true("a polearm kill records no title",
						spearman.title_count(open::Title::Duelist) == 0u &&
							spearman.title_count(open::Title::Breaker) == 0u);
		}

		{
			// Overkill on a limb is not charged to main HP a second time, and a
			// limb at zero is not a death by itself.
			open::Character attacker = make_character(1);
			expect_true("a sword is carried",
						attacker.add_item(one_handed(953, open::BasicSkill::OneHandedWeapons)));
			expect_true("the sword is held", attacker.equip(953, open::Slot::LeftHand));
			expect_true("the attack order is accepted", attacker.set_attack_target(2));
			open::Character victim = make_character(2);
			leave_part_below_one_hit(victim, open::BodyPart::LeftLeg, kRank1Damage);
			const Observed before_victim = observe(victim);
			std::mt19937_64 rng(seed_for_first_part(open::BodyPart::LeftLeg));
			const std::optional<open::SwingResult> swing = attacker.try_melee_swing(victim, self, other, rng);
			expect_true("the limb swing lands", swing.has_value() && swing->damage > 0.0f);
			expect_true("the sampled part is the limb", is_part(swing->part, open::BodyPart::LeftLeg));
			expect_close("the limb is clamped at zero", victim.body_part(open::BodyPart::LeftLeg).current, 0.0f,
						 0.001f);
			expect_one_hp_charge(victim, before_victim, kRank1Damage, "limb overkill is not charged twice");
			expect_true("a dead limb is not a dead character", victim.alive());
			expect_true("no kill credit for a limb",
						attacker.title_count(open::Title::Duelist) == 0u &&
							attacker.title_count(open::Title::Breaker) == 0u);
		}
	}

	return test_summary();
}
