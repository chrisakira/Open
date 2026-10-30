#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"
#include "open/techniques.hpp"
#include "open/weapon_rules.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <vector>

#include "test_support.hpp"

// Task 12: what a technique is, who may use it, and which one a fight would pick next.
//
// Nothing here performs anything. The definitions are locked numbers from
// docs/weapons.md, eligibility is the gate a swing has to clear before it is even
// considered, and the selector is a pure question - "given this sheet, this weapon and
// this configuration, what would the next attack be?" - that answers without spending a
// cooldown, advancing a sequence or drawing a random number it does not need. Performing
// is Task 13, and the boundary is the point of half these cases: a preview that could
// start a clock, or a selector that could eat a sequence entry, would make the popup and
// the fight disagree about what the character owes.
//
// Break these catches: a technique performed from a weapon that does not support it, an
// unlock read off effective skill instead of the resolved job rank, a title grant that
// works or does not work, a starter row whose multiplier or cooldown drifted, a disabled
// player performing one anyway, a sequence that advances on a technique it never got, a
// Wait entry that is skipped like a Skip, a Skip entry that blocks the fight like a Wait,
// an entry deleted because it is unusable today, a loop that walks the sequence twice in
// one attack, an edit or a change of controller that resets a clock, a selection that
// draws the rng when nothing was ready, and a malformed definition that reaches a catalog.

namespace {

constexpr open::DefinitionId kSword = 201;  // 6 cut, 2 pierce: supports Slash and Thrust
constexpr open::DefinitionId kSpear = 202;  // 8 pierce: Thrust only
constexpr open::DefinitionId kHammer = 203;  // 8 blunt: Heavy Strike only
constexpr open::DefinitionId kClub = 204;    // positive damage, no techniques at all
constexpr open::MaterialId kMaterial = 31;

// 1, 2 and 3 are the locked starter rows, so they are the production ids and a fixture
// must not reuse them. The gates below are test-only techniques, which is how the spec
// wants title grants covered while the starter slice assigns none.
constexpr open::TechniqueId kSlash = 1;
constexpr open::TechniqueId kThrust = 2;
constexpr open::TechniqueId kHeavyStrike = 3;
constexpr open::TechniqueId kRankGate = 900;       // needs trained 50
constexpr open::TechniqueId kTitleGate = 901;      // needs trained 200, granted by a title
constexpr open::TechniqueId kEverything = 902;     // its own cost, its own cooldown

// A catalog of the four fixture weapons, the one material, and the technique table the
// case is about. Built fresh every call: several cases below assert that a bad technique
// table or a broken support link rejects the whole catalog, and a memoised catalog would
// turn that into a shared "already rejected" surprise. `sword_supports` is the link under
// test, so the sword's support list is the one thing a case may change.
std::shared_ptr<const open::ItemCatalog> catalog_with(
	const std::vector<open::TechniqueDefinition> &techniques,
	const std::vector<open::DefinitionId> &sword_supports = {}) {
	// An empty request means "whatever this table defines": the sword supports the two
	// starter techniques plus the two unlock gates, so a case about ranks or titles is not
	// accidentally a case about support. A case that wants a narrower or a broken list says
	// so, and gets exactly what it asked for.
	std::vector<open::DefinitionId> supports = sword_supports;
	if (supports.empty()) {
		supports = {kSlash, kThrust};
		for (const open::TechniqueDefinition &technique : techniques) {
			if (technique.id == kRankGate || technique.id == kTitleGate) {
				supports.push_back(technique.id);
			}
		}
	}
	auto weapon = [](open::DefinitionId id, open::DamageComponents damage, open::SkillJob job,
					 std::vector<open::TechniqueId> supports) {
		open::ItemDefinition item;
		item.id = id;
		item.model_key = "Technique_Fixture";
		item.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
		item.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
		item.hand_use = open::HandUse::OneHanded;
		open::WeaponDefinition record;
		record.job = job;
		record.stats.damage = damage;
		record.stats.min_reach = 0.0;
		record.stats.max_reach = 1.5;
		record.stats.speed = 1.0;
		record.stats.stamina_cost = 8.0;
		record.stats.block_percent = 0.0;
		record.stats.weight = 2.0;
		record.stats.weight_class = open::WeightClass::Light;
		record.stats.requirements = {};
		record.stats.required_trained_rank = 1;
		record.supported_techniques = supports;
		item.weapon = record;
		return item;
	};

	// The jobs are the starter table's: a sword belongs to One-Handed Weapons and names
	// Sword as its matching specialisation, which is what makes a chosen track change the
	// rank a technique is judged on. The club names none, so its wielder is always read on
	// the parent.
	std::vector<open::ItemDefinition> items;
	items.push_back(weapon(kSword, open::DamageComponents{6.0, 2.0, 0.0},
						   open::SkillJob{open::BasicSkill::OneHandedWeapons,
										  open::AdvancedSkill::Sword},
						   supports));
	items.push_back(weapon(kSpear, open::DamageComponents{0.0, 8.0, 0.0},
						   open::SkillJob{open::BasicSkill::Polearms, open::AdvancedSkill::Spear},
						   std::vector<open::TechniqueId>{kThrust}));
	items.push_back(weapon(kHammer, open::DamageComponents{0.0, 0.0, 8.0},
						   open::SkillJob{open::BasicSkill::OneHandedWeapons,
										  open::AdvancedSkill::Mace},
						   std::vector<open::TechniqueId>{kHeavyStrike}));
	items.push_back(weapon(kClub, open::DamageComponents{0.0, 0.0, 4.0},
						   open::SkillJob{open::BasicSkill::OneHandedWeapons, std::nullopt}, {}));

	open::MaterialDefinition material;
	material.id = kMaterial;
	material.max_durability_by_quality = {100, 100, 100, 100, 100, 100, 100};

	const std::optional<open::ItemCatalog> made =
		open::ItemCatalog::create(items, {material}, {}, techniques);
	if (!made.has_value()) {
		return nullptr;
	}
	return std::make_shared<open::ItemCatalog>(*made);
}

// The three locked starter rows, exactly as docs/weapons.md states them.
std::vector<open::TechniqueDefinition> starters() {
	std::vector<open::TechniqueDefinition> made;
	{
		open::TechniqueDefinition slash;
		slash.id = kSlash;
		slash.required_rank = 1;
		slash.factors = open::DamageComponents{1.25, 1.0, 1.0};
		slash.cooldown = 6.0;
		made.push_back(slash);
	}
	{
		open::TechniqueDefinition thrust;
		thrust.id = kThrust;
		thrust.required_rank = 1;
		thrust.factors = open::DamageComponents{1.0, 1.25, 1.0};
		thrust.cooldown = 8.0;
		made.push_back(thrust);
	}
	{
		open::TechniqueDefinition heavy;
		heavy.id = kHeavyStrike;
		heavy.required_rank = 1;
		heavy.factors = open::DamageComponents{1.0, 1.0, 1.25};
		heavy.cooldown = 10.0;
		made.push_back(heavy);
	}
	return made;
}

// The unlock gates the starter slice deliberately does not use: one technique behind a
// trained rank, one behind a rank only a title can pay.
std::vector<open::TechniqueDefinition> starters_plus_gates() {
	std::vector<open::TechniqueDefinition> made = starters();
	{
		open::TechniqueDefinition rank_gate;
		rank_gate.id = kRankGate;
		rank_gate.required_rank = 50;
		rank_gate.factors = open::DamageComponents{2.0, 1.0, 1.0};
		rank_gate.cooldown = 4.0;
		made.push_back(rank_gate);
	}
	{
		open::TechniqueDefinition title_gate;
		title_gate.id = kTitleGate;
		title_gate.required_rank = 200;
		title_gate.granting_titles = {open::Title::Duelist};
		title_gate.factors = open::DamageComponents{1.0, 2.0, 1.0};
		title_gate.cooldown = 5.0;
		made.push_back(title_gate);
	}
	{
		open::TechniqueDefinition every;
		every.id = kEverything;
		every.required_rank = 1;
		every.factors = open::DamageComponents{2.0, 3.0, 4.0};
		every.stamina_cost = 5.0;
		every.cooldown = 7.0;
		made.push_back(every);
	}
	return made;
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

open::Character sheet(std::uint64_t id, const std::shared_ptr<const open::ItemCatalog> &made) {
	if (made == nullptr) {
		g_failed++;
		std::cerr << "FAIL the technique fixture catalog was rejected\n";
	}
	const std::optional<open::Character> fresh = open::Character::create(config(id), made);
	expect_true("the technique fixture's sheet is created", fresh.has_value());
	if (!fresh.has_value()) {
		g_failed++;
	}
	return *fresh;
}

void wield(open::Character &who, open::DefinitionId what, open::ItemId id) {
	const std::optional<open::ItemInstance> instance =
		open::make_item_instance(*who.catalog(), what, kMaterial, id, 1);
	expect_true("the fixture can describe the weapon", instance.has_value());
	if (!instance.has_value()) {
		return;
	}
	expect_true("the weapon is carried", who.add_instance(*instance));
	expect_true("the weapon is held", who.equip(id, open::Slot::RightHand));
}

open::WeaponAttackProfile profile_of(const open::Character &who) {
	const std::optional<open::WeaponAttackProfile> made = open::weapon_attack_profile(who);
	expect_true("the fixture resolves an attack profile", made.has_value());
	return *made;
}

// Training in place, by the sheet's own progression, for the job the profile names.
void train(open::Character &who, const open::SkillJob &job, unsigned target) {
	while (who.resolve_skill(job.basic, job.option).rank < target) {
		expect_true("the fixture can train the job", who.record_use(job.basic, job.option, true));
	}
}

// The whole technique list of a resolved profile, as a set, for "the weapon supports
// exactly this" claims.
std::set<open::TechniqueId> supported(const open::WeaponAttackProfile &attack) {
	return std::set<open::TechniqueId>(attack.techniques.begin(), attack.techniques.end());
}

open::TechniqueState player_sequence(std::vector<open::TechniqueStep> steps) {
	open::TechniqueState state;
	state.player_controlled = true;
	state.enabled = true;
	state.sequence = std::move(steps);
	return state;
}

open::TechniqueStep step_of(open::TechniqueId id, bool skip_on_cooldown) {
	open::TechniqueStep made;
	made.id = id;
	made.skip_on_cooldown = skip_on_cooldown;
	return made;
}

// Is this selection the basic attack, with the sequence left exactly where it was?
void expect_basic(const char *name, const open::TechniqueSelection &pick,
				  const open::TechniqueState &before) {
	expect_true(name, !pick.technique.has_value());
	expect_true("a basic attack because of a Wait keeps this entry next in line",
				pick.proposed_index == std::optional<std::size_t>(before.index));
}

// ------------------------------------------------------------------ definitions

// The starter techniques are production data with the locked numbers on them, and the
// slice assigns no title grants and no own stamina costs.
void the_starter_rows_are_the_locked_numbers() {
	const std::vector<open::TechniqueDefinition> made = open::starter_techniques();
	expect_true("three starter techniques exist", made.size() == 3u);
	if (made.size() != 3u) {
		return;
	}
	const std::vector<open::TechniqueId> ids = {open::starter_techniques()[0].id,
											   open::starter_techniques()[1].id,
											   open::starter_techniques()[2].id};
	expect_true("with the locked ids in order",
				ids[0] == kSlash && ids[1] == kThrust && ids[2] == kHeavyStrike);

	struct want {
		open::TechniqueId id;
		double cut;
		double pierce;
		double blunt;
		double cooldown;
	};
	const std::vector<want> rows = {{kSlash, 1.25, 1.0, 1.0, 6.0},
									{kThrust, 1.0, 1.25, 1.0, 8.0},
									{kHeavyStrike, 1.0, 1.0, 1.25, 10.0}};
	for (const want &row : rows) {
		const open::TechniqueDefinition *found = nullptr;
		for (const open::TechniqueDefinition &technique : made) {
			if (technique.id == row.id) {
				found = &technique;
			}
		}
		expect_true("every starter id is present", found != nullptr);
		if (found == nullptr) {
			continue;
		}
		expect_close("its cut multiplier", found->factors.cut, row.cut, 1e-9);
		expect_close("its pierce multiplier", found->factors.pierce, row.pierce, 1e-9);
		expect_close("its blunt multiplier", found->factors.blunt, row.blunt, 1e-9);
		expect_close("its cooldown", found->cooldown, row.cooldown, 1e-9);
		expect_true("every starter asks only for trained rank 1", found->required_rank == 1u);
		expect_true("and none of them costs more than the weapon does",
					!found->stamina_cost.has_value());
		expect_true("and the starter slice grants none from a title",
					found->granting_titles.empty());
	}

	// And they are real: a catalog accepts them and hands them back unchanged.
	const std::shared_ptr<const open::ItemCatalog> made_catalog = catalog_with(starters());
	const open::TechniqueDefinition *slash = made_catalog->technique(kSlash);
	expect_true("the catalog resolves a starter technique", slash != nullptr);
	if (slash != nullptr) {
		expect_close("with the locked cooldown still on it", slash->cooldown, 6.0, 1e-9);
	}
}

// ------------------------------------------------------------------- eligibility

// A technique is performed only by a weapon that says so, whoever the wielder is.
void a_weapon_must_support_the_technique() {
	// A sword that supports Slash and Thrust and nothing else, which is the only way to
	// watch the support list do its job rather than a rank or a title doing it.
	const std::shared_ptr<const open::ItemCatalog> made =
		catalog_with(starters_plus_gates(), {kSlash, kThrust});
	open::Character swordsman = sheet(1, made);
	wield(swordsman, kSword, 501);
	const open::WeaponAttackProfile sword = profile_of(swordsman);
	expect_true("a resolved profile carries the techniques its weapon supports",
				supported(sword) == std::set<open::TechniqueId>({kSlash, kThrust}));

	const open::TechniqueDefinition *slash = made->technique(kSlash);
	const open::TechniqueDefinition *thrust = made->technique(kThrust);
	const open::TechniqueDefinition *heavy = made->technique(kHeavyStrike);
	expect_true("the definitions resolve", slash != nullptr && thrust != nullptr && heavy != nullptr);
	if (slash == nullptr || thrust == nullptr || heavy == nullptr) {
		return;
	}
	expect_true("so Slash is eligible for the sword at trained rank 1",
				open::technique_eligible(swordsman, sword, *slash));
	expect_true("and so is Thrust", open::technique_eligible(swordsman, sword, *thrust));
	expect_true("but Heavy Strike is not a sword technique however much the sword wants it",
				!open::technique_eligible(swordsman, sword, *heavy));

	open::Character spearman = sheet(2, made);
	wield(spearman, kSpear, 502);
	const open::WeaponAttackProfile spear = profile_of(spearman);
	expect_true("the spear supports Thrust alone",
				supported(spear) == std::set<open::TechniqueId>({kThrust}));
	expect_true("which is why a thrust is eligible",
				open::technique_eligible(spearman, spear, *thrust));
	expect_true("and a slash is not, whatever the rank",
				!open::technique_eligible(spearman, spear, *slash));

	// A weapon that declares nothing supports nothing, even though it hits fine.
	open::Character clubsman = sheet(3, made);
	wield(clubsman, kClub, 503);
	const open::WeaponAttackProfile club = profile_of(clubsman);
	expect_true("an ordinary weapon with no declared techniques supports none",
				club.techniques.empty() && club.stats.damage.blunt > 0.0);
	expect_true("so even the technique it would most suit is refused",
				!open::technique_eligible(clubsman, club, *slash));
}

// Bare hands are not a weapon with an empty list; they are no weapon at all, and the spec
// says an unarmed attack has no techniques.
void bare_hands_support_nothing() {
	const std::shared_ptr<const open::ItemCatalog> made = catalog_with(starters_plus_gates());
	open::Character bare = sheet(4, made);
	const open::WeaponAttackProfile fists = profile_of(bare);
	expect_true("the unarmed fallback names no item", !fists.item.has_value());
	expect_true("and supports no technique", fists.techniques.empty());
	for (const open::TechniqueDefinition &technique : open::starter_techniques()) {
		expect_true("no starter technique is eligible bare-handed",
					!open::technique_eligible(bare, fists, technique));
	}

	// Training and titles change the answer for a weapon and never change it here.
	train(bare, fists.job, 100u);
	for (unsigned i = 0; i < 200; ++i) {
		bare.record_title_progress(open::Title::Duelist, true);
	}
	expect_true("the sheet really is a trained, titled bare-handed fighter",
		bare.has_title(open::Title::Duelist) && profile_of(bare).trained == 100u);
	for (const open::TechniqueDefinition &technique : open::starter_techniques()) {
		expect_true("and still no technique with no weapon",
					!open::technique_eligible(bare, fists, technique));
	}
	const open::TechniqueDefinition *title_gate = made->technique(kTitleGate);
	expect_true("not even a title-granted one",
				title_gate != nullptr && !open::technique_eligible(bare, fists, *title_gate));
}

// Unlock is the resolved job rank - matching specialisation if one is chosen, the frozen
// parent otherwise - or a title that grants the technique. Never effective skill, and
// never a rank earned with a different weapon.
void unlock_is_the_resolved_job_rank_or_a_title() {
	const std::shared_ptr<const open::ItemCatalog> made = catalog_with(starters_plus_gates());
	const open::TechniqueDefinition *rank_gate = made->technique(kRankGate);
	const open::TechniqueDefinition *title_gate = made->technique(kTitleGate);
	expect_true("both gates resolve", rank_gate != nullptr && title_gate != nullptr);
	if (rank_gate == nullptr || title_gate == nullptr) {
		return;
	}

	open::Character novice = sheet(5, made);
	wield(novice, kSword, 504);
	const open::WeaponAttackProfile sword = profile_of(novice);
	expect_true("a rank-1 wielder is below both gates",
				sword.trained == 1u && !open::technique_eligible(novice, sword, *rank_gate) &&
					!open::technique_eligible(novice, sword, *title_gate));

	// A specialisation in another skill family does not open a sword technique.
	while (novice.resolve_skill(open::BasicSkill::Polearms, std::nullopt).rank < 100u) {
		novice.record_use(open::BasicSkill::Polearms, std::nullopt, true);
	}
	expect_true("a rank-100 polearm rank leaves the sword technique gated",
				!open::technique_eligible(novice, sword, *rank_gate));

	// The sword's own job, which with nothing chosen is its parent family.
	train(novice, sword.job, 50u);
	expect_true("and the sword's own job rank at 50 opens the 50 gate",
				profile_of(novice).trained == 50u &&
					open::technique_eligible(novice, profile_of(novice), *rank_gate));

	// Choosing the matching specialisation is what a capped parent buys, and it restarts the
	// count at 1: the frozen-parent rule the plan insists techniques inherit instead of
	// redeciding, so a technique gated at 50 is gated again by the very choice that means the
	// sheet is now better with the weapon.
	train(novice, sword.job, 100u);
	expect_true("the Sword specialisation is chosen at a capped parent",
				novice.advance(open::AdvancedSkill::Sword));
	const open::WeaponAttackProfile specialised = profile_of(novice);
	expect_true("the profile now reads the chosen track from rank 1",
				specialised.trained == 1u && specialised.job.option ==
												 std::optional<open::AdvancedSkill>(open::AdvancedSkill::Sword));
	expect_true("so the technique the parent rank had opened is gated again",
				!open::technique_eligible(novice, specialised, *rank_gate));
	train(novice, specialised.job, 50u);
	expect_true("and opens once the track the swing actually uses reaches 50",
				profile_of(novice).trained == 50u &&
					open::technique_eligible(novice, profile_of(novice), *rank_gate));

	// A title grants a technique outright, at a rank it could not otherwise reach.
	open::Character titled = sheet(6, made);
	wield(titled, kSword, 505);
	const open::WeaponAttackProfile titled_sword = profile_of(titled);
	for (unsigned i = 0; i < 200; ++i) {
		titled.record_title_progress(open::Title::Duelist, true);
	}
	expect_true("the Duelist title is held", titled.has_title(open::Title::Duelist));
	expect_true("which opens the 200-rank technique at rank 1",
				titled_sword.trained == 1u &&
					open::technique_eligible(titled, titled_sword, *title_gate));
	expect_true("but it does not buy support the weapon never declared",
				!open::technique_eligible(titled, titled_sword, *rank_gate));

	// And a title that is not on the technique's list grants nothing.
	open::Character wall = sheet(7, made);
	wield(wall, kSword, 506);
	for (unsigned i = 0; i < 200; ++i) {
		wall.record_title_progress(open::Title::Wall, true);
	}
	expect_true("Wall is held but is not on the grant list",
				wall.has_title(open::Title::Wall) &&
					!open::technique_eligible(wall, profile_of(wall), *title_gate));
}

// ------------------------------------------------------------------------- catalog

// A technique table is validated as a whole, like every other part of the catalog, and
// the link from a weapon to what it supports is checked at the same moment.
void malformed_definitions_and_links_are_rejected() {
	const std::vector<open::TechniqueDefinition> good = starters();
	const std::shared_ptr<const open::ItemCatalog> made = catalog_with(good);
	expect_true("the good table is accepted", made != nullptr);

	// The weapon-to-technique link, in both directions of mistake.
	const std::shared_ptr<const open::ItemCatalog> unknown =
		catalog_with(good, {kSlash, 777});
	expect_true("a weapon supporting an undefined technique id rejects the catalog",
				unknown == nullptr);
	const std::shared_ptr<const open::ItemCatalog> zero = catalog_with(good, {0});
	expect_true("and so does an id of zero", zero == nullptr);
	const std::shared_ptr<const open::ItemCatalog> twice = catalog_with(good, {kSlash, kSlash});
	expect_true("and a support entry listed twice", twice == nullptr);

	auto rejected = [&](const char *name, const open::TechniqueDefinition &bad) {
		std::vector<open::TechniqueDefinition> techniques = good;
		techniques.push_back(bad);
		const std::shared_ptr<const open::ItemCatalog> built = catalog_with(techniques);
		expect_true(name, built == nullptr);
	};
	auto invalid = [&](const char *name, const std::function< void(open::TechniqueDefinition &) > &bend) {
		open::TechniqueDefinition bad;
		bad.id = 950;
		bad.required_rank = 1;
		bad.factors = open::DamageComponents{1.0, 1.0, 1.0};
		bad.cooldown = 6.0;
		bend(bad);
		rejected(name, bad);
	};

	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();
	invalid("a zero technique id is rejected", [&](open::TechniqueDefinition &bad) { bad.id = 0; });
	invalid("a duplicate of a starter id is rejected",
			[&](open::TechniqueDefinition &bad) { bad.id = kSlash; });
	invalid("a zero required rank is rejected",
			[&](open::TechniqueDefinition &bad) { bad.required_rank = 0; });
	invalid("a zero cooldown is rejected", [&](open::TechniqueDefinition &bad) { bad.cooldown = 0.0; });
	invalid("a negative cooldown is rejected",
			[&](open::TechniqueDefinition &bad) { bad.cooldown = -1.0; });
	invalid("a nan cooldown is rejected", [&](open::TechniqueDefinition &bad) { bad.cooldown = nan; });
	invalid("an infinite cooldown is rejected", [&](open::TechniqueDefinition &bad) { bad.cooldown = inf; });
	invalid("a zero multiplier is rejected",
			[&](open::TechniqueDefinition &bad) { bad.factors.cut = 0.0; });
	invalid("a negative multiplier is rejected",
			[&](open::TechniqueDefinition &bad) { bad.factors.pierce = -1.0; });
	invalid("a nan multiplier is rejected",
			[&](open::TechniqueDefinition &bad) { bad.factors.blunt = nan; });
	invalid("a zero stamina cost is rejected",
			[&](open::TechniqueDefinition &bad) { bad.stamina_cost = 0.0; });
	invalid("a negative stamina cost is rejected",
			[&](open::TechniqueDefinition &bad) { bad.stamina_cost = -2.0; });
	invalid("a nan stamina cost is rejected",
			[&](open::TechniqueDefinition &bad) { bad.stamina_cost = nan; });
	invalid("an unknown title is rejected", [&](open::TechniqueDefinition &bad) {
		bad.granting_titles = {static_cast<open::Title>(999)};
	});
	invalid("a title listed twice is rejected", [&](open::TechniqueDefinition &bad) {
		bad.granting_titles = {open::Title::Duelist, open::Title::Duelist};
	});

	// A cost that is present is honoured as a number, and an absent one is a real state.
	std::vector<open::TechniqueDefinition> with_cost = good;
	with_cost.push_back(starters_plus_gates().back());  // kEverything, cost 5
	const std::shared_ptr<const open::ItemCatalog> priced = catalog_with(with_cost);
	expect_true("a technique may declare its own stamina cost", priced != nullptr);
	if (priced != nullptr) {
		const open::TechniqueDefinition *every = priced->technique(kEverything);
		expect_true("and it is read back", every != nullptr && every->stamina_cost.has_value());
		if (every != nullptr && every->stamina_cost.has_value()) {
			expect_close("at its own value", *every->stamina_cost, 5.0, 1e-9);
		}
		const open::TechniqueDefinition *slash = priced->technique(kSlash);
		expect_true("while an ordinary technique declares none",
					slash != nullptr && !slash->stamina_cost.has_value());
		expect_true("and an unknown technique id answers nothing",
					priced->technique(9999) == nullptr);
		expect_true("and a technique id never resolves as an item id",
					priced->technique(kSword) == nullptr && priced->item(kSlash) == nullptr);
	}
}

// ------------------------------------------------------------------- the selector

// The state's own defaults are a rule: a sheet that has configured nothing is an NPC-style
// random chooser with every clock empty, not a disabled one.
void the_default_state_is_a_random_chooser() {
	const open::TechniqueState fresh;
	expect_true("techniques are enabled", fresh.enabled);
	expect_true("and the default controller is the simulation's own, not the player's",
				!fresh.player_controlled);
	expect_true("with no sequence", fresh.sequence.empty());
	expect_true("starting at the first entry", fresh.index == 0u);
	expect_true("and no clock running", fresh.cooldowns.empty());
}

// An NPC, and a player who configured nothing, choose among the ready applicable unlocked
// techniques - in an order that does not depend on how the catalog happened to store them
// - and fall back to a basic attack with nothing to choose from, without spending luck.
void random_selection_uses_the_ready_set() {
	const std::shared_ptr<const open::ItemCatalog> made = catalog_with(starters_plus_gates());
	open::Character swordsman = sheet(8, made);
	wield(swordsman, kSword, 507);
	const open::WeaponAttackProfile sword = profile_of(swordsman);
	open::TechniqueState state;  // default: NPC, enabled, no sequence

	std::mt19937_64 rng(5);
	std::set<open::TechniqueId> picked;
	for (int attempt = 0; attempt < 200; ++attempt) {
		const open::TechniqueSelection pick = open::select_technique(swordsman, sword, state, rng);
		if (!pick.technique.has_value()) {
			g_failed++;
			continue;
		}
		picked.insert(*pick.technique);
	}
	expect_true("the two sword techniques are the whole pool",
				picked == std::set<open::TechniqueId>({kSlash, kThrust}));

	// The same seed gives the same fight, and a different seed gives another one: the
	// candidate order the draw indexes into is sorted, not the catalog's hash order.
	std::mt19937_64 first(11);
	std::mt19937_64 second(11);
	bool replayed = true;
	for (int attempt = 0; attempt < 30; ++attempt) {
		if (open::select_technique(swordsman, sword, state, first).technique !=
			open::select_technique(swordsman, sword, state, second).technique) {
			replayed = false;
		}
	}
	expect_true("a seeded NPC fight replays its technique choices", replayed);
	std::mt19937_64 other(12);
	bool differs = false;
	for (int attempt = 0; attempt < 40 && !differs; ++attempt) {
		differs = open::select_technique(swordsman, sword, state, first).technique !=
				  open::select_technique(swordsman, sword, state, other).technique;
	}
	expect_true("and two seeds do not choose the same thing thirty times running", differs);

	// One clock running removes exactly that candidate.
	state.cooldowns[kSlash] = 2.0;
	bool saw_slash = false;
	bool saw_thrust = false;
	for (int attempt = 0; attempt < 60; ++attempt) {
		const open::TechniqueSelection pick = open::select_technique(swordsman, sword, state, rng);
		if (pick.technique == std::optional<open::TechniqueId>(kSlash)) {
			saw_slash = true;
		}
		if (pick.technique == std::optional<open::TechniqueId>(kThrust)) {
			saw_thrust = true;
		}
		expect_true("and a basic attack is never what an NPC falls back to while one is ready",
					pick.technique.has_value());
	}
	expect_true("the cooling technique is out of the pool", !saw_slash);
	expect_true("and the ready one stays in it", saw_thrust);

	// Everything cooling is a basic attack, and it costs no luck: the rng is untouched, so
	// a preview or a refused window cannot reorder the fight that follows it.
	state.cooldowns[kThrust] = 1.0;
	std::mt19937_64 before(rng);
	const open::TechniqueSelection none = open::select_technique(swordsman, sword, state, rng);
	expect_true("with every technique cooling, the answer is a basic attack",
				!none.technique.has_value());
	expect_true("and no sequence exists to move", !none.proposed_index.has_value());
	expect_true("and the random engine was not advanced by a single draw",
				rng() == before());

	// A weapon with no techniques at all answers the same way, however the state is set.
	open::Character clubsman = sheet(9, made);
	wield(clubsman, kClub, 508);
	const open::WeaponAttackProfile club = profile_of(clubsman);
	std::mt19937_64 quiet(3);
	std::mt19937_64 quiet_copy(quiet);
	const open::TechniqueSelection basic = open::select_technique(clubsman, club, state, quiet);
	expect_true("an unsupported weapon performs a basic attack", !basic.technique.has_value());
	expect_true("without drawing", quiet() == quiet_copy());

	// And selection is a question, not an action: nothing about the state moved.
	const open::TechniqueState unchanged = state;
	open::TechniqueState asked = state;
	for (int attempt = 0; attempt < 10; ++attempt) {
		open::select_technique(swordsman, sword, asked, rng);
	}
	expect_true("a preview does not spend a cooldown", asked.cooldowns == unchanged.cooldowns);
	expect_true("nor a sequence position", asked.index == unchanged.index);
	expect_true("nor the configuration itself", asked.sequence == unchanged.sequence &&
													 asked.enabled == unchanged.enabled &&
													 asked.player_controlled ==
														 unchanged.player_controlled);
}

// A player who switched techniques off performs a basic attack. This is the one case where
// `enabled` means anything, and it must not be paid for by breaking the NPC path.
void a_disabled_player_performs_the_basic_attack() {
	const std::shared_ptr<const open::ItemCatalog> made = catalog_with(starters_plus_gates());
	open::Character swordsman = sheet(10, made);
	wield(swordsman, kSword, 509);
	const open::WeaponAttackProfile sword = profile_of(swordsman);

	open::TechniqueState off = player_sequence({step_of(kSlash, false), step_of(kThrust, false)});
	off.enabled = false;
	std::mt19937_64 rng(13);
	std::mt19937_64 untouched(rng);
	const open::TechniqueSelection basic = open::select_technique(swordsman, sword, off, rng);
	expect_true("with techniques off, the sequence is not walked", !basic.technique.has_value());
	expect_true("and nothing is proposed, so nothing advances", !basic.proposed_index.has_value());
	expect_true("and no luck was spent", rng() == untouched());
	expect_true("and the sequence is still exactly as configured",
				off.sequence.size() == 2u && off.index == 0u);

	// Turning it back on restores the configured behaviour, clocks and index intact.
	off.enabled = true;
	const open::TechniqueSelection on = open::select_technique(swordsman, sword, off, rng);
	expect_true("and on again, the first entry is what happens",
				on.technique == std::optional<open::TechniqueId>(kSlash));

	// An NPC ignores the flag: it has no configuration screen to disable.
	open::TechniqueState npc;
	npc.enabled = false;
	bool any = false;
	for (int attempt = 0; attempt < 20 && !any; ++attempt) {
		any = open::select_technique(swordsman, sword, npc, rng).technique.has_value();
	}
	expect_true("the switch is the player's, not the fighter's", any);
}

// The configured sequence: one loop per attack, advancing only when something was actually
// chosen, looping at the end, and honouring Skip and Wait as two different promises.
void the_sequence_walks_one_loop_per_attack() {
	const std::shared_ptr<const open::ItemCatalog> made = catalog_with(starters_plus_gates());
	open::Character swordsman = sheet(11, made);
	wield(swordsman, kSword, 510);
	const open::WeaponAttackProfile sword = profile_of(swordsman);
	std::mt19937_64 rng(17);

	open::TechniqueState state = player_sequence({step_of(kSlash, false), step_of(kThrust, false)});
	const open::TechniqueSelection first = open::select_technique(swordsman, sword, state, rng);
	expect_true("the first entry is what the next attack performs",
				first.technique == std::optional<open::TechniqueId>(kSlash));
	expect_true("and the sequence would move on by one",
				first.proposed_index == std::optional<std::size_t>(1));

	state.index = 1;
	const open::TechniqueSelection second = open::select_technique(swordsman, sword, state, rng);
	expect_true("the second entry follows", second.technique == std::optional<open::TechniqueId>(kThrust));
	expect_true("and the loop wraps back to the first",
				second.proposed_index == std::optional<std::size_t>(0));

	// A single-entry sequence proposes its own beginning, which is how "loops back after
	// its last technique" reads when there is one technique.
	open::TechniqueState alone = player_sequence({step_of(kThrust, false)});
	const open::TechniqueSelection repeated = open::select_technique(swordsman, sword, alone, rng);
	expect_true("the only entry is chosen",
				repeated.technique == std::optional<open::TechniqueId>(kThrust));
	expect_true("and stays the only entry",
				repeated.proposed_index == std::optional<std::size_t>(0));

	// Duplicates are the documented way to repeat an entry: they are separate positions,
	// not one entry visited twice.
	open::TechniqueState doubled =
		player_sequence({step_of(kSlash, false), step_of(kSlash, false), step_of(kThrust, false)});
	const open::TechniqueSelection double_first = open::select_technique(swordsman, sword, doubled, rng);
	expect_true("a duplicated entry is a normal entry",
				double_first.technique == std::optional<open::TechniqueId>(kSlash) &&
					double_first.proposed_index == std::optional<std::size_t>(1));
	doubled.index = 1;
	const open::TechniqueSelection double_second = open::select_technique(swordsman, sword, doubled, rng);
	expect_true("and its second copy is the next entry, not a repeat of the first",
				double_second.technique == std::optional<open::TechniqueId>(kSlash) &&
					double_second.proposed_index == std::optional<std::size_t>(2));

	// Skip: an entry on cooldown is walked past, and the position it held is not what
	// comes next.
	open::TechniqueState skipping =
		player_sequence({step_of(kSlash, true), step_of(kThrust, true)});
	skipping.cooldowns[kSlash] = 3.0;
	const open::TechniqueSelection skipped = open::select_technique(swordsman, sword, skipping, rng);
	expect_true("a cooling entry configured to skip is skipped",
				skipped.technique == std::optional<open::TechniqueId>(kThrust));
	expect_true("and the position after the skipped one is what would follow",
				skipped.proposed_index == std::optional<std::size_t>(0));

	// Wait: the entry stays where it was and the character throws basics meanwhile. That is
	// the difference between the two settings, and it is asserted by comparing the two.
	open::TechniqueState waiting =
		player_sequence({step_of(kSlash, false), step_of(kThrust, true)});
	waiting.cooldowns[kSlash] = 3.0;
	const open::TechniqueSelection held = open::select_technique(swordsman, sword, waiting, rng);
	expect_basic("an entry configured to wait keeps its place and performs a basic attack", held,
				 waiting);
	expect_true("and it is the waiting entry that is held, not the skipping one",
				waiting.sequence[1].skip_on_cooldown && !waiting.sequence[0].skip_on_cooldown);

	// A loop that finds nothing usable is a basic attack with the index untouched, and it
	// looked at each entry once: an entry earlier in the list than the index that is ready
	// may not be reached twice in one attack.
	open::TechniqueState all_cooling =
		player_sequence({step_of(kSlash, false), step_of(kThrust, false)});
	all_cooling.index = 1;
	all_cooling.cooldowns[kSlash] = 9.0;
	all_cooling.cooldowns[kThrust] = 9.0;
	const open::TechniqueSelection stalled = open::select_technique(swordsman, sword, all_cooling, rng);
	expect_basic("with every entry waiting, this attack is a basic one", stalled, all_cooling);
	std::mt19937_64 quiet(4);
	std::mt19937_64 quiet_copy(quiet);
	open::select_technique(swordsman, sword, all_cooling, quiet);
	expect_true("and a sequence that found nothing spent no luck", quiet() == quiet_copy());
}

// Entries that are unusable today are passed over, never removed: the same configuration
// has to start working again when the weapon or the unlock comes back.
void unusable_entries_are_skipped_without_being_deleted() {
	const std::shared_ptr<const open::ItemCatalog> made = catalog_with(starters_plus_gates());
	open::Character swordsman = sheet(12, made);
	wield(swordsman, kSword, 511);
	const open::WeaponAttackProfile sword = profile_of(swordsman);
	std::mt19937_64 rng(19);

	// A technique the sword does not support, and a rank the wielder has not reached. The
	// unsupported one is not even in the catalog's favour: it must be refused by support,
	// not by luck.
	open::TechniqueState state = player_sequence({
		step_of(kHeavyStrike, false),  // supported by the hammer, not this sword
		step_of(kRankGate, false),     // trained 50, this sheet is rank 1
		step_of(kThrust, false),       // the one that will be chosen
	});
	const open::TechniqueSelection picked = open::select_technique(swordsman, sword, state, rng);
	expect_true("the first usable entry wins",
				picked.technique == std::optional<open::TechniqueId>(kThrust));
	expect_true("and the proposal moves past it",
				picked.proposed_index == std::optional<std::size_t>(0));
	expect_true("while all three entries are still configured", state.sequence.size() == 3u);

	// An id that names nothing at all is the same shape of unusable.
	open::TechniqueState ghost = player_sequence({step_of(4242, false), step_of(kSlash, false)});
	const open::TechniqueSelection from_ghost = open::select_technique(swordsman, sword, ghost, rng);
	expect_true("an id the catalog does not know is skipped",
				from_ghost.technique == std::optional<open::TechniqueId>(kSlash));
	expect_true("and it is still in the sequence afterwards", ghost.sequence.size() == 2u);

	// The whole sequence unusable is a basic attack, with the sequence intact and its
	// index exactly where the player left it.
	open::TechniqueState hopeless = player_sequence({step_of(kHeavyStrike, false)});
	hopeless.index = 0;
	const open::TechniqueSelection basic = open::select_technique(swordsman, sword, hopeless, rng);
	expect_basic("a sequence with nothing in it performs the weapon's basic attack", basic, hopeless);
	expect_true("and nothing was deleted to make it so", hopeless.sequence.size() == 1u);

	// Switching the weapon to one that supports the entry makes it work again, which is the
	// whole reason skipping is not deleting.
	open::Character hammerman = sheet(13, made);
	wield(hammerman, kHammer, 512);
	const open::WeaponAttackProfile hammer = profile_of(hammerman);
	expect_true("the same configuration, on the weapon that supports it, performs the technique",
				open::select_technique(hammerman, hammer, hopeless, rng).technique ==
					std::optional<open::TechniqueId>(kHeavyStrike));

	// And an unlock the sheet later earns works the same way: the entry was already in the
	// list, already supported, and simply was not the sheet's yet.
	train(swordsman, sword.job, 50u);
	open::TechniqueState gated = player_sequence({step_of(kRankGate, false)});
	const open::WeaponAttackProfile trained = profile_of(swordsman);
	expect_true("the sword supported the gated technique all along",
				supported(trained).count(kRankGate) == 1u);
	expect_true("the rank gate now opens and the entry is used",
				open::select_technique(swordsman, trained, gated, rng).technique ==
					std::optional<open::TechniqueId>(kRankGate));
}

// Editing and changing controllers are configuration, not punishment: the clocks a fight
// has already started are nobody's to give back.
void editing_and_control_keep_the_clocks() {
	const std::shared_ptr<const open::ItemCatalog> made = catalog_with(starters_plus_gates());
	open::Character swordsman = sheet(14, made);
	wield(swordsman, kSword, 513);
	const open::WeaponAttackProfile sword = profile_of(swordsman);

	open::TechniqueState state = player_sequence({step_of(kSlash, false), step_of(kThrust, false)});
	state.index = 1;
	state.cooldowns[kSlash] = 4.0;
	state.cooldowns[kThrust] = 2.0;

	std::vector<open::TechniqueStep> edited = {step_of(kThrust, true), step_of(kSlash, true),
											   step_of(kThrust, true)};
	open::edit_technique_sequence(state, edited);
	expect_true("the new sequence is the one that was asked for", state.sequence == edited);
	expect_true("and it restarts at its first entry", state.index == 0u);
	expect_true("while every cooldown is exactly where the fight left it",
				state.cooldowns.at(kSlash) == 4.0 && state.cooldowns.at(kThrust) == 2.0);
	expect_true("with the clock count unchanged", state.cooldowns.size() == 2u);
	expect_true("and the enabled switch left alone", state.enabled);

	// An empty edit is still an edit: a cleared sequence starts at the beginning, which is
	// nowhere, and still does not refund a clock.
	open::edit_technique_sequence(state, {});
	expect_true("the sequence is empty", state.sequence.empty());
	expect_true("the index is at the start", state.index == 0u);
	expect_true("and the clocks survived again",
				state.cooldowns.size() == 2u && state.cooldowns.at(kSlash) == 4.0 &&
					state.cooldowns.at(kThrust) == 2.0);

	// Handing the sheet to the player, and taking it back, preserves both the sequence and
	// the clocks; the controller flag is the only thing that changes.
	open::TechniqueState npc;
	npc.enabled = true;
	open::edit_technique_sequence(npc, edited);
	npc.index = 2;
	npc.cooldowns[kSlash] = 1.5;
	open::set_technique_controller(npc, true);
	expect_true("the player gets the configured sequence", npc.sequence == edited);
	expect_true("with the entry they were on still next", npc.index == 2u);
	expect_true("and the clock still running", npc.cooldowns.at(kSlash) == 1.5);
	expect_true("while the flag is what flipped", npc.player_controlled);

	open::set_technique_controller(npc, false);
	expect_true("and flipping it back preserves it all again",
				!npc.player_controlled && npc.sequence == edited && npc.index == 2u &&
					npc.cooldowns.at(kSlash) == 1.5);
	expect_true("without touching whether techniques are on", npc.enabled);

	// Which is a behaviour, not just a bookkeeping claim: the same configuration chooses
	// differently the moment the controller changes, because a player's sequence is a
	// sequence and an NPC's is nothing.
	open::TechniqueState handed = player_sequence({step_of(kThrust, false)});
	open::TechniqueState taken = handed;
	open::set_technique_controller(taken, false);
	std::mt19937_64 rng(23);
	const open::TechniqueSelection by_player = open::select_technique(swordsman, sword, handed, rng);
	const open::TechniqueSelection by_npc = open::select_technique(swordsman, sword, taken, rng);
	expect_true("the player performs the entry they configured",
				by_player.technique == std::optional<open::TechniqueId>(kThrust));
	expect_true("and the NPC picks from the ready pool instead", by_npc.technique.has_value());
	expect_true("with the NPC proposing no sequence position", !by_npc.proposed_index.has_value());
}

}  // namespace

int main() {
	the_starter_rows_are_the_locked_numbers();
	a_weapon_must_support_the_technique();
	bare_hands_support_nothing();
	unlock_is_the_resolved_job_rank_or_a_title();
	malformed_definitions_and_links_are_rejected();
	the_default_state_is_a_random_chooser();
	random_selection_uses_the_ready_set();
	a_disabled_player_performs_the_basic_attack();
	the_sequence_walks_one_loop_per_attack();
	unusable_entries_are_skipped_without_being_deleted();
	editing_and_control_keep_the_clocks();
	return test_summary();
}
