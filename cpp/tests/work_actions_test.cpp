#include "open/work_actions.hpp"

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/combat_session.hpp"
#include "open/items.hpp"
#include "open/move_order.hpp"
#include "open/starter_content.hpp"
#include "open/work.hpp"
#include "test_support.hpp"

// Task 16 tests: the workbench queue and its debit-at-start transactions.
//
// The rule under test everywhere is one sentence from docs/weapons.md: materials are paid
// when an action starts and are never paid back - so validation is front-loaded, arrival
// re-validates, and the only ways a started job ends are delivery and a spent failure. The
// shared matrix below runs cancel, outgoing and incoming combat (ordered, pursued, landed
// at fifteen), movement, death, and bench loss against the same three claims: the stock
// does not move a second time, nothing is delivered, and nothing is trained.
//
// Fixture content is test data except where a case names the starter tables; those cases
// read the production catalog directly.

namespace {

constexpr open::MaterialId kScrap = 100;      // the starter material doubles as the scrap stack
constexpr open::MaterialId kRuneDust = 101;
constexpr open::DefinitionId kBladeA = 201;
constexpr open::DefinitionId kBladeB = 202;
constexpr open::DefinitionId kBoard = 205;     // the fixture shield: no recipe may output it
constexpr open::EnchantmentId kEdge = 5;
constexpr open::WorkbenchId kForge = 1;
constexpr open::WorkbenchId kEnchantBench = 2;
constexpr open::RecipeId kRepairRecipe = 105;
constexpr open::RecipeId kEnchantRecipe = 106;
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

using Slots = std::bitset<static_cast<std::size_t>(open::Slot::Count)>;

Slots hands() {
	Slots s;
	s.set(static_cast<std::size_t>(open::Slot::LeftHand));
	s.set(static_cast<std::size_t>(open::Slot::RightHand));
	return s;
}

open::ItemDefinition weapon(open::DefinitionId id) {
	open::ItemDefinition item;
	item.id = id;
	item.model_key = "FixtureBlade";
	item.allowed_slots = hands();
	item.hand_use = open::HandUse::OneHanded;
	open::WeaponDefinition record;
	record.job = open::SkillJob{open::BasicSkill::OneHandedWeapons, std::nullopt};
	record.stats.damage = {6.0, 2.0, 0.0};
	record.stats.max_reach = 1.5;
	record.stats.speed = 1.0;
	record.stats.stamina_cost = 8.0;
	record.stats.requirements = {{open::Attribute::Dexterity, 5.0}};
	record.stats.required_trained_rank = 1u;
	item.weapon = std::move(record);
	return item;
}

open::ItemDefinition shield(open::DefinitionId id) {
	open::ItemDefinition item;
	item.id = id;
	item.model_key = "FixtureBoard";
	item.allowed_slots = hands();
	item.hand_use = open::HandUse::OneHanded;
	open::ShieldDefinition board;
	board.job = open::SkillJob{open::BasicSkill::Blocking, std::nullopt};
	board.block_percent = 60.0;
	item.shield = board;
	return item;
}

open::MaterialDefinition material(open::MaterialId id) {
	open::MaterialDefinition m;
	m.id = id;
	m.max_durability_by_quality.assign(open::kQualityTierCount, 100u);
	return m;
}

open::EnchantmentDefinition edge() {
	open::EnchantmentDefinition e;
	e.id = kEdge;
	e.model_key = "FixtureEdge";
	e.blunt_per_trained_rank = 0.01;
	e.requirement = open::Attribute::Intelligence;
	e.requirement_divisor = 20u;
	return e;
}

open::CraftRecipe craft(open::RecipeId id, open::DefinitionId output) {
	open::CraftRecipe r;
	r.id = id;
	r.kind = open::WorkKind::CraftWeapon;
	r.bench = open::BenchType::Smithing;
	r.output = output;
	r.material = kScrap;
	r.cost = {{kScrap, 2u}};
	return r;
}

open::CraftRecipe repair_recipe() {
	open::CraftRecipe r;
	r.id = kRepairRecipe;
	r.kind = open::WorkKind::Repair;
	r.bench = open::BenchType::Smithing;
	r.cost = {{kScrap, 1u}};
	return r;
}

open::CraftRecipe enchant_recipe() {
	open::CraftRecipe r;
	r.id = kEnchantRecipe;
	r.kind = open::WorkKind::EnchantWeapon;
	r.bench = open::BenchType::Enchanting;
	r.cost = {{kRuneDust, 1u}};
	return r;
}

// The fixture catalog: every id and cost the cases cite, from one validated build.
std::shared_ptr<const open::ItemCatalog> fixture_catalog(std::vector<open::CraftRecipe> recipes = {}) {
	if (recipes.empty()) {
		recipes = {craft(101, kBladeA), craft(102, kBladeB), repair_recipe(), enchant_recipe()};
	}
	const auto made = open::ItemCatalog::create(
		{weapon(kBladeA), weapon(kBladeB), shield(kBoard)}, {material(kScrap), material(kRuneDust)},
		{edge()}, {}, std::move(recipes));
	if (!made.has_value()) {
		++g_failed;
		std::cerr << "FAIL the fixture catalog refused to build\n";
		return nullptr;
	}
	return std::make_shared<const open::ItemCatalog>(*made);
}

open::Character sheet(std::uint64_t id, const std::shared_ptr<const open::ItemCatalog> &catalog,
					  const char *name = "Smith") {
	open::CharacterConfig config;
	config.id = id;
	config.name = name;
	auto made = open::Character::create(config, catalog);
	if (!made.has_value()) {
		++g_failed;
		std::cerr << "FAIL the sheet refused to build\n";
	}
	return std::move(made).value();
}

open::Vec3 at(double x, double z, double y = 0.0) {
	return open::Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

// One workbench standing at the origin, and the context around it.
struct World {
	explicit World(std::shared_ptr<const open::ItemCatalog> cat = fixture_catalog())
		: catalog(std::move(cat)), env(*catalog, ids, ledger) {
		env.benches = {{kForge, open::BenchType::Smithing, at(0.0, 0.0)},
					   {kEnchantBench, open::BenchType::Enchanting, at(50.0, 0.0)}};
	}
	std::shared_ptr<const open::ItemCatalog> catalog;
	open::ItemIdAllocator ids;
	open::MaterialLedger ledger;
	open::WorkContext env;
};

std::uint64_t stock(const World &world, std::uint64_t owner, open::MaterialId material) {
	return world.ledger.quantity(owner, material);
}

bool pay(World &world, std::uint64_t owner, open::MaterialId material, std::uint64_t amount) {
	const open::MaterialQuantity line{material, amount};
	return world.ledger.consume(owner, {&line, 1});
}

open::ItemId give(open::Character &who, const open::ItemCatalog &catalog, open::DefinitionId def,
				  open::ItemId id, unsigned quality = 1u) {
	const auto made = open::make_item_instance(catalog, def, kScrap, id, quality);
	if (!made.has_value() || !who.add_instance(*made)) {
		++g_failed;
		std::cerr << "FAIL the fixture item could not be given\n";
	}
	return id;
}

open::WorkRequest craft_request(open::RecipeId recipe, unsigned quality = 1u,
								open::WorkbenchId bench = kForge) {
	open::WorkRequest r;
	r.kind = open::WorkKind::CraftWeapon;
	r.recipe = recipe;
	r.bench = bench;
	r.quality = quality;
	return r;
}

open::WorkRequest repair_request(open::ItemId target, open::WorkbenchId bench = kForge) {
	open::WorkRequest r;
	r.kind = open::WorkKind::Repair;
	r.recipe = kRepairRecipe;
	r.bench = bench;
	r.target = target;
	return r;
}

open::WorkRequest enchant_request(open::ItemId target, open::WorkbenchId bench = kEnchantBench) {
	open::WorkRequest r;
	r.kind = open::WorkKind::EnchantWeapon;
	r.recipe = kEnchantRecipe;
	r.bench = bench;
	r.target = target;
	r.enchantment = kEdge;
	return r;
}

std::size_t backpack_size(const open::Character &who) {
	return who.backpack_items().size();
}

unsigned smithing_rank(const open::Character &who) {
	return who.resolve_skill(open::BasicSkill::Smithing, std::nullopt).rank;
}

bool has_instance(const open::Character &who, open::ItemId id) {
	return who.instance(id).has_value();
}

// Drive the session one frame with the actors where the test put them.
open::Vec3 positions_a = at(0.0, 0.0);  // scratch filled by the fight fixtures

}  // namespace

// --- the ledger -------------------------------------------------------------------

void ledger_transactions() {
	World world;
	expect_true("an unknown owner owes nothing", stock(world, 1, kScrap) == 0);
	expect_true("and an unknown material does", stock(world, 1, 9999) == 0);

	expect_true("setup credit is accepted", world.ledger.grant(1, kScrap, 20));
	expect_true("and two materials live side by side",
				world.ledger.grant(1, kRuneDust, 5) && stock(world, 1, kScrap) == 20
					&& stock(world, 1, kRuneDust) == 5);
	expect_true("a second grant adds to the stack",
				world.ledger.grant(1, kScrap, 1) && stock(world, 1, kScrap) == 21);
	expect_true("zero is not credit",
				!world.ledger.grant(1, kScrap, 0) && stock(world, 1, kScrap) == 21);

	// A cost is paid by every line or by none of them.
	const open::MaterialQuantity both[] = {{kScrap, 20}, {kRuneDust, 6}};
	expect_true("one short line refuses the whole cost",
				!world.ledger.consume(1, both) && stock(world, 1, kScrap) == 21
					&& stock(world, 1, kRuneDust) == 5);
	const open::MaterialQuantity exact[] = {{kScrap, 21}, {kRuneDust, 5}};
	expect_true("an exactly-payable cost moves both lines",
				world.ledger.consume(1, exact) && stock(world, 1, kScrap) == 0
					&& stock(world, 1, kRuneDust) == 0);
	expect_true("and an empty stack pays nothing further",
				!pay(world, 1, kScrap, 1) && stock(world, 1, kScrap) == 0);
	const open::MaterialQuantity zero_line[] = {{kScrap, 0}};
	expect_true("a zero line is a shape error, not a free pass",
				!world.ledger.consume(1, zero_line));
	expect_true("owners keep their own stacks",
				world.ledger.grant(2, kScrap, 3) && stock(world, 1, kScrap) == 0
					&& stock(world, 2, kScrap) == 3);

	// The no-wrap rule: a stack cannot be grown past the counter.
	expect_true("the top of the range is creditable",
				world.ledger.grant(3, kScrap, std::numeric_limits<std::uint64_t>::max()));
	expect_true("and a single unit past it is refused untouched",
				!world.ledger.grant(3, kScrap, 1)
					&& stock(world, 3, kScrap) == std::numeric_limits<std::uint64_t>::max());

	// The setup marker is what makes "one-time" a fact rather than a habit.
	expect_true("an un-set-up owner has no marker", !world.ledger.was_granted(7));
	world.ledger.mark_granted(7);
	expect_true("and marking is remembered", world.ledger.was_granted(7));
}

// --- the recipe table, in the catalog and in the starter slice ---------------------

void catalog_recipe_validation() {
	// The fixture builds once with a good table, and every single-entry defect rejects a
	// whole catalog the way every other table does.
	expect_true("a good table builds", fixture_catalog() != nullptr);

	const auto with_recipe = [](open::CraftRecipe bad) {
		return open::ItemCatalog::create(
			{weapon(kBladeA), weapon(kBladeB), shield(kBoard)},
			{material(kScrap), material(kRuneDust)}, {edge()}, {},
			std::vector<open::CraftRecipe>{bad});
	};
	open::CraftRecipe base = craft(101, kBladeA);
	expect_true("zero id is refused", !with_recipe([&] { auto r = base; r.id = 0; return r; }()));
	expect_true("a non-weapon output is refused",
				!with_recipe([&] { auto r = base; r.output = kBoard; return r; }()));
	expect_true("an unknown output is refused",
				!with_recipe([&] { auto r = base; r.output = 9999; return r; }()));
	expect_true("an unknown build material is refused",
				!with_recipe([&] { auto r = base; r.material = 9999; return r; }()));
	expect_true("an empty cost is refused",
				!with_recipe([&] { auto r = base; r.cost.clear(); return r; }()));
	expect_true("a zero quantity is refused",
				!with_recipe([&] { auto r = base; r.cost = {{kScrap, 0}}; return r; }()));
	expect_true("an unknown cost material is refused",
				!with_recipe([&] { auto r = base; r.cost = {{9999, 1}}; return r; }()));
	expect_true("listing one material twice is refused",
				!with_recipe([&] { auto r = base; r.cost = {{kScrap, 1}, {kScrap, 1}}; return r; }()));
	expect_true("a bench outside the domain is refused",
				!with_recipe([&] {
					auto r = base;
					r.bench = open::BenchType::Count;
					return r;
				}()));
	expect_true("a shield cannot be the kind of a craft recipe",
				!with_recipe([&] {
					auto r = repair_recipe();
					r.id = 150;
					r.kind = open::WorkKind::CraftWeapon;
					r.material = kScrap;
					r.output = kBoard;
					return r;
				}()));
	expect_true("a crafting recipe with no output is refused",
				!with_recipe([&] {
					auto r = base;
					r.output = 0;
					return r;
				}()));
	expect_true("a repair recipe cannot forge things",
				!with_recipe([&] {
					auto r = repair_recipe();
					r.output = kBladeA;
					r.material = kScrap;
					return r;
				}()));
	expect_true("repair belongs to the smithing bench",
				!with_recipe([&] {
					auto r = repair_recipe();
					r.bench = open::BenchType::Enchanting;
					return r;
				}()));
	expect_true("enchanting belongs to the enchanting bench",
				!with_recipe([&] {
					auto r = enchant_recipe();
					r.bench = open::BenchType::Smithing;
					return r;
				}()));
	expect_true("a repair recipe that charges nothing is refused",
				!with_recipe([&] {
					auto r = repair_recipe();
					r.cost.clear();
					return r;
				}()));

	// Duplicate ids and the one-per-action rule are catalog facts, not caller manners.
	{
		const auto dup = open::ItemCatalog::create(
			{weapon(kBladeA), weapon(kBladeB), shield(kBoard)},
			{material(kScrap), material(kRuneDust)}, {edge()}, {},
			std::vector<open::CraftRecipe>{craft(101, kBladeA), craft(101, kBladeB)});
		expect_true("two recipes with one id reject the catalog", !dup.has_value());
	}
	{
		const auto twice = open::ItemCatalog::create(
			{weapon(kBladeA), weapon(kBladeB), shield(kBoard)},
			{material(kScrap), material(kRuneDust)}, {edge()}, {},
			std::vector<open::CraftRecipe>{repair_recipe(),
										   [] {
											   auto r = repair_recipe();
											   r.id = 151;
											   return r;
										   }()});
		expect_true("a second repair recipe rejects the catalog", !twice.has_value());
	}
	{
		const auto look = fixture_catalog();
		expect_true("the good table enumerates sorted",
					look->recipe_ids() == std::vector<open::RecipeId>{101, 102, kRepairRecipe, kEnchantRecipe});
		expect_true("lookups resolve", look->recipe(101) != nullptr && look->recipe(0) == nullptr
										   && look->recipe(9999) == nullptr);
	}
}

void starter_work_table() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	if (catalog == nullptr) {
		expect_true("the starter catalog builds", false);
		return;
	}
	expect_true("the starter catalog carries the four forge recipes and the two action recipes",
				catalog->recipe_ids() == std::vector<open::RecipeId>{101, 102, 103, 104, 105, 106});
	for (const open::DefinitionId definition : open::starter_definitions()) {
		const auto *recipe = catalog->recipe(definition);  // 101-104 share their output's number
		const auto *def = catalog->item(definition);
		const bool weapon_kind = def != nullptr && def->weapon.has_value();
		if (weapon_kind) {
			expect_true("every starter weapon has its forge recipe",
						recipe != nullptr && recipe->kind == open::WorkKind::CraftWeapon
							&& recipe->output == definition && recipe->material == open::kStarterMaterial
							&& recipe->cost == std::vector<open::MaterialQuantity>{{open::kStarterMaterial, 2}});
		} else {
			expect_true("the shield has no forge recipe - nothing may output it",
						recipe == nullptr || recipe->kind != open::WorkKind::CraftWeapon);
		}
	}
	const auto *repair = catalog->recipe(105);
	expect_true("repair costs one scrap at the smithing bench",
				repair != nullptr && repair->kind == open::WorkKind::Repair
					&& repair->cost == std::vector<open::MaterialQuantity>{{open::kStarterMaterial, 1}});
	const auto *enchant = catalog->recipe(106);
	expect_true("enchanting costs one rune dust at the enchanting bench",
				enchant != nullptr && enchant->kind == open::WorkKind::EnchantWeapon
					&& enchant->bench == open::BenchType::Enchanting
					&& enchant->cost == std::vector<open::MaterialQuantity>{{open::kRuneDustMaterial, 1}});
	expect_true("and rune dust is a declared material", catalog->material(open::kRuneDustMaterial) != nullptr);
}

void starter_setup_supplies_materials_once() {
	const std::shared_ptr<const open::ItemCatalog> catalog = open::make_starter_catalog();
	World world(catalog);
	open::Character who = sheet(1, catalog);
	expect_true("setup fills both sides of the backpack",
				open::initialize_starter_inventory(who, world.ids, world.ledger));
	expect_true("twenty scrap", stock(world, who.id(), kScrap) == 20);
	expect_true("and five rune dust", stock(world, who.id(), kRuneDust) == 5);
	expect_true("the items arrived too", backpack_size(who) == 5);

	expect_true("setup again changes nothing",
				open::initialize_starter_inventory(who, world.ids, world.ledger)
					&& stock(world, who.id(), kScrap) == 20 && backpack_size(who) == 5);

	// A spent stack stays spent: the second setup is a memory, not a faucet.
	pay(world, who.id(), kScrap, 20);
	expect_true("setup survives a spent backpack without topping it up",
				open::initialize_starter_inventory(who, world.ids, world.ledger)
					&& stock(world, who.id(), kScrap) == 0);
	expect_true("the marker says it was issued", world.ledger.was_granted(who.id()));
}

// --- requests: what shape is legal, and what a refusal must not have touched -------

open::Vec3 smith_at = at(1.0, 0.0);  // scratch: where the fight fixtures stand the smith

std::vector<open::Combatant> roster_of(open::Character &who, const open::Vec3 &pos) {
	std::vector<open::Combatant> roster;
	roster.push_back(open::Combatant{&who, pos});
	return roster;
}

open::Combatant &slot_at(std::vector<open::Combatant> &roster, std::uint64_t id) {
	for (open::Combatant &entry : roster) {
		if (entry.character->id() == id) {
			return entry;
		}
	}
	++g_failed;
	std::cerr << "FAIL the roster has no such sheet\n";
	return roster.front();
}

bool grant_kit(World &world, std::uint64_t owner, std::uint64_t scrap = 20, std::uint64_t dust = 5) {
	return world.ledger.grant(owner, kScrap, scrap) && world.ledger.grant(owner, kRuneDust, dust);
}

const char *phase_name(open::WorkPhase phase) {
	switch (phase) {
	case open::WorkPhase::Idle: return "idle";
	case open::WorkPhase::Queued: return "queued";
	case open::WorkPhase::Active: return "active";
	case open::WorkPhase::Completed: return "completed";
	case open::WorkPhase::Failed: return "failed";
	}
	return "?";
}

void expect_phase(const char *name, const open::WorkStatus &status, open::WorkPhase phase) {
	if (status.phase != phase) {
		++g_failed;
		std::cerr << "FAIL " << name << ": phase is " << phase_name(status.phase) << ", want "
				  << phase_name(phase) << "\n";
	}
}

void expect_failed(const char *name, const open::WorkStatus &status, open::WorkFailure reason) {
	expect_phase(name, status, open::WorkPhase::Failed);
	if (status.phase == open::WorkPhase::Failed && status.failure != reason) {
		++g_failed;
		std::cerr << "FAIL " << name << ": wrong failure reason\n";
	}
}

void request_shapes() {
	World world;
	open::Character who = sheet(1, world.catalog);
	open::CombatSession session(0);
	auto roster = roster_of(who, at(1.0, 0.0));
	grant_kit(world, who.id());
	const auto idle = who.work_status();

	// Every shape a request can get wrong, against a bench that exists.
	expect_true("a good craft request is accepted",
				session.request_work(roster, who.id(), craft_request(101), world.env));
	// (accepted: settle it away below, then continue the shape battery)
	expect_phase("the accepted request started", who.work_status(), open::WorkPhase::Active);
	expect_true("and one work job at a time is the rule",
				!session.request_work(roster, who.id(), craft_request(102), world.env));
	expect_phase("the rejected repeat left the job alone", who.work_status(),
				 open::WorkPhase::Active);

	// Finish the job so the shape battery below reads a clean sheet: step to done.
	for (int i = 0; i < 20; ++i) {
		session.step(1.0, roster, world.env);
	}
	expect_phase("the job settled", who.work_status(), open::WorkPhase::Completed);
	expect_true("the invalid repeat is still not resurrecting anything",
				!session.request_work(roster, who.id(), craft_request(99999), world.env));
	expect_phase("and naming a bad recipe cannot even rewrite the kept result",
				 who.work_status(), open::WorkPhase::Completed);
	(void)idle;
	(void)session.cancel_work(roster, who.id(), world.env);

	// From here the sheet is Idle again after one successful cancel... cancels of terminal
	// work answer false, so shape the state directly:
	// A fresh smith, because Completed is kept until the next accepted job.
	open::Character fresh_smith = sheet(2, world.catalog);
	grant_kit(world, fresh_smith.id());
	auto fresh_roster = roster_of(fresh_smith, at(1.0, 0.0));

	const auto refused = [&](const char *claim, const open::WorkRequest &request,
							 const std::vector<open::Combatant> &actors = {}) {
		const auto &use = actors.empty() ? fresh_roster : actors;
		const auto before = fresh_smith.work_status();
		const std::uint64_t stock_before = stock(world, fresh_smith.id(), kScrap);
		expect_true(claim, !session.request_work(use, fresh_smith.id(), request, world.env));
		expect_phase("a shape refusal creates no job", fresh_smith.work_status(), before.phase);
		expect_true("a shape refusal pays nothing",
					stock(world, fresh_smith.id(), kScrap) == stock_before);
	};

	refused("an unknown bench is refused", craft_request(101, 1, 999));
	refused("bench zero is refused", craft_request(101, 1, 0));
	refused("crafting at the enchanting bench is refused", craft_request(101, 1, kEnchantBench));
	refused("repairing at the enchanting bench is refused",
			repair_request(give(fresh_smith, *world.catalog, kBladeA, 41), kEnchantBench));
	refused("enchanting at the smithing bench is refused",
			enchant_request(give(fresh_smith, *world.catalog, kBladeA, 42), kForge));
	refused("an unknown recipe is refused", craft_request(777));
	refused("a recipe of the wrong kind is refused",
			[&] { auto r = craft_request(kRepairRecipe); return r; }());
	refused("quality zero is not a tier", craft_request(101, 0));
	refused("quality eight is not a tier", craft_request(101, 8));
	{
		open::WorkRequest r = craft_request(101);
		r.target = 7;  // craft acts on nothing but the recipe
		refused("a craft naming a target is refused", r);
	}
	refused("repair with no target is refused",
			[] { auto r = repair_request(0); r.target = std::nullopt; return r; }());
	refused("a repair target nobody owns is refused", repair_request(55555));
	refused("enchant with no target is refused",
			[] { auto r = enchant_request(0); r.target = std::nullopt; return r; }());
	refused("an unknown enchantment is refused",
			[&] { auto r = enchant_request(give(fresh_smith, *world.catalog, kBladeA, 43));
				 r.enchantment = 9999; return r; }());
	{
		const open::ItemId board = give(fresh_smith, *world.catalog, kBoard, 44);
		refused("a shield cannot be enchanted", enchant_request(board));
	}
	{
		// A placement-only record has no durability and no make: it is not repairable.
		open::ItemRef legacy;
		legacy.id = 45;
		legacy.allowed_slots = hands();
		legacy.hand_use = open::HandUse::OneHanded;
		expect_true("the legacy record goes in", fresh_smith.add_item(legacy));
		refused("a placement record cannot be repaired", repair_request(45));
	}
	open::Character bystander_sheet = sheet(9, world.catalog);
	refused("a request naming an actor who is not in the roster is refused",
			craft_request(101), roster_of(bystander_sheet, at(25.0, 0.0)));
	{
		open::Character dead = sheet(3, world.catalog);
		grant_kit(world, dead.id());
		expect_true("and dies", dead.damage(open::BodyPart::Torso, 1.0e30f));
		auto dead_roster = roster_of(dead, at(1.0, 0.0));
		expect_true("a dead sheet is refused",
					!session.request_work(dead_roster, dead.id(), craft_request(101), world.env));
	}
	{
		// A bench whose observation is not a place: geometry that cannot be walked to.
		World broken;
		open::Character who_broken = sheet(4, broken.catalog);
		grant_kit(broken, who_broken.id());
		open::CombatSession broken_session(0);
		broken.env.benches[0].position = at(kNan, 0.0);
		auto broken_roster = roster_of(who_broken, at(1.0, 0.0));
		expect_true("a bench at NaN is refused",
					!broken_session.request_work(broken_roster, who_broken.id(),
												 craft_request(101), broken.env));
	}
}

// --- the craft: pay at the start, deliver at fifteen, once -------------------------

void craft_lifecycle() {
	World world;
	open::Character who = sheet(1, world.catalog);
	open::CombatSession session(0);
	auto roster = roster_of(who, at(1.0, 0.0));
	grant_kit(world, who.id());

	const unsigned rank_before = smithing_rank(who);
	const auto training_before = who.resolve_skill(open::BasicSkill::Smithing, std::nullopt);
	const std::size_t owned_before = backpack_size(who);
	// Take one id from the mint so the next free id is known: the start must spend it, and
	// a reservation-at-completion design would still be holding it while the bench works.
	const open::ItemId probe = *world.ids.allocate();
	const open::ItemId next_free = probe + 1;

	expect_true("the request starts on the spot, in reach",
				session.request_work(roster, who.id(), craft_request(101), world.env));
	const open::WorkStatus started = who.work_status();
	expect_phase("active immediately", started, open::WorkPhase::Active);
	expect_true("with the clock at zero", close(static_cast<float>(started.elapsed), 0.0f));
	expect_true("the scrap went in at the start, once", stock(world, who.id(), kScrap) == 18);
	expect_true("and the output id was reserved at the start, not kept for the settle",
				*world.ids.allocate() > next_free);

	// 14.999 seconds: not yet. Then the hundredth that ends it.
	for (int i = 0; i < 149; ++i) {
		session.step(0.1, roster, world.env);
	}
	expect_true("no output at fourteen point nine", backpack_size(who) == owned_before);
	expect_phase("still on the bench", who.work_status(), open::WorkPhase::Active);
	session.step(0.1, roster, world.env);
	const open::WorkStatus done = who.work_status();
	expect_phase("the fifteenth second delivered", done, open::WorkPhase::Completed);
	if (done.output.has_value()) {
		expect_true("exactly one new instance, under the id the start reserved",
					backpack_size(who) == owned_before + 1 && has_instance(who, *done.output)
						&& *done.output == next_free);
		const auto made = who.instance(*done.output);
		expect_true("the recipe's weapon at the chosen tier, whole",
					made.has_value() && made->definition == kBladeA && made->quality == 1u
						&& made->durability == 100u && made->max_durability == 100u
						&& made->material == kScrap && made->enchantments.empty());
	} else {
		expect_true("the completion names its output", false);
	}
	expect_true("one point of Smithing, no more",
				who.resolve_skill(open::BasicSkill::Smithing, std::nullopt).xp
						- training_before.xp
					== 1u);
	expect_true("and one mark toward Hammerhand", who.title_count(open::Title::Hammerhand) == 1u);
	expect_true("the rank did not jump", smithing_rank(who) == rank_before);

	// A hundred more frames do not forge again, pay again, or train again.
	for (int i = 0; i < 100; ++i) {
		session.step(1.0, roster, world.env);
	}
	expect_true("the delivery happened exactly once", backpack_size(who) == owned_before + 1);
	expect_true("the scrap stayed spent", stock(world, who.id(), kScrap) == 18);
	expect_true("the XP stayed one point",
				who.resolve_skill(open::BasicSkill::Smithing, std::nullopt).xp
					- training_before.xp
				== 1u);
	expect_true("the completed result is kept until the next accepted job",
				who.work_status().phase == open::WorkPhase::Completed);
}

// --- quality: the cap is the sheet's, sampled where the work starts ----------------

void quality_caps() {
	World world;
	open::Character who = sheet(1, world.catalog);
	open::CombatSession session(0);
	auto roster = roster_of(who, at(1.0, 0.0));
	grant_kit(world, who.id());

	expect_true("a rank-1 smith is capped at tier 1", open::max_weapon_quality(who) == 1u);
	expect_true("tier 2 is refused before anything is paid",
				!session.request_work(roster, who.id(), craft_request(101, 2), world.env));
	expect_true("with the stock untouched and no job started",
				stock(world, who.id(), kScrap) == 20 && !who.work_status().busy());
	expect_true("a deliberately lower tier is legal",
				session.request_work(roster, who.id(), craft_request(101, 1), world.env));
	for (int i = 0; i < 20; ++i) {
		session.step(1.0, roster, world.env);
	}

	// A fresh Weaponsmith already reaches tier 4 from advanced rank 1, and no further.
	auto train_smithing_to_cap = [](open::Character &smith) {
		for (unsigned guard = 0; smithing_rank(smith) < open::kBasicRankCap && guard < 100000u;
			 ++guard) {
			smith.record_use(open::BasicSkill::Smithing, std::nullopt, true);
		}
	};
	train_smithing_to_cap(who);
	expect_true("the parent froze at 100", smithing_rank(who) == open::kBasicRankCap);
	expect_true("and chose the advanced track", who.advance(open::AdvancedSkill::Weaponsmith));
	expect_true("Weaponsmith rank 1 already caps at tier 4", open::max_weapon_quality(who) == 4u);
	expect_true("tier 4 is accepted", session.request_work(roster, who.id(), craft_request(101, 4),
														   world.env));
	for (int i = 0; i < 20; ++i) {
		session.step(1.0, roster, world.env);
	}
	const auto tier4 = who.work_status();
	expect_true("and delivered at tier 4",
				tier4.phase == open::WorkPhase::Completed && tier4.output.has_value()
					&& who.instance(*tier4.output)->quality == 4u
					&& who.instance(*tier4.output)->max_durability == 100u);
	expect_true("tier 5 is one band too far",
				!session.request_work(roster, who.id(), craft_request(101, 5), world.env));

	// An Armoursmith is frozen basic Smithing for blades: the advanced band does not leak.
	World armour_world;
	open::Character armoursmith = sheet(2, armour_world.catalog);
	open::CombatSession armour_session(0);
	auto armour_roster = roster_of(armoursmith, at(1.0, 0.0));
	grant_kit(armour_world, armoursmith.id());
	train_smithing_to_cap(armoursmith);
	expect_true("the armour track was chosen instead",
				armoursmith.advance(open::AdvancedSkill::Armoursmith));
	expect_true("an Armoursmith's blades still cap at tier 3",
				open::max_weapon_quality(armoursmith) == 3u);
	expect_true("tier 3 is accepted", armour_session.request_work(armour_roster, armoursmith.id(),
																  craft_request(101, 3),
																  armour_world.env));
	expect_true("tier 4 is refused",
				!armour_session.request_work(armour_roster, armoursmith.id(), craft_request(101, 4),
											 armour_world.env));
}

// --- walking there: queue, arrival, and what "no time credited" means ---------------

void queue_and_arrival() {
	World world;
	open::Character who = sheet(1, world.catalog);
	open::CombatSession session(0);
	auto roster = roster_of(who, at(4.0, 3.0));  // far, and off-axis in Y terms
	grant_kit(world, who.id());

	// Out of reach: the request is accepted as queued, issues its own approach, and pays
	// nothing. The move order is visible as a real walk velocity toward the bench.
	expect_true("an out-of-reach request is accepted as queued",
				session.request_work(roster, who.id(), craft_request(101), world.env));
	const auto status = who.work_status();
	expect_phase("queued", status, open::WorkPhase::Queued);
	expect_true("with the whole stock still there", stock(world, who.id(), kScrap) == 20);
	expect_true("and the actor is told to walk to the bench",
				who.destination().has_value() && who.velocity_at(4.0f, 0.0f, 3.0f).x * who.velocity_at(4.0f, 0.0f, 3.0f).x +
					who.velocity_at(4.0f, 0.0f, 3.0f).z * who.velocity_at(4.0f, 0.0f, 3.0f).z > 0.0f);

	// Walking: a queued job does not tick while the smith is still striding.
	session.step(1.0, roster, world.env);
	session.step(1.0, roster, world.env);
	expect_true("nothing elapsed while walking", close(static_cast<float>(who.work_status().elapsed),
													   0.0f));

	// Arrival: the start validates at the bench, pays there, and starts at zero.
	slot_at(roster, who.id()).position = at(0.9, 0.4);
	session.step(0.1, roster, world.env);
	const auto active = who.work_status();
	expect_phase("arrival started the job", active, open::WorkPhase::Active);
	expect_true("paid exactly then", stock(world, who.id(), kScrap) == 18);
	expect_true("and the walking minutes were never credited",
				close(static_cast<float>(active.elapsed), 0.0f));
	session.step(1.0, roster, world.env);
	expect_true("the first real bench-second is the first second",
				close(static_cast<float>(who.work_status().elapsed), 1.0f));

	// Boundary reach, Y ignored: exactly 1.5 is at the bench; 1.5 + epsilon is not.
	World boundary;
	open::Character near_smith = sheet(2, boundary.catalog);
	open::CombatSession near_session(0);
	grant_kit(boundary, near_smith.id());
	auto near_roster = roster_of(near_smith, at(1.5, 0.0, 7.0));
	expect_true("1.5 horizontal, whatever the Y, is at the bench",
				near_session.request_work(near_roster, near_smith.id(), craft_request(101),
										  boundary.env)
					&& near_smith.work_status().active());
	open::Character edge_smith = sheet(3, boundary.catalog);
	grant_kit(boundary, edge_smith.id());
	auto edge_roster = roster_of(edge_smith, at(1.5001, 0.0));
	expect_true("1.5 + epsilon is still walking",
				near_session.request_work(edge_roster, edge_smith.id(), craft_request(101), boundary.env)
					&& edge_smith.work_status().queued());

	// An immobile smith out of reach cannot magically work; the same smith inside reach can.
	World still;
	open::Character planted = sheet(4, still.catalog);
	open::CombatSession still_session(0);
	grant_kit(still, planted.id());
	planted.set_can_move(false);
	auto planted_roster = roster_of(planted, at(9.0, 0.0));
	expect_true("immobile and far away is refused outright",
				!still_session.request_work(planted_roster, planted.id(), craft_request(101),
											still.env)
					&& !planted.work_status().busy() && stock(still, planted.id(), kScrap) == 20);
	planted_roster[0].position = at(1.0, 0.0);
	expect_true("immobile but at the bench still hammers",
				still_session.request_work(planted_roster, planted.id(), craft_request(101), still.env)
					&& planted.work_status().active()
					&& stock(still, planted.id(), kScrap) == 18);

	// A queued job whose materials went away before arrival fails free at the bench.
	World spent;
	open::Character hopeful = sheet(5, spent.catalog);
	open::CombatSession spent_session(0);
	auto spent_roster = roster_of(hopeful, at(9.0, 0.0));
	grant_kit(spent, hopeful.id(), 2);
	expect_true("two scrap covers one recipe, as queued",
				spent_session.request_work(spent_roster, hopeful.id(), craft_request(101), spent.env));
	pay(spent, hopeful.id(), kScrap, 2);  // somebody ate the scrap
	slot_at(spent_roster, hopeful.id()).position = at(0.5, 0.0);
	spent_session.step(0.1, spent_roster, spent.env);
	expect_failed("arrival without materials fails", hopeful.work_status(),
				  open::WorkFailure::RequirementsUnmet);
	expect_true("for free", stock(spent, hopeful.id(), kScrap) == 0 && backpack_size(hopeful) == 0);
}

// --- combat, movement, bench loss: the interruption matrix -------------------------

// One smith standing at the forge with a paid craft on the anvil. Every case in this
// section starts exactly here and asks how the job ends.
struct AtTheBench {
	AtTheBench() : who(sheet(1, world.catalog)), session(0) {
		grant_kit(world, who.id());
		roster = roster_of(who, at(1.0, 0.0));
		if (!session.request_work(roster, who.id(), craft_request(101), world.env)) {
			++g_failed;
			std::cerr << "FAIL the matrix fixture could not start a job\n";
		}
	}
	World world;
	open::Character who;
	open::CombatSession session;
	std::vector<open::Combatant> roster;
};

// The claims that must hold after every interruption of a paid job.
void expect_spent_failure(const char *name, AtTheBench &b, open::WorkFailure reason) {
	const auto status = b.who.work_status();
	expect_failed(name, status, reason);
	expect_true("the paid scrap stays paid", stock(b.world, b.who.id(), kScrap) == 18);
	expect_true("nothing is delivered by a spent failure", backpack_size(b.who) == 0);
	expect_true("and nothing is trained",
				smithing_rank(b.who) == 1u && b.who.title_count(open::Title::Hammerhand) == 0u);
	// A spent failure is terminal: no number of frames turns it into a delivery.
	for (int i = 0; i < 30; ++i) {
		b.session.step(1.0, b.roster, b.world.env);
	}
	expect_true("and it never settles late",
				backpack_size(b.who) == 0 && b.who.work_status().phase == open::WorkPhase::Failed);
}

void active_interruptions() {
	// Voluntary cancel.
	{
		AtTheBench b;
		expect_true("cancel takes the job off the bench",
					b.session.cancel_work(b.roster, b.who.id(), b.world.env));
		expect_spent_failure("cancelled work", b, open::WorkFailure::Cancelled);
		expect_true("and cancelling finished work answers nothing",
					!b.session.cancel_work(b.roster, b.who.id(), b.world.env));
	}
	// A new move order, even without leaving reach - and through both front doors.
	{
		AtTheBench b;
		expect_true("the session move interrupts",
					b.session.issue_move(b.roster, b.who.id(), at(1.2, 0.0), b.world.env));
		expect_spent_failure("a move inside reach still interrupts", b, open::WorkFailure::Movement);
	}
	{
		AtTheBench b;
		expect_true("the raw sheet call interrupts too", b.who.set_destination(3.0f, 0.0f, 0.0f));
		expect_spent_failure("floor movement through the sheet interrupts", b,
							 open::WorkFailure::Movement);
	}
	// An outgoing attack order.
	{
		AtTheBench b;
		open::Character victim = sheet(2, b.world.catalog, "Victim");
		b.roster.push_back(open::Combatant{&victim, at(2.0, 0.0)});
		expect_true("the smith puts down the hammer to swing",
					b.session.issue_attack(b.roster, b.who.id(), victim.id()));
		expect_spent_failure("an outgoing attack interrupts", b, open::WorkFailure::Combat);
	}
	// An incoming attack order interrupts through the session's combat reading of the next
	// frame - and the race is pinned: at exactly fifteen, combat wins.
	{
		AtTheBench b;
		open::Character raider = sheet(2, b.world.catalog, "Raider");
		b.roster.push_back(open::Combatant{&raider, at(1.2, 2.0)});
		expect_true("an order on the worker is accepted",
					b.session.issue_attack(b.roster, raider.id(), b.who.id()));
		b.session.step(0.1, b.roster, b.world.env);
		expect_failed("an incoming attack order interrupts through the session",
					  b.who.work_status(), open::WorkFailure::Combat);
		expect_true("with the scrap spent and no output",
					stock(b.world, b.who.id(), kScrap) == 18 && backpack_size(b.who) == 0);
	}
	{
		// The fifteen-second race, explicitly: 14.9 elapsed, the order lands between steps,
		// and the frame that would have completed instead interrupts. Command order:
		// request ... (149 steps) ... issue_attack ... step - combat first, settlement never.
		AtTheBench b;
		open::Character raider = sheet(2, b.world.catalog, "Raider");
		b.roster.push_back(open::Combatant{&raider, at(1.2, 2.0)});
		for (int i = 0; i < 149; ++i) {
			b.session.step(0.1, b.roster, b.world.env);
		}
		expect_true("the bench is at fourteen point nine",
					close(static_cast<float>(b.who.work_status().elapsed), 14.9f, 0.001f));
		expect_true("and still running", b.who.work_status().active());
		expect_true("the order arrives on the final tick",
					b.session.issue_attack(b.roster, raider.id(), b.who.id()));
		b.session.step(0.1, b.roster, b.world.env);
		expect_failed("incoming combat wins at fifteen", b.who.work_status(),
					  open::WorkFailure::Combat);
		expect_true("the crossing frame delivered nothing", backpack_size(b.who) == 0);
	}
	{
		// The control for the race: the same clock with nobody attacking completes.
		AtTheBench b;
		for (int i = 0; i < 150; ++i) {
			b.session.step(0.1, b.roster, b.world.env);
		}
		expect_phase("without combat, fifteen completes", b.who.work_status(),
					 open::WorkPhase::Completed);
	}
	// Death on the bench.
	{
		AtTheBench b;
		expect_true("the smith dies at the anvil",
					b.who.damage(open::BodyPart::Torso, 1.0e30f) && !b.who.alive());
		expect_spent_failure("death interrupts", b, open::WorkFailure::Dead);
	}
	// The bench vanishes, at the bench.
	{
		AtTheBench b;
		b.world.env.benches.clear();
		b.session.step(0.1, b.roster, b.world.env);
		expect_spent_failure("losing the bench interrupts active work", b,
							 open::WorkFailure::BenchLost);
	}
	// A queued job clears free under bench loss and under incoming pursuit.
	{
		AtTheBench b;
		b.session.cancel_work(b.roster, b.who.id(), b.world.env);
		const std::uint64_t spent_by_the_cancelled_job = stock(b.world, b.who.id(), kScrap);
		slot_at(b.roster, b.who.id()).position = at(8.0, 0.0);
		expect_true("the next request is far from the bench, so it queues",
					b.session.request_work(b.roster, b.who.id(), craft_request(102), b.world.env)
						&& b.who.work_status().queued());
		b.world.env.benches.erase(
			std::remove_if(b.world.env.benches.begin(), b.world.env.benches.end(),
						   [](const open::WorkbenchObservation &o) { return o.id == kForge; }),
			b.world.env.benches.end());
		b.session.step(0.1, b.roster, b.world.env);
		expect_failed("a cleared queue fails free", b.who.work_status(),
					  open::WorkFailure::BenchLost);
		expect_true("with the stock exactly where the cancelled job left it",
					stock(b.world, b.who.id(), kScrap) == spent_by_the_cancelled_job);
	}
	{
		AtTheBench b;
		b.session.cancel_work(b.roster, b.who.id(), b.world.env);
		slot_at(b.roster, b.who.id()).position = at(8.0, 0.0);
		expect_true("queued again",
					b.session.request_work(b.roster, b.who.id(), craft_request(102), b.world.env));
		open::Character chaser = sheet(3, b.world.catalog, "Chaser");
		b.roster.push_back(open::Combatant{&chaser, at(20.0, 0.0)});
		expect_true("a pursuer is ordered from across the field",
					b.session.issue_attack(b.roster, chaser.id(), b.who.id()));
		b.session.step(0.1, b.roster, b.world.env);
		expect_failed("the queue clears free before any payment", b.who.work_status(),
					  open::WorkFailure::Combat);
		expect_true("nothing was ever paid for this job", stock(b.world, b.who.id(), kScrap) == 18);
	}
	// "Accepted attack then cancel before step": the first cause wins, explicitly.
	{
		AtTheBench b;
		open::Character victim = sheet(2, b.world.catalog, "Victim");
		b.roster.push_back(open::Combatant{&victim, at(2.0, 0.0)});
		expect_true("the attack is accepted",
					b.session.issue_attack(b.roster, b.who.id(), victim.id()));
		expect_true("and the later cancel finds nothing to cancel",
					!b.session.cancel_work(b.roster, b.who.id(), b.world.env));
		expect_failed("the kept reason is the combat, not the cancel", b.who.work_status(),
					  open::WorkFailure::Combat);
	}
}

// --- approach, cancel of a queue, and the frame's own discipline --------------------

void bench_clicks_and_frames() {
	World world;
	open::Character who = sheet(1, world.catalog);
	open::CombatSession session(0);
	auto roster = roster_of(who, at(9.0, 0.0));
	grant_kit(world, who.id());

	// A bench click without an action: approach only. No job, no payment, and a real walk.
	expect_true("approaching is accepted from far away",
				session.approach_workbench(roster, who.id(), kForge, world.env));
	expect_true("without starting work",
				!who.work_status().busy() && stock(world, who.id(), kScrap) == 20);
	expect_true("and the actor was told to walk", who.destination().has_value());
	slot_at(roster, who.id()).position = at(0.5, 0.0);
	expect_true("in reach, the same click opens without walking",
				session.approach_workbench(roster, who.id(), kForge, world.env)
					&& !who.work_status().busy() && stock(world, who.id(), kScrap) == 20);
	expect_true("an unknown bench cannot be walked to",
				!session.approach_workbench(roster, who.id(), 999, world.env));

	// Cancelling a queue is free and total.
	slot_at(roster, who.id()).position = at(9.0, 0.0);
	expect_true("a far request queues",
				session.request_work(roster, who.id(), craft_request(101), world.env)
					&& who.work_status().queued());
	expect_true("cancel clears the queue", session.cancel_work(roster, who.id(), world.env));
	expect_failed("and the kept reason says so", who.work_status(), open::WorkFailure::Cancelled);
	expect_true("without paying", stock(world, who.id(), kScrap) == 20);
	expect_true("cancelling nothing answers false",
				!session.cancel_work(roster, who.id(), world.env));
}

void frame_discipline() {
	// Invalid time and invalid rosters reject atomically, and zero seconds neither
	// progresses, starts, nor completes - though it does notice lost benches.
	World w;
	open::Character s = sheet(1, w.catalog);
	open::CombatSession f(0);
	auto r = roster_of(s, at(1.0, 0.0));
	grant_kit(w, s.id());
	expect_true("job started", f.request_work(r, s.id(), craft_request(101), w.env));
	const double elapsed = s.work_status().elapsed;
	expect_true("a negative frame is refused", !f.step(-0.1f, r, w.env));
	expect_true("and a NaN frame is refused",
				!f.step(std::numeric_limits<float>::quiet_NaN(), r, w.env));
	std::vector<open::Combatant> broken_roster;
	broken_roster.push_back(open::Combatant{nullptr, at(0.0, 0.0)});
	expect_true("a roster with a sheet that is nobody is refused", !f.step(0.1f, broken_roster, w.env));
	expect_true("with the job exactly where it was",
				close(static_cast<float>(s.work_status().elapsed), static_cast<float>(elapsed))
					&& s.work_status().active() && stock(w, s.id(), kScrap) == 18);
	expect_true("a zero frame reconciles but never progresses", f.step(0.0f, r, w.env));
	expect_true("the clock did not move",
				close(static_cast<float>(s.work_status().elapsed), static_cast<float>(elapsed)));
	w.env.benches.clear();
	expect_true("a zero frame does notice the bench is gone", f.step(0.0f, r, w.env));
	expect_failed("and interrupts on the spot", s.work_status(), open::WorkFailure::BenchLost);

	// A live job makes the old context-free frame refuse the whole step: no silent ticking.
	World plain;
	open::Character worker = sheet(2, plain.catalog);
	open::CombatSession g(0);
	auto pr = roster_of(worker, at(1.0, 0.0));
	grant_kit(plain, worker.id());
	expect_true("job started", g.request_work(pr, worker.id(), craft_request(101), plain.env));
	expect_true("the two-argument frame rejects while work lives", !g.step(0.1f, pr));
	expect_true("without ticking it",
				close(static_cast<float>(worker.work_status().elapsed), 0.0f)
					&& worker.work_status().active());
	expect_true("cancel ends it", g.cancel_work(pr, worker.id(), plain.env));
	expect_true("and with only terminal state kept, the old frame runs again", g.step(0.1f, pr));
}

// --- the work lock on the target -----------------------------------------------------

void work_locks_the_target() {
	World world;
	open::Character who = sheet(1, world.catalog);
	open::CombatSession session(0);
	auto roster = roster_of(who, at(1.0, 0.0));
	grant_kit(world, who.id(), 5);
	const open::ItemId blade = give(who, *world.catalog, kBladeA, 60);
	expect_true("the blade goes up", who.equip(blade, open::Slot::RightHand));
	// A fresh blade is at full durability and repair would refuse it, so wear it first.
	expect_true("and dents itself", who.apply_wear(blade, 30)
										&& who.instance(blade)->durability == 70u);
	expect_true("it is repaired",
				session.request_work(roster, who.id(), repair_request(blade), world.env));
	expect_true("it is active", who.work_status().active());
	expect_true("the dent was paid for at the start", stock(world, who.id(), kScrap) == 4);

	// Equipping or moving the target is refused while the job is on the bench - and the
	// refusal must not be an interruption: the job carries on as if nothing was asked.
	expect_true("the target cannot be re-equipped", !who.equip(blade, open::Slot::LeftHand));
	expect_true("nor taken off", !who.unequip(open::Slot::RightHand));
	session.step(1.0, roster, world.env);
	expect_true("and the job is one second further, not ended",
				who.work_status().active()
					&& close(static_cast<float>(who.work_status().elapsed), 1.0f));

	// Other items move freely.
	const open::ItemId other = give(who, *world.catalog, kBladeB, 61);
	expect_true("an unrelated blade still equips", who.equip(other, open::Slot::LeftHand));

	// While only queued, the target may still be moved: nothing is paid yet, and the
	// arrival check is what fails - the free-fail itself is pinned under materials.
	expect_true("cancel the repair", session.cancel_work(roster, who.id(), world.env));
	slot_at(roster, who.id()).position = at(9.0, 0.0);
	expect_true("a far repair queues",
				session.request_work(roster, who.id(), repair_request(blade), world.env)
					&& who.work_status().queued());
	expect_true("and while walking the worn blade is free to move",
				who.unequip(open::Slot::LeftHand) && who.unequip(open::Slot::RightHand)
					&& who.equip(blade, open::Slot::RightHand) && who.work_status().queued());
	expect_true("and a queued job is still cancellable, free",
				session.cancel_work(roster, who.id(), world.env)
					&& stock(world, who.id(), kScrap) == 4);
}

int main() {
	ledger_transactions();
	catalog_recipe_validation();
	starter_work_table();
	starter_setup_supplies_materials_once();
	request_shapes();
	craft_lifecycle();
	quality_caps();
	queue_and_arrival();
	active_interruptions();
	bench_clicks_and_frames();
	frame_discipline();
	work_locks_the_target();
	return test_summary();
}
