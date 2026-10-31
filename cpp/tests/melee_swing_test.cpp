#include "open/character.hpp"
#include "open/combat_rules.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"
#include "open/move_order.hpp"
#include "open/weapon_rules.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <vector>

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

// A fresh sheet swings at trained rank 1: 8 damage x 1/200, one swing every
// 1/0.214 s. Nothing here sets a rank, a pool or a clock by hand. The typed path
// arrives at the same numbers by its own route - the locked unarmed fallback is 8
// blunt at speed 1, and every item below is a hand-carved 8-damage fixture whose
// components the scalar path never distinguished - which is the point: the switch
// replaces the placeholder without moving the balance it was tuned against.
constexpr float kRank1Damage = 0.04f;
constexpr float kRank1Cooldown = 1.0f / 0.214f;

// Rank 100 is 1/1.6 = 0.625 s and, with armour 0, 8 x 100/200 = 4 damage.
constexpr float kRank100Damage = 4.0f;
constexpr float kRank100Cooldown = 0.625f;

// A defence with nothing to pay its 8 with is still performed, at half. Two of the
// cases below measure a blow through such a defence; everywhere else the target is
// left with a full bar so that the first answer is a ready dodge (worth everything,
// and so spends its own clock), and the blow under test is the one that arrives while
// that clock is still running - unanswered, and at its own whole number.
constexpr float kUnpaidDefenceDivisor = 1.05f;  // base Toughness 1
inline float half_of(float hit) {
	return hit * 0.5f / kUnpaidDefenceDivisor;
}

// A blow that arrives, in the two ways a rank-1 blow can arrive: answered by a
// defence that could not pay (half), or by a defence that is still cooling from that
// answer (whole). Both divide by the defender's base Toughness.
inline float first_blow(float hit) {
	return hit * 0.5f / kUnpaidDefenceDivisor;
}

inline float answered_hit(float hit, bool defender_paid) {
	return defender_paid ? hit * 0.5f / kUnpaidDefenceDivisor : hit / kUnpaidDefenceDivisor;
}

// The same blow when the attacker also could not pay for it, which is a second
// halving: one for the swing, one for the defence.
inline float exhausted_attacker_hit(float hit, bool defender_paid) {
	return hit * 0.5f / kUnpaidDefenceDivisor * (defender_paid ? 0.5f : 1.0f);
}

// What one defence costs its owner, and what one swing costs the thing it is swung
// with: three separate numbers, three separate rules.
constexpr float kDefenceCost = 8.0f;
constexpr unsigned kWear = 1u;


// A part or main HP left with less than one more hit on it, so the next swing
// is what ends it. Sized to the half a blow delivers through a spent defence.
constexpr float kAlmostDead = 0.015f;

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

// The items these fights are swung with. Each one is exactly 8 of one component, so
// a swing's number is the sheet's rank and nothing else, and the only claims the
// items make are which job trains and what a block is worth.
constexpr open::DefinitionId kSwordDef = 501;
constexpr open::DefinitionId kGreatswordDef = 502;
constexpr open::DefinitionId kSpearDef = 503;
constexpr open::DefinitionId kTowerDef = 504;    // blocks everything
constexpr open::DefinitionId kBucklerDef = 505;  // blocks nothing at all
constexpr open::MaterialId kMaterial = 11;

const std::shared_ptr<const open::ItemCatalog> &catalogue() {
	static const std::shared_ptr<const open::ItemCatalog> made = [] {
		auto hand_weapon = [](open::DefinitionId id, const char *model, open::HandUse use,
							  open::DamageComponents damage, open::BasicSkill basic,
							  std::optional<open::AdvancedSkill> option) {
			open::ItemDefinition item;
			item.id = id;
			item.model_key = model;
			item.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
			item.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
			item.hand_use = use;
			open::WeaponDefinition weapon;
			weapon.job = {basic, option};
			weapon.stats.damage = damage;
			weapon.stats.min_reach = 0.0;
			weapon.stats.max_reach = 1.5;
			weapon.stats.speed = 1.0;
			weapon.stats.stamina_cost = 8.0;
			// Nothing here blocks: a weapon that could is a different claim, and the
			// sheets below need to answer with a dodge they can or cannot pay for.
			weapon.stats.block_percent = 0.0;
			weapon.stats.weight = 2.0;
			weapon.stats.required_trained_rank = 1;
			item.weapon = weapon;
			return item;
		};
		std::vector<open::ItemDefinition> items;
		// All 8 of one component, and that component is cut. Blunt is the one component
		// the stagger meter reads, and these fights would otherwise end in a stun,
		// whose "no defence at all" is a different rule from the one being measured;
		// `weapon_combat` owns stagger. What matters here is 8 x rank / 200.
		const open::DamageComponents eight{8.0, 0.0, 0.0};
		items.push_back(hand_weapon(kSwordDef, "Sword", open::HandUse::OneHanded, eight,
									open::BasicSkill::OneHandedWeapons, std::nullopt));
		items.push_back(hand_weapon(kGreatswordDef, "Greatsword", open::HandUse::TwoHanded, eight,
									open::BasicSkill::TwoHandedWeapons, std::nullopt));
		items.push_back(hand_weapon(kSpearDef, "Spear", open::HandUse::TwoHanded, eight,
									open::BasicSkill::Polearms, std::nullopt));

		auto board = [](open::DefinitionId id, const char *model, double mitigation) {
			open::ItemDefinition item;
			item.id = id;
			item.model_key = model;
			item.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
			item.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
			item.hand_use = open::HandUse::OneHanded;
			open::ShieldDefinition shield;
			shield.job = {open::BasicSkill::Blocking, std::nullopt};
			shield.block_percent = mitigation;
			shield.weight = 3.0;
			item.shield = shield;
			return item;
		};
		items.push_back(board(kTowerDef, "Tower_Shield", 100.0));
		items.push_back(board(kBucklerDef, "Buckler", 0.0));

		open::MaterialDefinition material;
		material.id = kMaterial;
		material.max_durability_by_quality = {100, 100, 100, 100, 100, 100, 100};
		auto built = open::ItemCatalog::create(items, {material});
		if (!built.has_value()) {
			g_failed++;
		}
		return std::make_shared<open::ItemCatalog>(std::move(built).value());
	}();
	return made;
}

open::Character sheet(std::uint64_t id) {
	const std::optional<open::Character> made = open::Character::create(config(id), catalogue());
	expect_true("the test character is created with a catalog", made.has_value());
	return *made;
}

// Carries one described item and puts it in a hand. Every step reports: a fixture
// that silently failed to hold its weapon would test the wrong thing.
void wield(open::Character &who, open::DefinitionId what, open::ItemId id, open::Slot where) {
	const std::optional<open::ItemInstance> instance =
		open::make_item_instance(*who.catalog(), what, kMaterial, id, 1);
	expect_true("the fixture can describe the item", instance.has_value());
	if (!instance.has_value()) {
		return;
	}
	expect_true("the item is carried", who.add_instance(*instance));
	expect_true("the item is held", who.equip(id, where));
}

// Takes a pool down to an exact figure, in one spend the sheet can afford. Fixtures
// that need "8 left" should say so, rather than adding up sprints from a starting 100
// that time and other swings have already moved.
void drain_to(open::Character &who, float want) {
	const float spare = who.pool(open::PoolKind::Stamina).current - want;
	// Sprinting costs 10 stamina a second, so the seconds asked for is a tenth of the
	// stamina the fixture wants gone.
	if (spare > 0.0f) {
		expect_true("the bar is drained to the figure the fixture asks for",
					who.spend(open::ResourceUse::Sprint, spare / open::kSprintStaminaPerSecond));
	}
	expect_close("the bar is where the fixture needs it",
				 who.pool(open::PoolKind::Stamina).current, want, 0.001f);
}

// Spends a target's whole bar, so its next answer is an unpaid one at half rather
// than a ready one that turns the blow aside. Draining is the only way to do this
// without spending the target's part draw, which the seeded cases below depend on.
void exhaust_defence(open::Character &who) {
	const float available = who.pool(open::PoolKind::Stamina).current;
	if (available > 0.0f) {
		expect_true("the defence is drained", who.spend_stamina(available));
	}
	expect_close("the defence has nothing left to pay with",
				 who.pool(open::PoolKind::Stamina).current, 0.0f, 1e-6);
}

// Waits out the attacker's own clock, and nothing else: a target that has just
// answered has a defence cooldown running, and this is the only way to swing at it
// unanswered. Advancing the target as well would hand its defence back, which is the
// opposite of what "the blow arrives" means here.
void ready_attacker(open::Character &who) {
	expect_true("the attacker feels its own clock", who.advance_combat_clocks(kRank1Cooldown));
	expect_close("its swing is ready again", who.attack_cooldown_remaining(), 0.0f, 0.001f);
	// Its bar regrows while it waits, which the fixtures below account for by setting
	// stamina from what is actually in the pool rather than from a hardcoded sprint.
	expect_true("the bar regrows in the meantime", who.regenerate(1.0f, false));
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

// --- review pass: finite is not one size ---------------------------------------------

// The catalog validates damage factors as positive and finite, and "finite" in double
// is a far bigger country than the float the sheet actually runs at. A blow that fits
// the first and not the second is no blow at all: it must be refused the way every
// other impossible attempt is - nothing spent, nothing trained, no clock started, and
// no random number consumed on the way out, because a refused attempt never happened.

constexpr open::DefinitionId kShoutDef = 506;
constexpr open::TechniqueId kShoutLeft = 971;
constexpr open::TechniqueId kShoutRight = 972;

std::shared_ptr<const open::ItemCatalog> shout_catalogue(double magnitude) {
	open::ItemDefinition item;
	item.id = kShoutDef;
	item.model_key = "Sword";
	item.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
	item.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
	item.hand_use = open::HandUse::OneHanded;
	open::WeaponDefinition weapon;
	weapon.job = {open::BasicSkill::OneHandedWeapons, std::nullopt};
	weapon.stats.damage = open::DamageComponents{8.0, 0.0, 0.0};
	weapon.stats.min_reach = 0.0;
	weapon.stats.max_reach = 1.5;
	weapon.stats.speed = 1.0;
	weapon.stats.stamina_cost = 8.0;
	weapon.stats.block_percent = 0.0;
	weapon.stats.weight = 2.0;
	weapon.stats.required_trained_rank = 1;
	weapon.supported_techniques = {kShoutLeft, kShoutRight};
	item.weapon = weapon;

	// Two techniques of the same magnitude: the pick between two candidates is
	// itself a draw, and that draw is exactly what a refused attempt must leave
	// the engine holding.
	auto shout = [](open::TechniqueId id, double magnitude) {
		open::TechniqueDefinition made;
		made.id = id;
		made.required_rank = 1;
		made.factors = {magnitude, magnitude, magnitude};
		made.cooldown = 5.0;
		return made;
	};

	open::MaterialDefinition material;
	material.id = kMaterial;
	material.max_durability_by_quality = {100, 100, 100, 100, 100, 100, 100};

	const auto built = open::ItemCatalog::create(
		{item}, {material}, {}, {shout(kShoutLeft, magnitude), shout(kShoutRight, magnitude)});
	if (!built.has_value()) {
		++g_failed;
		std::cerr << "FAIL the shout catalog could not be described\n";
	}
	return std::make_shared<open::ItemCatalog>(std::move(built).value());
}

open::Character shout_sheet(std::uint64_t id, double magnitude) {
	const std::optional<open::Character> made =
		open::Character::create(config(id), shout_catalogue(magnitude));
	expect_true("the shout sheet is created", made.has_value());
	return *made;
}

void extremes_stay_atomic() {
	const open::Vec3 self = at(0.0f, 0.0f, 0.0f);
	const open::Vec3 other = at(1.0f, 0.0f, 0.0f);

	// Case A: a factor near the top of double. Where the sheet's own fraction leaves
	// the product, no body-part hit could hold: finite as double perhaps, or not even
	// that, depending on rank - either way it has no float, and so no swing.
	{
		open::Character attacker = shout_sheet(1, 1.0e308);
		wield(attacker, kShoutDef, 971, open::Slot::RightHand);
		expect_true("the shout is aimed", attacker.set_attack_target(2));
		open::Character defender = make_character(2);
		// A fresh sheet's dodge is worth everything and would answer the shout before
		// arithmetic could: the bar goes first, so what this measures is the size of
		// the blow and not a nullified one.
		exhaust_defence(defender);
		std::mt19937_64 rng(20261030);
		std::mt19937_64 control(20261030);
		expect_true("a swing no float could carry is refused",
					!attacker.try_melee_swing(defender, self, other, rng).has_value());
		expect_true("and consumed no random number at all", rng == control);
	}

	// Case B: finite as double, bigger than any float - a number no body part can
	// hold. The attempt is refused before anything on either sheet commits.
	{
		open::Character attacker = shout_sheet(3, 1.0e300);
		wield(attacker, kShoutDef, 972, open::Slot::RightHand);
		expect_true("the shout is aimed", attacker.set_attack_target(4));
		open::Character defender = make_character(4);
		exhaust_defence(defender);
		const Observed before_attacker = observe(attacker);
		const Observed before_defender = observe(defender);
		std::mt19937_64 rng(31337);
		std::mt19937_64 control(31337);
		expect_true("a swing bigger than any float is refused",
					!attacker.try_melee_swing(defender, self, other, rng).has_value());
		expect_unchanged("a swing too big for float changes nothing on the attacker",
						 before_attacker, attacker);
		expect_unchanged("nor on the defender", before_defender, defender);
		expect_true("and spent no draw", rng == control);
	}
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

		std::mt19937_64 rng(7);
		// The target answers the first blow with a ready dodge, which is worth
		// everything, and the swing this block measures is the next one: unanswered,
		// because the defence is still cooling.
		const std::optional<open::SwingResult> turned_aside =
			attacker.try_melee_swing(target, self, other, rng);
		expect_true("a fresh sheet's own dodge turns the first blow aside",
					turned_aside.has_value() && turned_aside->nullified);
		ready_attacker(attacker);

		const Observed before_target = observe(target);
		const Observed before_attacker = observe(attacker);
		const std::optional<open::SwingResult> swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("a ready in-range swing is an attempt", swing.has_value());
		expect_true("the swing lands", swing.has_value() && !swing->nullified);
		// 8 blunt x 1/200 over 1.05 of Toughness: the same 0.04 the placeholder produced,
		// reached by the typed path's own arithmetic.
		expect_close("a rank-1 unarmed swing does 0.04 less Toughness",
					 swing.has_value() ? swing->damage : 0.0f, kRank1Damage / kUnpaidDefenceDivisor, 0.001f);
		expect_true("a landed hit names a part", swing.has_value() && swing->part.has_value());

		int hurt = 0;
		for (std::size_t i = 0; i < kPartCount; ++i) {
			const float lost = before_target.parts[i] - target.body_part(kParts[i]).current;
			if (!close(lost, 0.0f)) {
				hurt++;
				expect_close("that part takes exactly the swing", lost,
							 kRank1Damage / kUnpaidDefenceDivisor, 0.001f);
				expect_true("the part that moved is the part the swing named",
							is_part(swing->part, kParts[i]));
			}
		}
		expect_true("exactly one body part is hurt", hurt == 1);
		expect_one_hp_charge(target, before_target, kRank1Damage / kUnpaidDefenceDivisor,
							 "main HP is charged once for the hit");

		// Only the swing that landed cost anything: the cooling answer is not performed
		// and so is not charged.
		expect_close("the landing swing paid its 8 and nothing else did",
					 attacker.pool(open::PoolKind::Stamina).current, 92.0f);
		expect_close("an unanswered defence pays nothing",
					 target.pool(open::PoolKind::Stamina).current, before_target.stamina);
		const open::Training punch = attacker.basic(open::BasicSkill::UnarmedCombat);
		// One XP for the turned-aside swing is not this block's claim (a nullified blow
		// trains the attacker nothing, and is pinned so elsewhere); the landed one is the
		// second point.
		expect_true("the landed swing adds one residual XP and no rank",
					punch.rank == before_attacker.tracks[kUnarmed].rank &&
						punch.xp == before_attacker.tracks[kUnarmed].xp + 1u);
		expect_true("the defence that won earlier trains, and the cooling one does not",
					target.basic(open::BasicSkill::Dodging).xp == before_target.tracks[kDodging].xp);
		expect_true("neither level moves", attacker.level() == 1 && target.level() == 1);
		expect_true("no kill or block title is earned by a scratch",
					attacker.title_count(open::Title::Duelist) == 0u &&
						attacker.title_count(open::Title::Breaker) == 0u &&
						target.title_count(open::Title::Wall) == 0u);
		expect_close("the attack clock starts at the rank-1 cooldown", attacker.attack_cooldown_remaining(),
					 kRank1Cooldown);
		// The target felt no time of its own, so the clock from the turned-aside blow is
		// what let this one through: the wait belonged entirely to the attacker.
		expect_close("the turned-aside blow's defence clock is still running",
					 target.defend_cooldown_remaining(), kRank1Cooldown, 0.001f);
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
		// One blow is spent turning the target's ready defence aside, and from here the
		// target's own clock never moves again, so every swing below is measured against
		// a cooling defence: the attacker's own bar is the only thing changing.
		const std::optional<open::SwingResult> turned_aside =
			attacker.try_melee_swing(target, at(0.0f, 0.0f, 0.0f), other, rng);
		expect_true("the target's first answer turns the blow aside",
					turned_aside.has_value() && turned_aside->nullified);
		ready_attacker(attacker);
		// The bar is set from what is really in it rather than from a hardcoded sprint:
		// the fixture says "8 left" whatever the wait and the turn-aside cost.
		drain_to(attacker, 8.0f);
		expect_close("the fixture starts this swing at 8 stamina",
					 attacker.pool(open::PoolKind::Stamina).current, 8.0f);
		Observed before_hit = observe(target);
		std::optional<open::SwingResult> swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("a swing at exactly 8 stamina is still an attempt", swing.has_value());
		expect_true("a payable swing is not exhausted", swing.has_value() && !swing->nullified);
		expect_close("the payable swing does full damage", swing.has_value() ? swing->damage : 0.0f,
					 answered_hit(kRank1Damage, false), 0.001f);
		expect_one_hp_charge(target, before_hit, answered_hit(kRank1Damage, false),
							 "the payable swing charges main HP once");
		expect_close("and pays all its 8 stamina", attacker.pool(open::PoolKind::Stamina).current, 0.0f);
		expect_close("a payable swing is not slowed", attacker.attack_cooldown_remaining(), kRank1Cooldown);

		expect_true("stamina regenerates out of the swing", attacker.regenerate(1.0f, false));
		drain_to(attacker, 7.0f);
		expect_true("the attack clock is waited out", attacker.advance_combat_clocks(kRank1Cooldown));
		before_hit = observe(target);
		swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("an unpaid swing still happens", swing.has_value());
		// The attacker's own exhaustion already halves the offensive term; the target's
		// defence is cooling, so nothing halves it a second time.
		expect_close("an unpaid swing does half the offensive term", swing.has_value() ? swing->damage : 0.0f,
					 exhausted_attacker_hit(kRank1Damage, false), 0.001f);
		expect_one_hp_charge(target, before_hit, exhausted_attacker_hit(kRank1Damage, false),
							 "the unpaid swing charges main HP once");
		expect_close("an unpaid swing spends nothing", attacker.pool(open::PoolKind::Stamina).current, 7.0f);
		expect_close("a rank-1 unpaid swing is clamped to the slowest clock",
					 attacker.attack_cooldown_remaining(), 5.0f);

		drain_to(attacker, 0.0f);
		expect_true("the slow clock is waited out", attacker.advance_combat_clocks(5.0f));
		before_hit = observe(target);
		swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("an empty swing still happens", swing.has_value());
		expect_close("an empty swing does half the offensive term", swing.has_value() ? swing->damage : 0.0f,
					 exhausted_attacker_hit(kRank1Damage, false), 0.001f);
		expect_one_hp_charge(target, before_hit, exhausted_attacker_hit(kRank1Damage, false),
							 "the empty swing charges main HP once");
		expect_close("an empty swing spends nothing it does not have",
					 attacker.pool(open::PoolKind::Stamina).current, 0.0f);
		expect_close("an empty swing is still on the slowest clock", attacker.attack_cooldown_remaining(), 5.0f);

		// Recovered by the sheet's own regeneration, the next swing is normal again: the
		// half damage and half speed belonged to that one swing. One second out of
		// combat restores 10 stamina on a fresh sheet, which is just over the price.
		expect_true("stamina regenerates", attacker.regenerate(1.0f, false));
		expect_close("one second restores 10 stamina", attacker.pool(open::PoolKind::Stamina).current, 10.0f);
		expect_true("the slow clock is waited out again", attacker.advance_combat_clocks(5.0f));
		before_hit = observe(target);
		swing = attacker.try_melee_swing(target, self, other, rng);
		expect_true("the recovered swing is an attempt", swing.has_value());
		expect_close("the recovered swing does full damage again", swing.has_value() ? swing->damage : 0.0f,
					 answered_hit(kRank1Damage, false), 0.001f);
		expect_one_hp_charge(target, before_hit, answered_hit(kRank1Damage, false),
							 "the recovered swing charges main HP once");
		expect_close("the recovered swing pays again", attacker.pool(open::PoolKind::Stamina).current, 2.0f,
					 0.001f);
		expect_close("the recovered swing is back on the normal clock", attacker.attack_cooldown_remaining(),
					 kRank1Cooldown);
	}

	{
		// defence_is_paid_for_and_never_slows_for_tiredness: a defence has its own
		// cost (the plan's 8), and paying it is what keeps the mitigation whole. A
		// guard who cannot pay still blocks - and still on the plain rank formula,
		// never the doubled-then-clamped one a tired swing would use: a tired rank-1
		// defence clock would read 5 s against 1/0.214 here, so the two cannot be
		// confused. Break these catches: a defence that gates on stamina instead of
		// halving, one that slows because its owner is tired, and one that is free.
		// The shield is a 100% tower, so the only thing separating the two guards is
		// whether they could pay for the answer.
		const open::Vec3 self = at(0.0f, 0.0f, 0.0f);
		const open::Vec3 other = at(1.5f, 0.0f, 0.0f);
		std::mt19937_64 rng(211);

		for (const float drained : {8.0f, 9.3f}) {
			open::Character attacker = make_character(1);
			open::Character guard = sheet(2);
			wield(guard, kTowerDef, 905, open::Slot::LeftHand);
			expect_true("the guard sprints itself down", guard.spend(open::ResourceUse::Sprint, drained));
			const float start = 100.0f - drained * 10.0f;
			expect_close("the guard starts at the intended stamina",
						 guard.pool(open::PoolKind::Stamina).current, start);
			expect_true("the attack order is accepted", attacker.set_attack_target(2));

			const std::optional<open::SwingResult> swing =
				attacker.try_melee_swing(guard, self, other, rng);
			expect_true("the guard still attempts", swing.has_value());
			if (!swing.has_value()) {
				continue;
			}
			// 20 of 8 leaves exactly the cost: paid, whole, and the blow is turned
			// aside. 7.8 of 8 cannot: the same block is performed at half, so half of
			// the rank-1 blow arrives.
			const bool paid = start >= kDefenceCost;
			expect_true("a paid defence turns a 100% block into everything",
						swing->nullified == paid);
			expect_close("an unpaid defence lets half of it through", swing->damage,
						 paid ? 0.0f : half_of(kRank1Damage), 0.001f);
			expect_close("a defence costs its own 8, paid or not",
						 guard.pool(open::PoolKind::Stamina).current,
						 paid ? start - kDefenceCost : start);
			// The clock is the defender's own: an answer it could not pay for is
			// exhausted, and doubled within the band, exactly as an unpaid swing is. It
			// is the attacker's exhaustion that never touches this clock.
			expect_close("a paid defence clock is the plain rank formula",
						 guard.defend_cooldown_remaining(), paid ? kRank1Cooldown : 5.0f);
			expect_true("the guard owes no attack order", !guard.attack_target().has_value());
			// A blocking item wears for what it prevented. A paid 100% block prevents
			// the whole blow; an unpaid one prevents half, which is still something.
			expect_true("the shield wears for preventing damage",
						guard.instance(905)->durability == 100u - kWear);
			if (paid) {
				expect_true("and a guard that turned it aside is untouched",
							guard.pool(open::PoolKind::Hp).current == 100.0f);
			}
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
		exhaust_defence(target);
		const Observed before_target = observe(target);
		std::mt19937_64 rng(22);
		const std::optional<open::SwingResult> swing = attacker.try_melee_swing(target, at(0.0f, 0.0f, 0.0f),
																			   at(1.5f, 0.0f, 0.0f), rng);
		expect_true("the freshly unlocked specialisation is still an attempt", swing.has_value());
		expect_true("the unlocked specialisation does positive damage",
					swing.has_value() && swing->damage > 0.0f);
		expect_close("at the rank-1 unarmed number, through an unpaid defence",
					 swing.has_value() ? swing->damage : -1.0f, half_of(kRank1Damage), 0.001f);
		expect_true("and it names a part", swing.has_value() && swing->part.has_value());
		expect_one_hp_charge(target, before_target, half_of(kRank1Damage),
							 "the unlocked swing charges main HP once");
		expect_close("the swing pays its stamina",
					 attacker.pool(open::PoolKind::Stamina).current, 92.0f);
		expect_close("a rank-1 specialisation swings on the rank-1 clock",
					 attacker.attack_cooldown_remaining(), kRank1Cooldown);
		// The answer it could not pay for is exhausted, so its clock is the doubled
		// rank-1 figure clamped to the band: the defence is tired, not absent.
		expect_close("the rank-1 defence spends its own (exhausted) clock",
					 target.defend_cooldown_remaining(), 5.0f);

		const std::optional<open::Training> boxing = attacker.advanced(open::AdvancedSkill::Boxing);
		expect_true("exactly one Boxing XP is awarded and no rank",
					boxing.has_value() && boxing->rank == fresh_boxing->rank && boxing->xp == 1u);
		expect_true("the frozen basic stays at 100 with no residual XP",
					attacker.basic(open::BasicSkill::UnarmedCombat).rank == 100u &&
						attacker.basic(open::BasicSkill::UnarmedCombat).xp == 0u);
		// What trains a defence is prevention, not victory: this unpaid dodge still
		// turned half the blow aside, so it trains, and the nullify case pins the rule
		// from the other end (a defence that prevents nothing earns nothing).
		expect_true("the defence that prevented half trains once",
					target.basic(open::BasicSkill::Dodging).xp == before_target.tracks[kDodging].xp + 1u);
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
		// A ready, paid-for dodge is worth everything, so the first blow is turned aside
		// whole - and it still spends the defence that won.
		expect_true("a ready defence turns the first swing aside", first.has_value() && first->nullified);
		expect_close("a nullified blow does no damage", first->damage, 0.0f, 1e-6);
	expect_true("and the report says a dodge ran, not that nothing did",
				first.has_value() && first->defense_action.has_value() &&
					*first->defense_action == open::DefendAction::Dodge);
		expect_one_hp_charge(target, before_target, 0.0f, "a nullified swing charges main HP once");
		expect_close("a winning defence still spends its clock", target.defend_cooldown_remaining(),
					 kRank1Cooldown);
		expect_close("and its own cost", before_target.stamina - target.pool(open::PoolKind::Stamina).current,
					 kDefenceCost, 0.001f);

		expect_true("the attacker's clock is waited out", attacker.advance_combat_clocks(kRank100Cooldown));
		expect_true("the defender feels the same time", target.advance_combat_clocks(kRank100Cooldown));
		expect_close("the attacker is ready again", attacker.attack_cooldown_remaining(), 0.0f);
		expect_true("the defender is still cooling", target.defend_cooldown_remaining() > 0.0f);

		const std::optional<open::SwingResult> second = attacker.try_melee_swing(target, self, other, rng);
		expect_true("a cooling defence cannot stop the next swing", second.has_value() && !second->nullified);
		expect_true("and cannot report a defence either", second.has_value() &&
															  !second->defense_action.has_value());
		expect_close("the cooling defence lets the hit land in full", second.has_value() ? second->damage : 0.0f,
					 kRank100Damage / kUnpaidDefenceDivisor, 0.001f);
		expect_close("only the landed hit is charged to main HP",
					 before_target.hp - target.pool(open::PoolKind::Hp).current,
					 kRank100Damage / kUnpaidDefenceDivisor, 0.001f);
		expect_close("a cooling defence is not restarted by the attempt", target.defend_cooldown_remaining(),
					 kRank1Cooldown - kRank100Cooldown, 0.001f);
		expect_true("a cooling defence still earns nothing",
					target.basic(open::BasicSkill::Dodging).xp ==
						before_target.tracks[kDodging].xp + 1u);  // the one it won with
		expect_close("the second swing restarts the attack clock", attacker.attack_cooldown_remaining(),
					 kRank100Cooldown);
		expect_close("both swings paid 8 stamina", attacker.pool(open::PoolKind::Stamina).current, 84.0f);
		expect_true("combat never raises a capped weapon track past 100",
					attacker.basic(open::BasicSkill::UnarmedCombat).rank == 100u);
		expect_true("neither swing raises a level", attacker.level() == 1 && target.level() == 1);
	}

	{
		// nullify_awards_only_defender: a usable shield turns a rank-1 punch aside
		// with no part, no attacker XP, one Blocking XP, one Wall record and one point
		// of wear on the shield that prevented it.
		open::Character attacker = make_character(1);
		open::Character guard = sheet(2);
		wield(guard, kTowerDef, 901, open::Slot::LeftHand);
		expect_true("the attack order is accepted", attacker.set_attack_target(2));

		const Observed before_guard = observe(guard);
		const Observed before_attacker = observe(attacker);
		std::mt19937_64 rng(24);
		const std::optional<open::SwingResult> swing = attacker.try_melee_swing(guard, at(0.0f, 0.0f, 0.0f),
																			   at(1.5f, 0.0f, 0.0f), rng);
		expect_true("the swing against a shield is still an attempt", swing.has_value());
		expect_true("the shield nullifies it", swing.has_value() && swing->nullified);
		// A block worth everything, so a nullified zero names the block rather than
		// leaving presentation to work it out from the fact that nothing arrived.
		expect_true("a shield that stopped everything reports a block",
					swing.has_value() && swing->defense_action.has_value() &&
						*swing->defense_action == open::DefendAction::Block);
		expect_close("a nullified swing does no damage", swing.has_value() ? swing->damage : -1.0f, 0.0f);
		expect_true("a nullified swing names no part", swing.has_value() && !swing->part.has_value());
		// The shield's owner pays the defence and nothing else: no wound, no pool
		// drained but stamina, and no item destroyed.
		expect_close("a nullifying defence costs its owner the defence price",
					 before_guard.stamina - guard.pool(open::PoolKind::Stamina).current, kDefenceCost, 0.001f);
		const Observed after_nullify = observe(guard);
		expect_true("a nullify wounds no body part",
					[&] {
						for (std::size_t i = 0; i < kPartCount; ++i) {
							if (!close(before_guard.parts[i], after_nullify.parts[i])) {
								return false;
							}
						}
						return true;
					}());
		expect_true("and costs no main HP", close(before_guard.hp, after_nullify.hp));
		expect_true("and no mana", close(before_guard.mana, after_nullify.mana));
		expect_true("the shield wears once for the prevention",
					guard.instance(901)->durability == 100u - kWear);
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

		// A trained unarmed dodge nullifies the same way, and is never a Wall block or
		// an item wearing out.
		open::Character kicker = make_character(3);
		train(kicker, open::BasicSkill::Dodging, 20u);
		open::Character puncher = make_character(4);
		expect_true("the attack order is accepted", puncher.set_attack_target(3));
		const Observed before_kicker = observe(kicker);
		const std::optional<open::SwingResult> dodged = puncher.try_melee_swing(kicker, at(0.0f, 0.0f, 0.0f),
																			   at(1.5f, 0.0f, 0.0f), rng);
		expect_true("the swing at a trained dodger is an attempt", dodged.has_value());
		expect_true("a trained dodge nullifies a rank-1 punch", dodged.has_value() && dodged->nullified);
		expect_true("and reports the dodge, not the punch it stopped",
					dodged.has_value() && dodged->defense_action.has_value() &&
						*dodged->defense_action == open::DefendAction::Dodge);
		expect_close("the dodged swing does no damage", dodged.has_value() ? dodged->damage : -1.0f, 0.0f);

		expect_true("a dodger gains no attack order", !kicker.attack_target().has_value());
		const open::Training dodge = kicker.basic(open::BasicSkill::Dodging);
		expect_true("a nullifying dodge trains Dodging once",
					dodge.rank == before_kicker.tracks[kDodging].rank &&
						dodge.xp == before_kicker.tracks[kDodging].xp + 1u);
		expect_true("a dodge is not a Wall block", kicker.title_count(open::Title::Wall) == 0u);
		expect_close("a dodge costs its owner the same defence price, with nothing held",
					 before_kicker.stamina - kicker.pool(open::PoolKind::Stamina).current, kDefenceCost,
					 0.001f);
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
		// Both sheets' defences are ready, and a ready defence is worth everything, so
		// neither blow arrives; what this block is about is the clocks, and those start
		// whichever way the blow went.
		expect_true("b's swing is turned aside by a ready a", from_b.has_value() && from_b->nullified);
		expect_close("b's swing does no damage through a ready defence",
					 from_b.has_value() ? from_b->damage : 0.0f, 0.0f, 1e-6);
		expect_close("b starts its own attack clock", b.attack_cooldown_remaining(), kRank1Cooldown);
		expect_close("a's attack clock is not restarted by b", a.attack_cooldown_remaining(), kRank1Cooldown);
		expect_close("a's defence clock is spent by b's swing", a.defend_cooldown_remaining(), kRank1Cooldown);
		// Each sheet paid twice: b answered a's blow and then swung, and a answered b's
		// blow after swinging. No amount is shared or forgiven between them.
		expect_close("both of b's answers paid their own price",
					 b.pool(open::PoolKind::Stamina).current, 100.0f - 2.0f * kDefenceCost, 0.001f);
		expect_close("and so did a's", a.pool(open::PoolKind::Stamina).current,
					 100.0f - 2.0f * kDefenceCost, 0.001f);

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
			open::Character attacker = sheet(1);
			wield(attacker, kSwordDef, 950, open::Slot::LeftHand);
			expect_true("the attack order is accepted", attacker.set_attack_target(2));
			const std::uint64_t seed = seed_for_first_part(part);
			std::mt19937_64 rng(seed);

			// One part, one main-HP charge per blow, and one kill credit for the whole
			// fight. Head and torso die at their own zero; a limb has to finish the job
			// through main HP, so this runs until it does rather than pretending one
			// rank-1 blow can be timed to land the last point exactly.
			//
			// The first blow is answered by a defence with nothing to pay for itself
			// (half gets through); every blow after it finds that defence's clock still
			// running, because only the attacker feels time here, and so arrives whole.
			open::Character victim = make_character(2);
			const float before_hp = victim.pool(open::PoolKind::Hp).current;

			// One part, one main-HP charge per blow, and one kill credit for the whole
			// fight. Head and torso die at their own zero; a limb has to finish the job
			// through main HP, so the fight runs until it does rather than pretending one
			// rank-1 blow can be timed to land the last point exactly. The blow sizes are
			// not re-derived here - `weapon_combat` owns the typed arithmetic, and the
			// weapon trains as it hits, so a per-blow number would move under the test.
			// What must not move is the shape of a blow: one part, one main-HP charge,
			// nothing else, once per swing, and exactly one kill at the end.
			unsigned swings = 0;
			unsigned blows = 0;
			bool bad_part = false;
			bool bad_charge = false;
			while (victim.alive() && swings < 4000) {
				++swings;
				exhaust_defence(victim);          // answered, at half: never a free hit
				drain_to(attacker, 8.0f);         // paid: never an exhausted swing
				const float hp_before = victim.pool(open::PoolKind::Hp).current;
				std::array<float, kPartCount> parts_before{};
				for (std::size_t j = 0; j < kPartCount; ++j) {
					parts_before[j] = victim.body_part(kParts[j]).current;
				}
				const std::optional<open::SwingResult> swing =
					attacker.try_melee_swing(victim, self, other, rng);
				if (!swing.has_value()) {
					expect_true("the kill fight is refused mid-way", false);
					break;
				}
				if (swing->damage > 0.0f) {
					++blows;
					// One blow charges main HP once and exactly one body part once: the
					// same amount, give or take whichever of the two clamped at zero, and
					// never a second part and never a second helping of the difference.
					const float hp_lost = hp_before - victim.pool(open::PoolKind::Hp).current;
					unsigned moved = 0;
					std::optional<open::BodyPart> mover;
					float mover_lost = 0.0f;
					for (std::size_t j = 0; j < kPartCount; ++j) {
						const float lost = parts_before[j] - victim.body_part(kParts[j]).current;
						if (!close(lost, 0.0f, 1e-5f)) {
							++moved;
							mover = kParts[j];
							mover_lost = lost;
						}
					}
					bad_part |= moved != 1u || !swing->part.has_value() || !(mover == swing->part);
					bad_charge |= hp_lost > swing->damage + 1e-4f ||
								  mover_lost > swing->damage + 1e-4f ||
								  (hp_lost < swing->damage - 1e-4f &&
								   mover_lost < swing->damage - 1e-4f);
				} else {
					bad_part |= swing->part.has_value();
				}
				attacker.advance_combat_clocks(5.0f);
				victim.advance_combat_clocks(5.0f);
				attacker.regenerate(1.0f, false);
				victim.regenerate(1.0f, false);
			}
			expect_true("the fight ended in a death", !victim.alive());
			expect_true("every landed blow moved exactly one body part, the one it named",
						!bad_part);
			expect_true("and charged main HP and that part once each, never twice",
						!bad_charge);
			expect_true("main HP reached zero, never below",
						victim.pool(open::PoolKind::Hp).current == 0.0f);
			expect_true("no part was left negative",
						[&victim] {
							for (const open::BodyPart each : kParts) {
								if (victim.body_part(each).current < 0.0f) {
									return false;
								}
							}
							return true;
						}());
			expect_true("the victim died from the blows it was given", blows > 0u);
			expect_true("main HP was charged for the fight and is now empty",
						before_hp - victim.pool(open::PoolKind::Hp).current > 0.0f);
			expect_true("exactly one kill credit for the whole fight",
						attacker.title_count(open::Title::Duelist) == 1u);
			expect_true("no other title moves",
						attacker.title_count(open::Title::Breaker) == 0u &&
							attacker.title_count(open::Title::Wall) == 0u);
			expect_true("the fight trains the weapon for landing blows",
						attacker.basic(open::BasicSkill::OneHandedWeapons).xp > 0u);
			expect_true("the victim is credited no block, and a dead sheet keeps nothing",
						victim.title_count(open::Title::Wall) == 0u && !victim.alive());

			// A dead target is not a swing: no damage, no XP, no title, and the
			// attacker keeps its stale order instead of acquiring a new one.
			const unsigned xp_after_kill = attacker.basic(open::BasicSkill::OneHandedWeapons).xp;
			expect_no_attempt("a swing at the fresh corpse is refused", attacker, victim, self, other, seed);
			expect_true("a corpse trains nothing",
						attacker.basic(open::BasicSkill::OneHandedWeapons).xp == xp_after_kill);
			expect_true("the attacker does not acquire a new target after a kill",
						attacker.attack_target() == std::optional<std::uint64_t>(2));
		}

		{
			// A limb hit that ends the limb is not a death by itself, and a dead
			// limb's overkill is not charged to main HP a second time.
			open::Character attacker = sheet(1);
			wield(attacker, kSwordDef, 953, open::Slot::LeftHand);
			expect_true("the attack order is accepted", attacker.set_attack_target(2));
			open::Character victim = make_character(2);
			exhaust_defence(victim);
			const float first_hit = kRank1Damage * 0.5f / kUnpaidDefenceDivisor;
			const float leg_before = victim.body_part(open::BodyPart::LeftLeg).current;
			std::mt19937_64 rng(seed_for_first_part(open::BodyPart::LeftLeg));
			const std::optional<open::SwingResult> swing = attacker.try_melee_swing(victim, self, other, rng);
			expect_true("the limb swing lands", swing.has_value() && swing->damage > 0.0f);
			expect_true("the sampled part is the limb", is_part(swing->part, open::BodyPart::LeftLeg));
			expect_close("the limb is charged once, not twice",
						 leg_before - victim.body_part(open::BodyPart::LeftLeg).current, first_hit, 0.001f);
			expect_one_hp_charge(victim, observe(victim), 0.0f, "limb damage is not charged to main HP twice");
			expect_true("a wounded limb is not a dead character", victim.alive());
			expect_true("no kill credit for a limb",
						attacker.title_count(open::Title::Duelist) == 0u &&
							attacker.title_count(open::Title::Breaker) == 0u);
		}

		{
			// A two-handed weapon reports Breaker, not Duelist.
			open::Character attacker = sheet(1);
			wield(attacker, kGreatswordDef, 951, open::Slot::LeftHand);
			expect_true("the attack order is accepted", attacker.set_attack_target(2));
			open::Character victim = make_character(2);
			exhaust_defence(victim);
			leave_part_below_one_hit(victim, open::BodyPart::Torso, first_blow(kRank1Damage));
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
			exhaust_defence(victim);
			leave_part_below_one_hit(victim, open::BodyPart::Head, first_blow(kRank1Damage));
			std::mt19937_64 rng(seed_for_first_part(open::BodyPart::Head));
			const std::optional<open::SwingResult> swing = bare.try_melee_swing(victim, self, other, rng);
			expect_true("the unarmed kill swing lands", swing.has_value() && swing->damage > 0.0f);
			expect_true("the victim dies", !victim.alive());
			expect_true("an unarmed kill records no title",
						bare.title_count(open::Title::Duelist) == 0u && bare.title_count(open::Title::Breaker) == 0u);

			open::Character spearman = sheet(3);
			wield(spearman, kSpearDef, 952, open::Slot::LeftHand);
			expect_true("the attack order is accepted", spearman.set_attack_target(4));
			open::Character other_victim = make_character(4);
			exhaust_defence(other_victim);
			leave_part_below_one_hit(other_victim, open::BodyPart::Torso, first_blow(kRank1Damage));
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
			open::Character attacker = sheet(1);
			wield(attacker, kSwordDef, 953, open::Slot::LeftHand);
			expect_true("the attack order is accepted", attacker.set_attack_target(2));
			open::Character victim = make_character(2);
			exhaust_defence(victim);
			leave_part_below_one_hit(victim, open::BodyPart::LeftLeg, first_blow(kRank1Damage));
			const Observed before_victim = observe(victim);
			std::mt19937_64 rng(seed_for_first_part(open::BodyPart::LeftLeg));
			const std::optional<open::SwingResult> swing = attacker.try_melee_swing(victim, self, other, rng);
			expect_true("the limb swing lands", swing.has_value() && swing->damage > 0.0f);
			expect_true("the sampled part is the limb", is_part(swing->part, open::BodyPart::LeftLeg));
			expect_close("the limb is clamped at zero", victim.body_part(open::BodyPart::LeftLeg).current, 0.0f,
						 0.001f);
			expect_one_hp_charge(victim, before_victim, half_of(kRank1Damage),
								 "limb overkill is not charged twice");
			expect_true("a dead limb is not a dead character", victim.alive());
			expect_true("no kill credit for a limb",
						attacker.title_count(open::Title::Duelist) == 0u &&
							attacker.title_count(open::Title::Breaker) == 0u);
		}
	}

	extremes_stay_atomic();

	return test_summary();
}
