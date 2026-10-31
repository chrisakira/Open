#include "open/character.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <vector>

#include "open/character_rules.hpp"
#include "open/combat_rules.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"
#include "open/techniques.hpp"
#include "open/weapon_rules.hpp"
#include "test_support.hpp"

// Task 13: a technique performed inside the one attack pipeline.
//
// The selector (Task 12) answers a question; this suite is about the answer being
// *committed* exactly once by the swing that asked it. The lines that matter are all about
// counting: one clock per performed technique and never one for a neighbour, one attack
// cooldown per attempt whether the move was basic or a technique, one point of wear on the
// weapon, one XP for the hit and no second award for the flourish, one sequence advance for
// the attack that actually happened, and one draw of the body-part engine.
//
// Break these catches: a refused attempt that still advanced the sequence or started a
// technique clock, a technique that shared another's cooldown or that quietly replaced the
// attack cooldown, a move that cost the weapon's stamina instead of its own (or that got a
// discount because the cost was replaced before the effectiveness adjustment), a technique
// that failed to be performed for being blocked or soaked and then had its cooldown
// refunded, a clock that ticked through a stun, an equipment swap that reset a clock, an
// edit that reset a clock, and a selection re-run after the hit so the popup's preview and
// the swing disagree.

namespace {

constexpr open::DefinitionId kSword = 101;   // 6 cut / 2 pierce, cost 8, reach 0-1.5
constexpr open::DefinitionId kHammer = 104;  // 8 blunt, supports Heavy Strike only
constexpr open::DefinitionId kPike = 111;    // 0.6 minimum reach, so min-range stacks
constexpr open::DefinitionId kRuiner = 112;  // a 20-stamina technique, and a price to pay
constexpr open::DefinitionId kPlate = 107;   // 100% of everything, on the chest
constexpr open::DefinitionId kBuckler = 106;  // 0% mitigation, to make blocks measurable
constexpr open::EnchantmentId kBluntEdge = 201;
constexpr open::MaterialId kMaterial = 11;

constexpr std::uint64_t kAttackerId = 1;
constexpr std::uint64_t kDefenderId = 2;

// The locked starter ids, and the two fixture techniques this suite needs to say things the
// starters cannot (an own stamina cost, and an unlock behind a trained rank).
constexpr open::TechniqueId kSlash = 1;
constexpr open::TechniqueId kThrust = 2;
constexpr open::TechniqueId kHeavyStrike = 3;
constexpr open::TechniqueId kRuinator = 900;  // cost 20, factors 1/1/1, cooldown 12
constexpr open::TechniqueId kGate = 901;      // needs trained 50, cooldown 6

constexpr double kDefenseCost = 8.0;
constexpr double kToughnessDivisor = 1.05;  // a fresh sheet's base Toughness is 1

open::DamageComponents parts(double cut, double pierce, double blunt) {
	open::DamageComponents value;
	value.cut = cut;
	value.pierce = pierce;
	value.blunt = blunt;
	return value;
}

open::TechniqueDefinition technique(open::TechniqueId id, open::DamageComponents factors,
									double cooldown, std::optional<double> cost = std::nullopt,
									unsigned required_rank = 1) {
	open::TechniqueDefinition made;
	made.id = id;
	made.required_rank = required_rank;
	made.factors = factors;
	made.cooldown = cooldown;
	made.stamina_cost = cost;
	return made;
}

std::vector<open::TechniqueDefinition> techniques() {
	std::vector<open::TechniqueDefinition> made = open::starter_techniques();
	made.push_back(technique(kRuinator, parts(1.0, 1.0, 1.0), 12.0, 20.0));
	made.push_back(technique(kGate, parts(2.0, 1.0, 1.0), 6.0, std::nullopt, 50));
	return made;
}

const std::shared_ptr<const open::ItemCatalog> &catalogue() {
	static const std::shared_ptr<const open::ItemCatalog> made =
		[]() -> std::shared_ptr<const open::ItemCatalog> {
		auto weapon = [](open::DefinitionId id, const char *model, open::DamageComponents damage,
						 open::SkillJob job, std::vector<open::TechniqueId> supports,
						 double min_reach, double stamina_cost) {
			open::ItemDefinition item;
			item.id = id;
			item.model_key = model;
			item.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
			item.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
			item.hand_use = open::HandUse::OneHanded;
			open::WeaponDefinition role;
			role.job = job;
			role.stats.damage = damage;
			role.stats.penetration = parts(0.0, 0.0, 0.0);
			role.stats.min_reach = min_reach;
			role.stats.max_reach = 1.5;
			role.stats.speed = 1.0;
			role.stats.stamina_cost = stamina_cost;
			role.stats.block_percent = 25.0;
			role.stats.weight = 2.0;
			role.stats.requirements = {};
			role.stats.required_trained_rank = 1;
			role.supported_techniques = std::move(supports);
			item.weapon = role;
			return item;
		};
		const open::SkillJob one_handed{open::BasicSkill::OneHandedWeapons,
										open::AdvancedSkill::Sword};

		std::vector<open::ItemDefinition> items;
		items.push_back(weapon(kSword, "Sword", parts(6.0, 2.0, 0.0), one_handed, {kSlash, kThrust},
							   0.0, 8.0));
		items.push_back(weapon(kHammer, "Hammer_Small", parts(0.0, 0.0, 8.0),
							   {open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Mace},
							   {kHeavyStrike}, 0.0, 8.0));
		items.push_back(
			weapon(kPike, "Pike_Fixture", parts(4.0, 4.0, 0.0), one_handed, {kSlash}, 0.6, 8.0));
		items.push_back(weapon(kRuiner, "Ruiner_Fixture", parts(10.0, 0.0, 0.0), one_handed,
							   {kRuinator, kGate}, 0.0, 8.0));

		open::ItemDefinition buckler;
		buckler.id = kBuckler;
		buckler.model_key = "Buckler_Fixture";
		buckler.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
		buckler.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
		buckler.hand_use = open::HandUse::OneHanded;
		open::ShieldDefinition board;
		board.job = {open::BasicSkill::Blocking, std::nullopt};
		board.block_percent = 0.0;
		board.weight = 3.0;
		buckler.shield = board;
		items.push_back(buckler);

		open::ItemDefinition plate;
		plate.id = kPlate;
		plate.model_key = "Plate_Fixture";
		plate.allowed_slots.set(static_cast<std::size_t>(open::Slot::Chest));
		open::ArmourDefinition torso;
		torso.resistance_percent = parts(100.0, 100.0, 100.0);
		torso.weight = 4.0;
		plate.armour = torso;
		items.push_back(plate);

		open::MaterialDefinition material;
		material.id = kMaterial;
		material.max_durability_by_quality = {100, 100, 100, 100, 100, 100, 100};

		open::EnchantmentDefinition blunt_edge;
		blunt_edge.id = kBluntEdge;
		blunt_edge.model_key = "Blunt_Edge";
		blunt_edge.blunt_per_trained_rank = 0.01;
		blunt_edge.requirement = open::Attribute::Intelligence;
		blunt_edge.requirement_divisor = 20;

		const std::optional<open::ItemCatalog> built =
			open::ItemCatalog::create(items, {material}, {blunt_edge}, techniques());
		if (!built.has_value()) {
			g_failed++;
			std::cerr << "FAIL the technique combat catalog was rejected\n";
			return std::shared_ptr<const open::ItemCatalog>();
		}
		return std::make_shared<open::ItemCatalog>(std::move(built).value());
	}();
	return made;
}

open::Character sheet(std::uint64_t id) {
	open::CharacterConfig config;
	config.id = id;
	config.name = "";
	config.faction = open::Faction::Friendly;
	config.level = 1;
	config.blessing = open::Attribute::Strength;
	const std::optional<open::Character> made = open::Character::create(config, catalogue());
	expect_true("the technique combat sheet is created", made.has_value());
	return *made;
}

void wear(open::Character &who, open::DefinitionId definition, open::ItemId id,
		  open::Slot slot = open::Slot::RightHand, unsigned quality = 1) {
	const std::optional<open::ItemInstance> instance =
		open::make_item_instance(*who.catalog(), definition, kMaterial, id, quality);
	if (!instance.has_value() || !who.add_instance(*instance) || !who.equip(id, slot)) {
		g_failed++;
		std::cerr << "FAIL the technique fixture could not be worn\n";
	}
}

// Blunt Edge as the bench makes it: 0.01 blunt per rank of the enchanting job.
void enchant(open::Character &who, open::ItemId id, unsigned trained) {
	const open::EnchantmentDefinition *definition = catalogue()->enchantment(kBluntEdge);
	expect_true("Blunt Edge is defined", definition != nullptr);
	const std::optional<open::AppliedEnchantment> applied =
		definition != nullptr ? open::make_applied_enchantment(*definition, trained) : std::nullopt;
	expect_true("and it can be applied at the fixture's rank", applied.has_value());
	if (applied.has_value()) {
		expect_true("the weapon accepts it", who.attach_enchantment(id, *applied));
	}
}

void train_to_100(open::Character &who, open::BasicSkill basic) {
	while (who.resolve_skill(basic, std::nullopt).rank < 100u) {
		expect_true("the fixture can train", who.record_use(basic, std::nullopt, true));
	}
}

double stamina(const open::Character &who) {
	return static_cast<double>(who.pool(open::PoolKind::Stamina).current);
}

open::Vec3 at(float x, float y = 0.0f, float z = 0.0f) {
	open::Vec3 made;
	made.x = x;
	made.y = y;
	made.z = z;
	return made;
}

std::optional<open::SwingResult> swing(open::Character &attacker, open::Character &defender,
									   std::mt19937_64 &rng, float separation = 1.0f) {
	return attacker.try_melee_swing(defender, at(0.0f), at(separation), rng);
}

// The defender's answer is one clock, so the exchange has exactly two states worth
// measuring: the first blow meets a ready defence (which a fresh sheet pays for and so is
// worth everything), and every later blow meets a defence that has just spent itself. This
// spends that first answer, leaving the blow under test unanswered and therefore readable
// as the whole of its own arithmetic.
void spend_the_defenders_answer(open::Character &attacker, open::Character &defender,
								std::mt19937_64 &rng) {
	const std::optional<open::SwingResult> answered = swing(attacker, defender, rng);
	expect_true("the opening blow is answered", answered.has_value());
	defender.advance_combat_clocks(0.0f);  // the answer's clock is left running on purpose
	attacker.advance_combat_clocks(600.0f);
}

// One blow that meets no answer, so its number is the swing's own arithmetic and nothing
// else's. The defender's one ready defence is spent on an opening blow, the attacker waits
// out its own clock, and the defender is never given the time its answer needs - a paid
// defence is worth everything it can do and a half-paid one is worth a factor of two, so
// either of them would replace the number this case is about.
std::optional<open::SwingResult> unanswered_blow(open::Character &attacker,
												 open::Character &defender,
												 std::mt19937_64 &rng, float separation = 1.0f) {
	const std::optional<open::SwingResult> opening = swing(attacker, defender, rng, separation);
	expect_true("the opening blow is attempted", opening.has_value());
	attacker.advance_combat_clocks(600.0f);
	const std::optional<open::SwingResult> one = swing(attacker, defender, rng, separation);
	expect_true("and so is the blow under test", one.has_value());
	if (one.has_value()) {
		expect_close("with nothing at all preventing it", static_cast<double>(one->prevented), 0.0,
					 1e-6);
	}
	return one;
}

// Leaves the sheet unable to pay for its next defence: the answer is then performed at half
// rather than turning the whole blow aside, which is what makes a damage number measurable
// without waiting for a body part the test cannot aim at.
void exhaust_defence_of(open::Character &who) {
	const float available = who.pool(open::PoolKind::Stamina).current;
	if (available > 0.0f) {
		expect_true("the defence is drained", who.spend_stamina(available));
	}
}

double cooldown_of(const open::Character &who, open::TechniqueId id) {
	const open::TechniqueState state = who.technique_state();
	const auto found = state.cooldowns.find(id);
	return found == state.cooldowns.end() ? 0.0 : found->second;
}

bool knows_clock(const open::Character &who, open::TechniqueId id) {
	return who.technique_state().cooldowns.count(id) != 0;
}

open::TechniqueStep step_of(open::TechniqueId id, bool skip_on_cooldown) {
	open::TechniqueStep made;
	made.id = id;
	made.skip_on_cooldown = skip_on_cooldown;
	return made;
}

std::vector<open::TechniqueStep> sequence_of(std::vector<open::TechniqueStep> steps) {
	return steps;
}

// ------------------------------------------------------------------- performing

// The performed technique starts its own clock and the attack's own clock, wears the weapon
// once, trains the weapon once, and leaves every other technique's clock untouched.
void a_performed_technique_starts_its_own_clock_and_the_attack_clock() {
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	// An NPC state (the default) with a sword that supports Slash and Thrust: whatever it
	// picks, the claims below are the same for either.
	wear(attacker, kSword, 401);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(1);
	spend_the_defenders_answer(attacker, defender, rng);

	const open::Training weapon_before =
		attacker.basic(open::BasicSkill::OneHandedWeapons);
	const double wear_before = static_cast<double>(attacker.instance(401)->durability);
	const std::optional<open::SwingResult> hit = swing(attacker, defender, rng);
	expect_true("the swing happened", hit.has_value());
	if (!hit) {
		return;
	}
	expect_true("and it performed one of the sword's techniques",
				hit->technique == std::optional<open::TechniqueId>(kSlash) ||
					hit->technique == std::optional<open::TechniqueId>(kThrust));
	if (!hit->technique.has_value()) {
		return;
	}
	const bool slash = *hit->technique == kSlash;
	expect_close("the performed technique's own clock started at its own cooldown",
				 cooldown_of(attacker, *hit->technique), slash ? 6.0 : 8.0, 1e-9);
	expect_true("and the technique it did not perform has no clock at all: cooldowns are per "
				"technique, never shared",
				!knows_clock(attacker, slash ? kThrust : kSlash));
	expect_true("the attack cooldown started too, and this swing used both",
				attacker.attack_cooldown_remaining() > 0.0f);
	expect_close("the weapon wore exactly once for the attempt",
				 wear_before - static_cast<double>(attacker.instance(401)->durability), 1.0, 1e-9);
	expect_true("and the weapon trained once, with no second credit for the flourish",
				attacker.basic(open::BasicSkill::OneHandedWeapons).xp == weapon_before.xp + 1u &&
					attacker.basic(open::BasicSkill::OneHandedWeapons).rank == weapon_before.rank);
}

// A technique is performed when it is swung, not when it hurts. Three outcomes a blow can
// come back with - wholly turned aside, blocked by something worth nothing, and soaked to
// zero by armour - and the move is spent in every one of them: its own clock started, the
// weapon worn once, and no weapon XP for a hit that did no damage.
void a_technique_is_performed_even_when_it_does_not_hurt() {
	// One entry, performed every swing, so "the technique" is never a guess about what an
	// NPC drew.
	const std::vector<open::TechniqueStep> only_slash = sequence_of({step_of(kSlash, false)});

	auto sequenced = [&](open::Character &who, open::DefinitionId weapon, open::ItemId id,
						 std::uint64_t target_id) {
		wear(who, weapon, id);
		who.set_attack_target(target_id);
		who.set_player_controlled(true);
		expect_true("Slash is configured", who.configure_techniques(true, only_slash));
	};

	// 1. Wholly turned aside: the defender's ready, paid dodge takes all of it.
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	sequenced(attacker, kSword, 411, defender.id());
	std::mt19937_64 rng(2);
	const double wear_before = static_cast<double>(attacker.instance(411)->durability);
	const unsigned xp_before = attacker.basic(open::BasicSkill::OneHandedWeapons).xp;
	const std::optional<open::SwingResult> turned = swing(attacker, defender, rng);
	expect_true("the opening swing happened", turned.has_value());
	if (!turned) {
		return;
	}
	expect_true("and was nullified", turned->nullified);
	expect_true("it was the technique all the same",
				turned->technique == std::optional<open::TechniqueId>(kSlash));
	expect_true("and the answer that turned it aside was a dodge",
				turned->defense_action.has_value() &&
					*turned->defense_action == open::DefendAction::Dodge);
	expect_close("the technique is still spent, cooldown and all",
				 cooldown_of(attacker, kSlash), 6.0, 1e-9);
	expect_close("the weapon wore exactly once for the attempt",
				 wear_before - static_cast<double>(attacker.instance(411)->durability), 1.0, 1e-9);
	expect_true("and a blow that did no damage trained nothing",
				attacker.basic(open::BasicSkill::OneHandedWeapons).xp == xp_before);

	// 2. Met by a block worth nothing: a buckler that mitigates 0% is still a defence that
	// is attempted, paid for and named in the result, and the move is spent for it.
	open::Character blocker = sheet(31);
	open::Character swordsman = sheet(32);
	wear(blocker, kBuckler, 413, open::Slot::LeftHand);
	sequenced(swordsman, kSword, 414, blocker.id());
	const double block_wear_before = static_cast<double>(swordsman.instance(414)->durability);
	const std::optional<open::SwingResult> blocked = swing(swordsman, blocker, rng);
	if (!blocked.has_value()) {
		g_failed++;
		std::cerr << "FAIL the blocked exchange did not happen\n";
		return;
	}
	expect_true("the blow was answered by the held buckler", blocked->defense_item.has_value());
	expect_true("which prevented nothing, and is not a nullify",
				blocked->prevented == 0.0f && !blocked->nullified);
	expect_true("yet the report still calls it the block it was",
				blocked->defense_action.has_value() &&
					*blocked->defense_action == open::DefendAction::Block);
	expect_true("and arrived as the technique",
				blocked->technique == std::optional<open::TechniqueId>(kSlash));
	expect_close("with its clock started", cooldown_of(swordsman, kSlash), 6.0, 1e-9);
	expect_close("and one point of wear",
				 block_wear_before - static_cast<double>(swordsman.instance(414)->durability), 1.0,
				 1e-9);
	expect_true("while the hit that did land trained the weapon once",
				swordsman.basic(open::BasicSkill::OneHandedWeapons).xp == 1u);

	// 3. Soaked to nothing by armour: the part is drawn, the total is zero, and the move was
	// still made. Which part a blow takes is the engine's draw, so this walks until the
	// armoured one comes up; the defender's bar is emptied as it goes, so what answers the
	// blow is a half-paid dodge and not a nullify.
	open::Character plated = sheet(33);
	open::Character hitter = sheet(34);
	wear(plated, kPlate, 412, open::Slot::Chest);
	sequenced(hitter, kSword, 415, plated.id());
	bool soaked = false;
	for (int attempt = 0; attempt < 400 && !soaked; ++attempt) {
		hitter.advance_combat_clocks(600.0f);
		plated.advance_combat_clocks(600.0f);
		exhaust_defence_of(plated);
		const unsigned xp_now = hitter.basic(open::BasicSkill::OneHandedWeapons).xp;
		const std::optional<open::SwingResult> one = swing(hitter, plated, rng);
		if (!one.has_value() || !one->part.has_value()) {
			continue;
		}
		if (*one->part == open::BodyPart::Torso && one->damage == 0.0f && !one->nullified) {
			soaked = true;
			expect_true("an armour-zero blow still performed its technique",
						one->technique == std::optional<open::TechniqueId>(kSlash) &&
							cooldown_of(hitter, kSlash) > 0.0);
			// The half-paid dodge is what this answer was, and the report says so even
			// though the armour, not the defence, is why the total came to nothing.
			expect_true("and it reports the dodge that ran behind the armour",
						one->defense_action.has_value() &&
							*one->defense_action == open::DefendAction::Dodge);
			expect_true("and it earned no weapon XP for a hit that did no damage",
						hitter.basic(open::BasicSkill::OneHandedWeapons).xp == xp_now);
		}
	}
	expect_true("the sweep found the armoured part", soaked);
}
// A refused attempt is not a swing at all: no luck spent, no sequence advance, no clock, no
// wear, no stamina. The selection happens after prevalidation precisely so that this is
// true, and every one of those four is cheap to lose.
void a_refused_attempt_commits_nothing() {
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kRuiner, 421);
	// A configured player: the sequence is the thing that must not move.
	std::vector<open::TechniqueStep> steps = sequence_of({step_of(kRuinator, false)});
	expect_true("the sequence is configured", attacker.configure_techniques(true, steps));
	expect_true("and the player is driving", attacker.set_player_controlled(true));
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(3);

	const open::TechniqueState before = attacker.technique_state();
	const double wear_before = static_cast<double>(attacker.instance(421)->durability);
	const double stamina_before = stamina(attacker);

	// Out of reach.
	expect_true("out of reach is refused", !swing(attacker, defender, rng, 9.0f).has_value());
	// Still no order: a sheet cannot be talked out of an order it was given, so the case
	// needs one that never received it.
	open::Character never_ordered = sheet(8);
	expect_true("with no order is refused",
				!swing(never_ordered, defender, rng).has_value() &&
					!never_ordered.attack_target().has_value());
	// A dead defender.
	open::Character corpse = sheet(7);
	expect_true("the corpse is dead", corpse.damage(open::BodyPart::Torso, 1.0e30f));
	expect_true("a corpse is not attacked",
				!attacker.try_melee_swing(corpse, at(0.0f), at(1.0f), rng).has_value());
	// A cooling clock.
	const std::optional<open::SwingResult> first = swing(attacker, defender, rng);
	expect_true("the first real swing happened", first.has_value());
	expect_true("and it performed the configured technique",
				first.has_value() && first->technique == std::optional<open::TechniqueId>(kRuinator));
	const open::TechniqueState performed = attacker.technique_state();
	expect_close("its clock started at 12", cooldown_of(attacker, kRuinator), 12.0, 1e-9);
	expect_true("and a one-entry sequence loops back to its beginning", performed.index == 0u);
	const std::optional<open::SwingResult> cooling = swing(attacker, defender, rng);
	expect_true("while the attack clock runs, a second swing is refused", !cooling.has_value());

	// And none of the refusals above moved anything that a swing should move.
	const open::TechniqueState after = attacker.technique_state();
	expect_true("the sequence is no further along than the one swing left it",
				after.index == performed.index);
	expect_close("no extra technique clock started", cooldown_of(attacker, kRuinator), 12.0, 1e-9);
	expect_close("the weapon wore once, for the one attempt",
				 wear_before - static_cast<double>(attacker.instance(421)->durability), 1.0, 1e-9);
	expect_true("and the only stamina gone is the one swing's, plus nothing else",
				stamina_before - stamina(attacker) >= 19.0 &&
					stamina_before - stamina(attacker) <= 21.0);
	std::mt19937_64 quiet(3);
	std::mt19937_64 quiet_copy(quiet);
	open::Character idle = sheet(kAttackerId);
	expect_true("a swing refused for want of an order draws nothing",
				!swing(idle, defender, quiet, 9.0f).has_value() && quiet() == quiet_copy());
}

// --------------------------------------------------------------------- sequences

// Skip, Wait, an empty sequence, and a sequence with nothing usable in it - the four ways a
// configured player can end up swinging something other than what they listed.
void skip_waits_loops_and_gives_up() {
	// Timed on a rank-100 sword: its swing cooldown is well under a second, so advancing a
	// whole second between blows always means "one swing due, one second of the technique's
	// own cooldown spent", and the 6 and 8 second technique clocks are still running by the
	// time the third blow is due. Integer seconds on both clocks is the whole point: every
	// number below is exact.
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	std::mt19937_64 rng(20);
	train_to_100(attacker, open::BasicSkill::OneHandedWeapons);
	wear(attacker, kSword, 431);
	attacker.set_attack_target(kDefenderId);
	attacker.set_player_controlled(true);
	const auto attack_is_due = [&](open::Character &who) {
		who.advance_combat_clocks(1.0f);
	};

	// Wait: an entry that is not ready yet keeps its place, and the character attacks
	// basically rather than reaching for anything else it happens to have.
	const std::vector<open::TechniqueStep> waiting = sequence_of({step_of(kSlash, false)});
	expect_true("the wait sequence is configured", attacker.configure_techniques(true, waiting));
	const std::optional<open::SwingResult> first = swing(attacker, defender, rng);
	expect_true("the entry is performed",
				first.has_value() && first->technique == std::optional<open::TechniqueId>(kSlash));
	expect_true("and a one-entry sequence loops back to it", attacker.technique_state().index == 0u);
	attack_is_due(attacker);
	const std::optional<open::SwingResult> held = swing(attacker, defender, rng);
	expect_true("while the entry waits, the character performs a basic attack",
				held.has_value() && !held->technique.has_value());
	expect_true("with the waited entry still next in line",
				attacker.technique_state().index == 0u);
	expect_close("and its clock ran for exactly the second that passed, no more",
				 cooldown_of(attacker, kSlash), 5.0, 1e-9);
	attacker.advance_combat_clocks(5.0f);
	const std::optional<open::SwingResult> kept = swing(attacker, defender, rng);
	expect_true("the waited entry was kept rather than consumed by the wait, and is performed "
				"once it is ready",
				kept.has_value() && kept->technique == std::optional<open::TechniqueId>(kSlash));

	// Skip: the same two entries allowed to walk past their own clocks, on a timeline in
	// whole seconds - the swing is due every 1 s of it, so every number is exact:
	//   t=0.0  Thrust performed (cooling until 8) -> next entry is Slash
	//   t=1.0  Slash performed  (cooling until 7) -> next entry is Thrust
	//   t=2.0  neither ready: a basic attack, and the loop is no further on
	//   t=7.0  Thrust has 1 s left and Slash has come ready: the loop passes Thrust by,
	//          performs the entry behind it, and leaves the clock it skipped untouched
	open::Character other = sheet(11);
	train_to_100(other, open::BasicSkill::OneHandedWeapons);
	wear(other, kSword, 432);
	other.set_attack_target(kDefenderId);
	other.set_player_controlled(true);
	const std::vector<open::TechniqueStep> skipping =
		sequence_of({step_of(kThrust, true), step_of(kSlash, true)});
	expect_true("the skip sequence is configured", other.configure_techniques(true, skipping));
	const std::optional<open::SwingResult> skip_first = swing(other, defender, rng);
	expect_true("the first entry is performed",
				skip_first.has_value() &&
					skip_first->technique == std::optional<open::TechniqueId>(kThrust));
	expect_true("and the sequence moved to the second entry", other.technique_state().index == 1u);
	attack_is_due(other);
	const std::optional<open::SwingResult> skip_second = swing(other, defender, rng);
	expect_true("the second entry follows",
				skip_second.has_value() &&
					skip_second->technique == std::optional<open::TechniqueId>(kSlash));
	expect_close("the first technique's clock ran while it was not the turn",
				 cooldown_of(other, kThrust), 7.0, 1e-9);

	attack_is_due(other);
	const std::size_t before_empty_loop = other.technique_state().index;
	const std::optional<open::SwingResult> nothing_ready = swing(other, defender, rng);
	expect_true("with every entry cooling and allowed to skip, the attack is basic",
				nothing_ready.has_value() && !nothing_ready->technique.has_value());
	expect_true("and the sequence is left where the one-pass loop put it",
				other.technique_state().index == before_empty_loop);
	expect_close("the loop spent no clock of its own: both entries lost only the seconds they "
				 "were owed",
				 cooldown_of(other, kSlash), 5.0, 1e-9);

	other.advance_combat_clocks(5.0f);  // t=7.0: Thrust 1 s short of ready, Slash ready
	const std::optional<open::SwingResult> skipped_past = swing(other, defender, rng);
	expect_true("a cooling entry configured to skip is walked past, and the one behind it is "
				"performed",
				skipped_past.has_value() &&
					skipped_past->technique == std::optional<open::TechniqueId>(kSlash));
	expect_close("the entry that was passed over keeps exactly the clock it had - skipped is "
				 "not refunded",
				 cooldown_of(other, kThrust), 1.0, 1e-9);
	expect_close("and the performed one starts its own cooldown again beside it",
				 cooldown_of(other, kSlash), 6.0, 1e-9);

	// A player who configured nothing chooses like an NPC: a sequence is not what makes a
	// technique happen, it is only what decides which one.
	open::Character blank = sheet(13);
	train_to_100(blank, open::BasicSkill::OneHandedWeapons);
	wear(blank, kSword, 433);
	blank.set_attack_target(kDefenderId);
	expect_true("an empty configuration is accepted",
				blank.configure_techniques(true, {}) && blank.set_player_controlled(true));
	bool any_technique = false;
	for (int attempt = 0; attempt < 4 && !any_technique; ++attempt) {
		blank.advance_combat_clocks(600.0f);
		const std::optional<open::SwingResult> one = swing(blank, defender, rng);
		any_technique = one.has_value() && one->technique.has_value();
	}
	expect_true("with no sequence, the player picks at random like anyone else", any_technique);
	expect_true("and configuring nothing leaves no position to advance",
				blank.technique_state().index == 0u);
}

// Entries that are not usable are walked past and kept, and the equipment or the rank that
// made them unusable is allowed to change back.
void an_unusable_entry_is_skipped_and_kept() {
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	// The hammer supports Heavy Strike alone, so a sequence naming Slash is configured
	// correctly and simply not the hammer's to perform.
	wear(attacker, kHammer, 441);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(4);
	std::vector<open::TechniqueStep> steps =
		sequence_of({step_of(kSlash, false), step_of(kHeavyStrike, false)});
	expect_true("the sequence is configured", attacker.configure_techniques(true, steps));
	expect_true("and the player drives", attacker.set_player_controlled(true));

	const std::optional<open::SwingResult> hit = swing(attacker, defender, rng);
	expect_true("the unsupported entry is passed over",
				hit.has_value() && hit->technique == std::optional<open::TechniqueId>(kHeavyStrike));
	expect_true("the entry is still configured", attacker.technique_state().sequence == steps);
	attacker.set_attack_target(0);
	expect_true("and still configured after the fight", attacker.technique_state().sequence == steps);

	// A rank gate is the same shape: the entry is skipped until the rank is there.
	open::Character learner = sheet(13);
	wear(learner, kRuiner, 442);
	learner.set_attack_target(kDefenderId);
	std::vector<open::TechniqueStep> gated = sequence_of({step_of(kGate, true)});
	expect_true("the gated entry is configured", learner.configure_techniques(true, gated));
	expect_true("and the player drives", learner.set_player_controlled(true));
	const std::optional<open::SwingResult> basic = swing(learner, defender, rng);
	expect_true("at rank 1 the gated technique is not performed",
				basic.has_value() && !basic->technique.has_value());
	expect_true("and no clock for it was started", !knows_clock(learner, kGate));
	train_to_100(learner, open::BasicSkill::OneHandedWeapons);
	learner.advance_combat_clocks(600.0f);
	const std::optional<open::SwingResult> unlocked = swing(learner, defender, rng);
	expect_true("the same entry is performed once the rank the swing resolves to reaches 50",
				unlocked.has_value() && unlocked->technique == std::optional<open::TechniqueId>(kGate));
}

// The part a swing takes is the engine's own draw. With a player sequence nothing about the
// technique selection consumes luck, so the body-part draw can be predicted from the same
// engine - and if the selection were asked twice, or asked at all on this path, the
// prediction would move.
void the_body_part_draw_is_the_only_luck_a_sequenced_swing_spends() {
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kSword, 451);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(5);
	const std::vector<open::TechniqueStep> steps = sequence_of({step_of(kSlash, false)});
	expect_true("the sequence is configured", attacker.configure_techniques(true, steps));
	expect_true("and the player drives", attacker.set_player_controlled(true));

	// Spend the defender's ready answer first, so the blow under test arrives and draws a
	// part; that first blow is a sequence entry too, so advance one full turn of the clock.
	const std::optional<open::SwingResult> opening = swing(attacker, defender, rng);
	expect_true("the opening swing happened",
				opening.has_value() && opening->technique.has_value());
	// Only the attacker feels time. The defender's answer is still cooling, so the blow
	// under test arrives, and an arriving blow is what spends a part draw.
	attacker.advance_combat_clocks(600.0f);

	std::mt19937_64 oracle(rng);
	const open::BodyPart expected = open::random_melee_part(oracle);
	const std::optional<open::SwingResult> hit = swing(attacker, defender, rng, 1.0f);
	expect_true("the measured swing happened", hit.has_value());
	if (!hit) {
		return;
	}
	expect_true("and it named exactly the part the engine was at, with no draw spent first",
				hit->part.has_value() && *hit->part == expected);
}

// ------------------------------------------------------------------- arithmetic

// A technique's factors are one more independent multiplier on the weapon's own components:
// they ride with the trained fraction, effectiveness, exhaustion, the minimum-range factor,
// the zero-durability penalty and the enchantment's fixed blunt - they replace nothing and
// are not replaced by anything.
void technique_factors_are_one_more_independent_multiplier() {
	// Same weapon, same sheet, same unarmoured defender with its answer spent: one blow is
	// the sword's own and one is the sword's with Slash on it, and the only difference
	// between the two numbers is the technique. Rank 100 makes the trained fraction 1/2 and
	// base Toughness 1 is a divide by 1.05, so
	//   basic:  (6, 2, 0) x 1/2 = (3, 1, 0)   -> 4.0 / 1.05
	//   Slash:  (6 x 1.25, 2, 0) x 1/2 = (3.75, 1, 0) -> 4.75 / 1.05
	open::Character plain = sheet(21);
	open::Character target = sheet(22);
	train_to_100(plain, open::BasicSkill::OneHandedWeapons);
	wear(plain, kSword, 461);
	plain.set_attack_target(target.id());
	plain.set_player_controlled(true);
	std::mt19937_64 rng(6);
	expect_true("the switch is off, so this one only ever attacks basically",
				plain.configure_techniques(false, {}));
	const std::optional<open::SwingResult> basic = unanswered_blow(plain, target, rng);
	if (!basic.has_value()) {
		return;
	}
	expect_true("and the blow under test performed no technique", !basic->technique.has_value());
	expect_close("the basic sword total: (6 + 2) halved by half-training, over Toughness",
				 basic->damage, 4.0 / kToughnessDivisor, 1e-3);

	open::Character slasher = sheet(23);
	open::Character other_target = sheet(24);
	train_to_100(slasher, open::BasicSkill::OneHandedWeapons);
	wear(slasher, kSword, 462);
	slasher.set_attack_target(other_target.id());
	slasher.set_player_controlled(true);
	const std::vector<open::TechniqueStep> only_slash = sequence_of({step_of(kSlash, false)});
	expect_true("Slash is configured", slasher.configure_techniques(true, only_slash));
	const std::optional<open::SwingResult> slashed = unanswered_blow(slasher, other_target, rng);
	if (!slashed.has_value()) {
		return;
	}
	expect_true("the blow under test is the technique",
				slashed->technique == std::optional<open::TechniqueId>(kSlash));
	expect_close("two swings of the sword, at the weapon's own 8 each and no extra for the "
				 "technique",
				 100.0 - stamina(slasher), 16.0, 0.01);

	// Only cut is multiplied, and it is multiplied before armour and before the soak, so the
	// ratio of the two totals is exactly (3.75 + 1) / (3 + 1).
	expect_close("Slash's 1.25 on cut alone, over the same basic blow",
				 slashed->damage / basic->damage, 4.75 / 4.0, 1e-3);
	expect_close("its own cut is the weapon's times the factor",
				 slashed->final.cut / basic->final.cut, 1.25, 1e-3);
	expect_close("while pierce is untouched by a cut factor", slashed->final.pierce,
				 basic->final.pierce, 1e-4);
	expect_close("and blunt, which this weapon does not have, stays nothing",
				 slashed->final.blunt, 0.0, 1e-9);
}

// Minimum range, exhaustion and a ruined blade all halve or discount the technique's own
// result, and the enchantment's fixed blunt is inside the thing they multiply.
void every_other_penalty_still_applies_to_a_technique() {
	// Heavy Strike on a Blunt Edge hammer: the enchantment's fixed blunt is inside the
	// technique's own factor, and exhaustion, a ruined blade and the weapon's minimum range
	// each still take their share of the result.
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	train_to_100(attacker, open::BasicSkill::OneHandedWeapons);
	wear(attacker, kHammer, 471);
	// Applied at rank 20, where the enchantment's requirement is 1 Intelligence: the sheet
	// has that much, so its effectiveness is the clean 1 and the only thing the enchantment
	// is doing here is adding 0.2 blunt. At rank 100 it would ask for 5 and take the swing
	// down to 4% effectiveness, which is a different rule with its own test.
	enchant(attacker, 471, 20u);
	attacker.set_attack_target(defender.id());
	std::mt19937_64 rng(7);
	attacker.set_player_controlled(true);
	const std::vector<open::TechniqueStep> heavy = sequence_of({step_of(kHeavyStrike, false)});
	expect_true("Heavy Strike is configured", attacker.configure_techniques(true, heavy));
	const std::optional<open::WeaponAttackProfile> armed = open::weapon_attack_profile(attacker);
	expect_true("and the hammer is held unimpeded",
				armed.has_value() && armed->effectiveness == 1.0 &&
					armed->enchantments.size() == 1u);

	const std::optional<open::SwingResult> paid = unanswered_blow(attacker, defender, rng);
	if (!paid.has_value()) {
		return;
	}
	// (8 + 0.2) x 1/2 trained x 1.25 Heavy Strike, over 1.05 of Toughness.
	expect_close("the enchantment's blunt is inside the technique's own factor", paid->final.blunt,
				 8.2 * 0.5 * 1.25 / kToughnessDivisor, 1e-3);

	// Exhausted: half the offense, and the technique happens anyway.
	attacker.advance_combat_clocks(600.0f);
	expect_true("the bar is emptied",
				attacker.spend_stamina(attacker.pool(open::PoolKind::Stamina).current));
	const std::optional<open::SwingResult> exhausted_hit = swing(attacker, defender, rng);
	if (!exhausted_hit.has_value()) {
		return;
	}
	expect_true("an exhausted swing still performs its technique",
				exhausted_hit->technique == std::optional<open::TechniqueId>(kHeavyStrike));
	expect_close("at half the offense", exhausted_hit->final.blunt, paid->final.blunt * 0.5, 1e-3);

	// A blade with nothing left: the third of it, technique included.
	open::Character worn = sheet(25);
	open::Character worn_target = sheet(26);
	train_to_100(worn, open::BasicSkill::OneHandedWeapons);
	wear(worn, kHammer, 472);
	worn.set_attack_target(worn_target.id());
	worn.set_player_controlled(true);
	expect_true("Heavy Strike is configured again", worn.configure_techniques(true, heavy));
	std::mt19937_64 worn_rng(17);
	const std::optional<open::SwingResult> whole = unanswered_blow(worn, worn_target, worn_rng);
	if (!whole.has_value()) {
		return;
	}
	expect_close("a whole blade's technique, before any discount", whole->final.blunt,
				 8.0 * 0.5 * 1.25 / kToughnessDivisor, 1e-3);
	const unsigned left = worn.instance(472)->durability;
	expect_true("the blade is worn out", worn.apply_wear(472, left));
	expect_true("with nothing left on it", worn.instance(472)->durability == 0u);
	worn.advance_combat_clocks(600.0f);
	const std::optional<open::SwingResult> ruined = swing(worn, worn_target, worn_rng);
	if (!ruined.has_value()) {
		return;
	}
	expect_true("and the technique still arrives from a ruined blade",
				ruined->technique == std::optional<open::TechniqueId>(kHeavyStrike));
	expect_close("discounted like anything else it multiplies", ruined->final.blunt,
				 8.0 * 0.5 * 1.25 * 0.33 / kToughnessDivisor, 2e-3);

	// Minimum range: the pike's own 0.6 minimum halves inside it, and the technique's
	// factors are multiplied beside that halving, not instead of it.
	open::Character piker = sheet(27);
	open::Character far_target = sheet(28);
	train_to_100(piker, open::BasicSkill::OneHandedWeapons);
	wear(piker, kPike, 473);
	piker.set_attack_target(far_target.id());
	piker.set_player_controlled(true);
	const std::vector<open::TechniqueStep> slash_only = sequence_of({step_of(kSlash, false)});
	expect_true("Slash is configured on the pike", piker.configure_techniques(true, slash_only));
	std::mt19937_64 piker_rng(27);
	const std::optional<open::SwingResult> far = unanswered_blow(piker, far_target, piker_rng, 1.2f);
	// The answer the opening blow spent is still cooling, since only the piker was given the
	// time, so this blow is unanswered too and the two are directly comparable.
	piker.advance_combat_clocks(600.0f);
	const std::optional<open::SwingResult> inside = swing(piker, far_target, piker_rng, 0.3f);
	if (!far.has_value() || !inside.has_value()) {
		return;
	}
	expect_close("this blow is unanswered as well", static_cast<double>(inside->prevented), 0.0, 1e-6);
	expect_true("strictly inside the minimum it is still performed",
				inside->technique == std::optional<open::TechniqueId>(kSlash));
	// (4 x 1.25 + 4) x 1/2 = 4.5 over 1.05 at full reach, and exactly half of that inside.
	expect_close("the full-reach total", far->final.cut + far->final.pierce,
				 4.5 / kToughnessDivisor, 1e-3);
	expect_close("and halved by the minimum, on top of the technique's own factor",
				 (inside->final.cut + inside->final.pierce) / (far->final.cut + far->final.pierce),
				 0.5, 1e-3);
}

// A technique that names a stamina cost replaces the weapon's, and the replacement happens
// before the effectiveness adjustment so a cost override is never a discount.
void a_technique_cost_replaces_the_weapon_cost_before_effectiveness() {
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kRuiner, 481);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(8);
	attacker.set_player_controlled(true);
	const std::vector<open::TechniqueStep> ruin = sequence_of({step_of(kRuinator, false)});
	expect_true("the priced technique is configured", attacker.configure_techniques(true, ruin));

	const std::optional<open::SwingResult> hit = swing(attacker, defender, rng);
	expect_true("the swing happens", hit.has_value());
	if (!hit) {
		return;
	}
	expect_true("and it is the priced technique",
				hit->technique == std::optional<open::TechniqueId>(kRuinator));
	// Full effectiveness: cost is the technique's 20, and the defence the other sheet made
	// cost it its own 8.
	expect_close("the swing paid the technique's 20 and not the weapon's 8",
				 100.0 - stamina(attacker), 20.0, 0.01);
	expect_close("and the defender paid the defence price of its own answer, which is the "
				 "weapon's 8 and not the technique's",
				 100.0 - stamina(defender), kDefenseCost, 0.01);

	const std::optional<open::WeaponAttackProfile> strong = open::weapon_attack_profile(attacker);
	expect_true("that sheet meets everything the blade asks of it",
				strong.has_value() && strong->effectiveness == 1.0);

	// And the replacement happens *before* the effectiveness adjustment, which is the only
	// order in which a cost override is not a discount. Blunt Edge applied at rank 21 asks
	// for 2 Intelligence; a sheet with 1 is at a quarter of it, squared, so effectiveness
	// 0.25 and the technique costs 20 x (2 - 0.25) = 35. The weapon's own 8 would have cost
	// 14 on the same swing, and 35 is what the sheet is left with 65 of.
	open::Character unready = sheet(26);
	open::Character unready_target = sheet(29);
	wear(unready, kRuiner, 483);
	enchant(unready, 483, 21u);
	const std::optional<open::WeaponAttackProfile> straining = open::weapon_attack_profile(unready);
	expect_true("the enchantment asks for more than the sheet has",
				straining.has_value() && straining->effectiveness == 0.25);
	unready.set_attack_target(unready_target.id());
	unready.set_player_controlled(true);
	expect_true("and it is configured with the same priced technique",
				unready.configure_techniques(true, ruin));
	const std::optional<open::SwingResult> discounted = swing(unready, unready_target, rng);
	if (!discounted.has_value()) {
		return;
	}
	expect_true("the technique is performed",
				discounted->technique == std::optional<open::TechniqueId>(kRuinator));
	expect_close("billed as the technique's 20 at the weakened price, not the weapon's",
				 100.0 - stamina(unready), 35.0, 0.01);

	// Same swing, same numbers, one exhausted sheet: an unpaid attempt spends nothing and is
	// performed at half - the price the technique cost rule shares with the weapon's.
	open::Character broke = sheet(27);
	wear(broke, kRuiner, 482);
	broke.set_attack_target(kDefenderId);
	broke.set_player_controlled(true);
	expect_true("and it is configured with the priced technique",
				broke.configure_techniques(true, ruin));
	expect_true("the bar is emptied", broke.spend_stamina(broke.pool(open::PoolKind::Stamina).current));
	const std::optional<open::SwingResult> unpaid = swing(broke, defender, rng);
	expect_true("an unaffordable technique is still performed",
				unpaid.has_value() && unpaid->technique == std::optional<open::TechniqueId>(kRuinator));
	expect_true("and it is charged nothing it does not have",
				stamina(broke) < 1.0);
	expect_true("with the same clock started for it", cooldown_of(broke, kRuinator) == 12.0);
}

// ------------------------------------------------------------------- clocks & stun

// The one clock owner: `advance_combat_clocks` moves technique time as well, exactly once,
// and a stun freezes all three kinds together.
void advance_combat_clocks_owns_technique_time() {
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	wear(attacker, kRuiner, 491);
	attacker.set_attack_target(kDefenderId);
	std::mt19937_64 rng(9);
	attacker.set_player_controlled(true);
	const std::vector<open::TechniqueStep> ruin = sequence_of({step_of(kRuinator, false)});
	expect_true("the sequence is configured", attacker.configure_techniques(true, ruin));

	const std::optional<open::SwingResult> hit = swing(attacker, defender, rng);
	expect_true("the 12-second technique is performed",
				hit.has_value() && hit->technique == std::optional<open::TechniqueId>(kRuinator));
	expect_close("it starts at 12", cooldown_of(attacker, kRuinator), 12.0, 1e-9);
	const double attack_clock = attacker.attack_cooldown_remaining();
	expect_true("and the rank-1 attack clock is running beside it", attack_clock > 0.0f);

	// One clock owner, once: 4.25 s of game time takes 4.25 s off the technique's 12 and the
	// same amount off the attack clock. The attack clock reaching ready inside the very same
	// step is what makes this worth pinning: a coordinator that looped its clocks would have
	// performed the technique again before the step ended.
	expect_true("time passes", attacker.advance_combat_clocks(4.25f));
	expect_close("the technique clock ran for exactly that", cooldown_of(attacker, kRuinator),
				 12.0 - 4.25, 1e-9);
	expect_close("and so did the attack clock, once",
				 static_cast<double>(attacker.attack_cooldown_remaining()),
				 attack_clock - 4.25 > 0.0 ? attack_clock - 4.25 : 0.0, 1e-6);
	expect_true("the technique is not ready yet", attacker.technique_state().cooldowns.count(kRuinator) != 0u);
	expect_true("the rest of it is finished by the same owner",
				attacker.advance_combat_clocks(7.75f));
	expect_close("the technique is ready", cooldown_of(attacker, kRuinator), 0.0, 1e-9);
	expect_true("and a ready technique leaves the state rather than sitting at zero",
				attacker.technique_state().cooldowns.count(kRuinator) == 0u);

	// A refused step owns nothing: no clock of any kind moved.
	const double before_technique = [&] {
		attacker.advance_combat_clocks(0.0f);
		return 0.0;
	}();
	expect_close("a zero step is a no-op", before_technique, 0.0, 1e-9);
	const open::TechniqueState settled = attacker.technique_state();
	expect_true("a bad step is refused and changes nothing",
				!attacker.advance_combat_clocks(-1.0f) && attacker.technique_state().cooldowns ==
															 settled.cooldowns);
}

// A stun holds every clock, including the ones a character owes on moves it did not get to
// finish using, and it is not refreshed by the blows that keep arriving.
void a_stun_freezes_every_clock_it_is_given() {
	open::Character victim = sheet(kAttackerId);
	open::Character hitter = sheet(kDefenderId);
	wear(victim, kRuiner, 501);
	// A fighter who can actually be stunned: Constitution well above Toughness, so the
	// health pool outlasts the stagger meter. See the Task 11 note in the ledger - the meter
	// is a rate, so a trained blunt hitter is what reaches a stun, not a heavy single blow.
	victim.set_base(open::Attribute::Constitution, 20.0f);
	victim.set_base(open::Attribute::Toughness, 1.0f);
	train_to_100(hitter, open::BasicSkill::OneHandedWeapons);
	wear(hitter, kHammer, 502);
	// The hitter needs its own order: a swing is refused without one, and a fight that never
	// throws a blow never fills a meter.
	hitter.set_attack_target(victim.id());
	std::mt19937_64 rng(10);

	// The victim starts a fight holding a technique clock it will not get to use.
	victim.set_attack_target(kDefenderId);
	victim.set_player_controlled(true);
	const std::vector<open::TechniqueStep> ruin = sequence_of({step_of(kRuinator, false)});
	expect_true("the victim configured a technique", victim.configure_techniques(true, ruin));
	const std::optional<open::SwingResult> opening = swing(victim, hitter, rng);
	expect_true("and performed it", opening.has_value() && opening->technique.has_value());

	// Get stun number one, a tenth of a second at a time, with the hitter's own defence
	// spent so its blows keep arriving.
	int ticks = 0;
	// The victim's bar is emptied every tick, so its dodge is performed unpaid (half) and
	// never nullifies. The hitter pays for its swings: a rank-100 hammer that cannot afford
	// its own blow delivers blunt slower than the meter decays, and no stun ever arrives.
	// The meter is a rate - see the Task 11 ledger note.
	while (!victim.is_stunned() && ticks < 4000) {
		++ticks;
		victim.spend_stamina(victim.pool(open::PoolKind::Stamina).current);
		const std::optional<open::SwingResult> one = swing(hitter, victim, rng);
		(void)one;
		hitter.advance_combat_clocks(0.1f);
		victim.advance_combat_clocks(0.1f);
	}
	expect_true("the exchange stunned the victim", victim.is_stunned());
	if (!victim.is_stunned()) {
		return;
	}
	const double held_technique = cooldown_of(victim, kRuinator);
	const double held_stun = victim.stun_remaining();
	expect_true("it still owes its technique clock", held_technique > 0.0);
	expect_true("and the clock it froze with is inside its cooldown", held_technique <= 12.0);

	// Every clock freezes together, and the meter does not top up underneath.
	for (int tick = 0; tick < 5; ++tick) {
		const double meter_before = victim.stagger_buildup();
		expect_true("a tenth of a second passes", victim.advance_combat_clocks(0.1f));
		expect_close("the technique clock is frozen", cooldown_of(victim, kRuinator),
					 held_technique, 1e-9);
		expect_close("the stun counted down by exactly the step", victim.stun_remaining(),
					 held_stun - 0.1 * (tick + 1), 1e-6);
		expect_close("and the meter stayed where the stun left it", victim.stagger_buildup(),
					 meter_before, 1e-9);
	}
	// A blow landing during the stun neither refreshes it nor feeds the meter.
	const double schedule = victim.stun_remaining();
	hitter.advance_combat_clocks(600.0f);
	hitter.spend_stamina(hitter.pool(open::PoolKind::Stamina).current);
	const std::optional<open::SwingResult> during = swing(hitter, victim, rng);
	expect_true("a blow still lands on a stunned sheet", during.has_value() && during->damage > 0.0f);
	expect_close("and the stun ends when it said it would, not later", victim.stun_remaining(),
				 schedule, 1e-9);
	expect_close("no meter was banked under the stun", victim.stagger_buildup(), 0.0, 1e-9);

	// When the stun is over, the clocks the stun held start running again.
	int to_expiry = 0;
	while (victim.is_stunned() && to_expiry < 100) {
		++to_expiry;
		expect_true("time passes", victim.advance_combat_clocks(0.1f));
	}
	expect_true("the stun expired", !victim.is_stunned());
	const double resumed = cooldown_of(victim, kRuinator);
	expect_true("and the owed technique clock resumed from where it stopped",
				resumed > 0.0 && resumed < held_technique);
}

// Swapping what is held, switching who drives, editing the list and toggling the switch are
// four ways to touch a character that must never refund a clock.
void nothing_but_time_refunds_a_technique() {
	open::Character attacker = sheet(kAttackerId);
	open::Character defender = sheet(kDefenderId);
	std::mt19937_64 rng(21);
	wear(attacker, kSword, 511);
	attacker.set_attack_target(kDefenderId);
	attacker.set_player_controlled(true);
	const std::vector<open::TechniqueStep> steps =
		sequence_of({step_of(kSlash, false), step_of(kThrust, false)});
	expect_true("the sequence is configured", attacker.configure_techniques(true, steps));
	expect_true("the first blow is performed", swing(attacker, defender, rng).has_value());
	const open::TechniqueState after = attacker.technique_state();
	expect_true("and one clock is running", after.cooldowns.size() == 1u);
	const open::TechniqueId performed = after.cooldowns.begin()->first;
	const double owed = after.cooldowns.begin()->second;
	expect_true("the sequence moved on", after.index == 1u);

	// 1. The clock is advanced by time only: a step of 1 s takes 1 s off it.
	expect_true("a second passes", attacker.advance_combat_clocks(1.0f));
	expect_close("the clock counted once", cooldown_of(attacker, performed), owed - 1.0, 1e-9);

	// 2. Unequipping and re-equipping the weapon that earned it.
	const std::optional<open::ItemId> held = attacker.equipped(open::Slot::RightHand);
	expect_true("the sword is held", held.has_value());
	expect_true("and it can be removed", attacker.unequip(open::Slot::RightHand));
	const double bare_clock = cooldown_of(attacker, performed);
	expect_close("removing the weapon changed no clock", bare_clock, owed - 1.0, 1e-9);
	expect_true("the sword is held again", attacker.equip(*held, open::Slot::RightHand));
	expect_close("and so did putting it back", cooldown_of(attacker, performed), owed - 1.0, 1e-9);

	// 3. Swapping for a weapon that supports nothing this sequence names.
	open::Character hammer_user = sheet(31);
	wear(hammer_user, kHammer, 512);
	hammer_user.set_attack_target(kDefenderId);
	hammer_user.set_player_controlled(true);
	expect_true("the sequence is configured on the hammer too",
				hammer_user.configure_techniques(true, steps));
	expect_true("the hammer performs neither entry and still swings",
				swing(hammer_user, defender, rng).has_value());
	// (its own clock, if any, is its own business: see the unsupported case above)

	// 4. Switching controllers, editing the sequence, and toggling the switch.
	expect_true("control switches", attacker.set_player_controlled(false));
	expect_true("and the clock survived", cooldown_of(attacker, performed) > 0.0);
	expect_true("with the position it had", attacker.technique_state().index == 1u);
	attacker.advance_combat_clocks(0.0f);
	const double before_edit = cooldown_of(attacker, performed);
	const std::vector<open::TechniqueStep> edited =
		sequence_of({step_of(kThrust, true), step_of(kSlash, true)});
	expect_true("the sequence is edited", attacker.configure_techniques(true, edited));
	expect_true("the edit restarted the index", attacker.technique_state().index == 0u);
	expect_close("and refunded no clock", cooldown_of(attacker, performed), before_edit, 1e-9);
	expect_true("the switch turns off", attacker.configure_techniques(false, edited));
	expect_true("without touching the clock", cooldown_of(attacker, performed) == before_edit);
	expect_true("and the sequence it was configured with is still the same list",
				attacker.technique_state().sequence == edited);
	// 5. And the clock the fight already started is not re-spent by a swing that could not
	// use it: with the switch off, no technique is performed and no new clock appears. The
	// switch is the player's, so the player has to be driving again for it to mean anything
	// - step 4 handed the sheet to the simulation, and an NPC ignores the switch by design.
	expect_true("the player is driving again", attacker.set_player_controlled(true));
	// Exactly the time the next swing is due in, and not a second more. That is also why the
	// technique clock is guaranteed to still be running here: the sword's own cooldown is
	// capped at 5 s and this entry's cooldown is 6 s, and both started on the same swing, so
	// the attack is ready first however the swing was paid for.
	attacker.advance_combat_clocks(attacker.attack_cooldown_remaining());
	const std::size_t clocks_before = attacker.technique_state().cooldowns.size();
	expect_true("the technique it performed is still cooling", clocks_before == 1u);
	const std::optional<open::SwingResult> basic = swing(attacker, defender, rng);
	expect_true("with techniques off the attack is basic",
				basic.has_value() && !basic->technique.has_value());
	expect_true("and no technique clock started",
				attacker.technique_state().cooldowns.size() == clocks_before);
}

void configuring_and_control_refuse_what_cannot_work() {
	open::Character who = sheet(kAttackerId);
	std::vector<open::TechniqueStep> steps = sequence_of({step_of(kSlash, false)});
	expect_true("a real entry is accepted", who.configure_techniques(true, steps));
	expect_true("and read back", who.technique_state().sequence == steps &&
									 who.technique_state().enabled &&
									 !who.technique_state().player_controlled);
	expect_true("an empty sequence is accepted", who.configure_techniques(true, {}));
	const std::vector<open::TechniqueStep> zero_id = sequence_of({step_of(0, false)});
	expect_true("an entry that names no technique is refused",
				!who.configure_techniques(true, zero_id));
	expect_true("and the sequence it was refused into is still the one before",
				who.technique_state().sequence.empty());

	// A dead sheet has no configuration screen and no next attack.
	open::Character corpse = sheet(32);
	expect_true("the sheet dies", corpse.damage(open::BodyPart::Torso, 1.0e30f));
	expect_true("configuring a dead sheet is refused", !corpse.configure_techniques(true, steps));
	expect_true("and so is changing who drives it", !corpse.set_player_controlled(true));
}

}  // namespace

int main() {
	a_performed_technique_starts_its_own_clock_and_the_attack_clock();
	a_technique_is_performed_even_when_it_does_not_hurt();
	a_refused_attempt_commits_nothing();
	skip_waits_loops_and_gives_up();
	an_unusable_entry_is_skipped_and_kept();
	the_body_part_draw_is_the_only_luck_a_sequenced_swing_spends();
	technique_factors_are_one_more_independent_multiplier();
	every_other_penalty_still_applies_to_a_technique();
	a_technique_cost_replaces_the_weapon_cost_before_effectiveness();
	advance_combat_clocks_owns_technique_time();
	a_stun_freezes_every_clock_it_is_given();
	nothing_but_time_refunds_a_technique();
	configuring_and_control_refuse_what_cannot_work();
	return test_summary();
}
