#include "open/item_instance.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/equipment.hpp"
#include "open/items.hpp"
#include "open/weapon_rules.hpp"
#include "test_support.hpp"

// Task 8 tests. Break these catches: an enchantment multiplying instead of adding,
// quality applied to the enchantment's blunt instead of the base, capacity that is
// not the quality tier, an applied value that drifts when the enchanter trains or
// the wielder's attribute moves, a requirement that overwrites the weapon's own
// instead of adding to it, an enchantment that can be removed or replaced, and a
// zero-durability discount applied to the base but not to the combination.
//
// Blunt Edge is the approved starter enchantment. The other requirement combinations
// below are fixtures for the arithmetic, not new production content.

namespace {

constexpr open::DefinitionId kSword = 101;
constexpr open::DefinitionId kShield = 105;
constexpr open::MaterialId kMaterial = 11;
constexpr open::EnchantmentId kBluntEdge = 1;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

open::DamageComponents parts(double cut, double pierce, double blunt) {
	open::DamageComponents value;
	value.cut = cut;
	value.pierce = pierce;
	value.blunt = blunt;
	return value;
}

bool same(const open::DamageComponents &got, double cut, double pierce, double blunt,
		  double tolerance = 1e-9) {
	return std::fabs(got.cut - cut) <= tolerance && std::fabs(got.pierce - pierce) <= tolerance &&
		   std::fabs(got.blunt - blunt) <= tolerance;
}

open::EnchantmentDefinition blunt_edge() {
	open::EnchantmentDefinition enchantment;
	enchantment.id = kBluntEdge;
	enchantment.model_key = "Blunt_Edge";
	enchantment.blunt_per_trained_rank = 0.01;
	enchantment.requirement = open::Attribute::Intelligence;
	enchantment.requirement_divisor = 20;
	return enchantment;
}

open::ItemCatalog catalog() {
	open::WeaponStats stats;
	stats.damage = parts(6.0, 2.0, 0.0);
	stats.min_reach = 0.0;
	stats.max_reach = 1.5;
	stats.speed = 1.0;
	stats.stamina_cost = 8.0;
	stats.block_percent = 25.0;
	stats.weight = 2.0;
	stats.required_trained_rank = 1;

	std::vector<open::ItemDefinition> items;

	open::ItemDefinition sword;
	sword.id = kSword;
	sword.model_key = "Sword";
	sword.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
	sword.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
	sword.hand_use = open::HandUse::OneHanded;
	open::WeaponDefinition weapon;
	weapon.job = {open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword};
	weapon.stats = stats;
	sword.weapon = weapon;
	items.push_back(sword);

	// A shield is not a weapon, so it has no enchantment slots.
	open::ItemDefinition shield;
	shield.id = kShield;
	shield.model_key = "Shield_Round";
	shield.allowed_slots.set(static_cast<std::size_t>(open::Slot::LeftHand));
	shield.allowed_slots.set(static_cast<std::size_t>(open::Slot::RightHand));
	shield.hand_use = open::HandUse::OneHanded;
	open::ShieldDefinition board;
	board.job = {open::BasicSkill::Blocking, open::AdvancedSkill::SmallShields};
	board.block_percent = 60.0;
	board.weight = 3.0;
	shield.shield = board;
	items.push_back(shield);

	open::MaterialDefinition material;
	material.id = kMaterial;
	material.max_durability_by_quality = {100, 100, 100, 100, 100, 100, 100};

	auto made = open::ItemCatalog::create(items, {material}, {blunt_edge()});
	if (!made.has_value()) {
		g_failed++;
	}
	return std::move(made).value();
}

open::ItemInstance owned(const open::ItemCatalog &made, open::ItemId id, unsigned quality,
						 open::DefinitionId definition = kSword) {
	auto instance = open::make_item_instance(made, definition, kMaterial, id, quality);
	if (!instance.has_value()) {
		g_failed++;
	}
	return std::move(instance).value();
}

// An applied enchantment written out by hand, for arithmetic the starter content
// does not produce. This is a fixture, not a new production enchantment.
open::AppliedEnchantment fixture(open::EnchantmentId definition, double added_blunt,
								 std::optional<open::Requirement> requirement = std::nullopt) {
	open::AppliedEnchantment applied;
	applied.definition = definition;
	applied.added_blunt = added_blunt;
	applied.requirement = requirement;
	return applied;
}

open::Character smith(int level = 1, open::Attribute blessing = open::Attribute::Strength) {
	open::CharacterConfig setup;
	setup.id = 1;
	setup.name = "Smith";
	setup.level = level;
	setup.blessing = blessing;
	auto made = open::Character::create(setup);
	return std::move(made).value();
}

void the_starter_formula_is_captured_once() {
	const open::EnchantmentDefinition edge = blunt_edge();

	// docs/weapons.md: added blunt is trained rank x 0.01, the added Intelligence
	// requirement is ceil(trained / 20), both fixed when the work is done.
	struct Row {
		unsigned trained;
		double blunt;
		unsigned intelligence;
	};
	const Row rows[] = {{1, 0.01, 1}, {20, 0.2, 1}, {21, 0.21, 2}, {200, 2.0, 10}};
	for (const Row &row : rows) {
		const std::optional<open::AppliedEnchantment> applied =
			open::make_applied_enchantment(edge, row.trained);
		expect_true("the row applies", applied.has_value());
		if (!applied) {
			continue;
		}
		expect_close("blunt added at that rank", applied->added_blunt, row.blunt, 1e-12);
		expect_true("the requirement is Intelligence",
					applied->requirement.has_value() &&
						applied->requirement->attribute == open::Attribute::Intelligence);
		if (applied->requirement.has_value()) {
			expect_close("Intelligence required", applied->requirement->value,
						 static_cast<double>(row.intelligence), 1e-12);
		}
		expect_true("and it names its own definition", applied->definition == kBluntEdge);
	}

	// Refusals: there is no rank below 1, and a definition with no effect is not an
	// enchantment.
	expect_true("rank 0 cannot enchant", !open::make_applied_enchantment(edge, 0).has_value());
	open::EnchantmentDefinition broken = blunt_edge();
	broken.requirement = std::nullopt;
	broken.requirement_divisor = 0;
	broken.blunt_per_trained_rank = 0.0;
	expect_true("an enchantment that does nothing is refused",
				!open::make_applied_enchantment(broken, 100).has_value());

	// The captured value belongs to the weapon, not to the smith who made it: more
	// training afterwards does not retro-fit old work.
	const open::ItemCatalog made = catalog();
	open::Equipment equipment;
	const std::optional<open::AppliedEnchantment> at_rank_1 = open::make_applied_enchantment(edge, 1);
	expect_true("the rank-1 application exists", at_rank_1.has_value());
	open::ItemInstance item = owned(made, 401, 7);
	equipment.add_instance(item, true);
	expect_true("and it is attached", equipment.attach_enchantment(401, *at_rank_1));
	const std::optional<open::AppliedEnchantment> at_rank_200 =
		open::make_applied_enchantment(edge, 200);
	expect_true("a rank-200 application also exists", at_rank_200.has_value());
	const std::optional<open::ItemInstance> kept = equipment.instance(401);
	expect_true("the attached weapon still reads rank 1's numbers",
				kept.has_value() && kept->enchantments.size() == 1 &&
					std::fabs(kept->enchantments[0].added_blunt - 0.01) <= 1e-12 &&
					kept->enchantments[0].requirement.has_value() &&
					kept->enchantments[0].requirement->value == 1.0);
}

void slots_are_the_quality_tier_and_nothing_is_ever_removed() {
	const open::ItemCatalog made = catalog();

	// Tier N has exactly N slots, and the N+1th application is refused.
	for (unsigned tier = 1; tier <= 7; ++tier) {
		open::Equipment equipment;
		equipment.add_instance(owned(made, 500 + tier, tier), true);
		bool filled = true;
		for (unsigned slot = 0; slot < tier; ++slot) {
			if (!equipment.attach_enchantment(500 + tier, fixture(kBluntEdge, 0.5))) {
				filled = false;
			}
		}
		expect_true("a tier fills exactly its own number of slots", filled);
		expect_true("and the next one is refused",
					!equipment.attach_enchantment(500 + tier, fixture(kBluntEdge, 0.5)));
		const std::optional<open::ItemInstance> item = equipment.instance(500 + tier);
		expect_true("with the tier count held, not more",
					item.has_value() && item->enchantments.size() == tier);
	}

	// A new item has no enchantments, and there is no way to take one off: an
	// attachment is permanent, never replaced, never expired.
	open::Equipment equipment;
	open::ItemInstance fresh = owned(made, 511, 3);
	expect_true("a crafted item starts clean", fresh.enchantments.empty());
	equipment.add_instance(fresh, true);
	expect_true("one is attached", equipment.attach_enchantment(511, fixture(kBluntEdge, 0.2)));
	expect_true("the next adds alongside it, replacing nothing",
				equipment.attach_enchantment(511, fixture(kBluntEdge, 0.4)));
	const std::optional<open::ItemInstance> stacked = equipment.instance(511);
	expect_true("both are still there, in order",
				stacked.has_value() && stacked->enchantments.size() == 2 &&
					std::fabs(stacked->enchantments[0].added_blunt - 0.2) <= 1e-12 &&
					std::fabs(stacked->enchantments[1].added_blunt - 0.4) <= 1e-12);

	// Only weapons have slots, and only described items have any.
	open::ItemInstance shield = owned(made, 512, 3, kShield);
	equipment.add_instance(shield, true);
	expect_true("a shield has no weapon enchantment slots",
				!equipment.attach_enchantment(512, fixture(kBluntEdge, 0.2)));
	open::ItemRef legacy;
	legacy.id = 513;
	legacy.allowed_slots.set(static_cast<std::size_t>(open::Slot::Backpack));
	equipment.add(legacy, true);
	expect_true("a placement-only record has nothing to attach to",
				!equipment.attach_enchantment(513, fixture(kBluntEdge, 0.2)));
	expect_true("and an unknown id has nothing either",
				!equipment.attach_enchantment(9999, fixture(kBluntEdge, 0.2)));

	// An incoherent attachment is refused without touching what is already fixed on
	// the item.
	open::Equipment guarded;
	guarded.add_instance(owned(made, 514, 3), true);
	expect_true("a good attachment lands", guarded.attach_enchantment(514, fixture(kBluntEdge, 0.2)));
	expect_true("a nameless one is refused", !guarded.attach_enchantment(514, fixture(0, 0.2)));
	expect_true("negative added damage is refused",
				!guarded.attach_enchantment(514, fixture(kBluntEdge, -0.2)));
	expect_true("non-finite added damage is refused",
				!guarded.attach_enchantment(514, fixture(kBluntEdge, kNan)));
	open::Requirement bad_value;
	bad_value.attribute = open::Attribute::Intelligence;
	bad_value.value = 0.0;
	expect_true("a zero requirement value is refused",
				!guarded.attach_enchantment(514, fixture(kBluntEdge, 0.2, bad_value)));
	open::Requirement bad_attribute;
	bad_attribute.attribute = static_cast<open::Attribute>(static_cast<int>(open::Attribute::Count));
	bad_attribute.value = 2.0;
	expect_true("a requirement naming no attribute is refused",
				!guarded.attach_enchantment(514, fixture(kBluntEdge, 0.2, bad_attribute)));
	const std::optional<open::ItemInstance> unharmed = guarded.instance(514);
	expect_true("and the item still holds exactly the one good attachment",
				unharmed.has_value() && unharmed->enchantments.size() == 1);
}

void attachments_survive_every_move_a_record_makes() {
	const open::ItemCatalog made = catalog();
	open::Equipment armory;
	armory.add_instance(owned(made, 521, 4), true);
	expect_true("a blunt edge is fixed on", armory.attach_enchantment(521, fixture(kBluntEdge, 0.25)));
	expect_true("and an Intelligence requirement with it",
				armory.attach_enchantment(
					521, fixture(kBluntEdge, 0.25, open::Requirement{open::Attribute::Intelligence, 2.0})));

	// Equipping and unequipping move the record, not its history.
	expect_true("worn", armory.equip(521, open::Slot::RightHand, true, true));
	const std::optional<open::ItemInstance> worn = armory.instance(521);
	expect_true("two attachments while worn",
				worn.has_value() && worn->enchantments.size() == 2 && worn->quality == 4);
	expect_true("stored again", armory.unequip(open::Slot::RightHand));
	const std::optional<open::ItemInstance> stored = armory.instance(521);
	expect_true("still two, still the same values",
				stored.has_value() && stored->enchantments.size() == 2 &&
					std::fabs(stored->enchantments[0].added_blunt - 0.25) <= 1e-12 &&
					stored->enchantments[1].requirement.has_value() &&
					stored->enchantments[1].requirement->value == 2.0);

	// Wear does not rub an enchantment off, and neither does running out.
	expect_true("worn to nothing", armory.apply_wear(521, 400));
	const std::optional<open::ItemInstance> broken = armory.instance(521);
	expect_true("both attachments remain and stay active",
				broken.has_value() && broken->enchantments.size() == 2 && broken->durability == 0);

	// A record read out is a copy, so a caller cannot edit history either.
	std::optional<open::ItemInstance> copy = armory.instance(521);
	copy->enchantments.clear();
	expect_true("clearing a copy changes nothing owned",
				armory.instance(521)->enchantments.size() == 2);

	// Ownership movement keeps them too: the same record handed to another carrier.
	open::Equipment other;
	const open::ItemInstance handed = *armory.instance(521);
	expect_true("the record is accepted by a new owner", other.add_instance(handed, true));
	expect_true("with both attachments and the same tier and wear",
				other.instance(521).has_value() && other.instance(521)->enchantments.size() == 2 &&
					other.instance(521)->quality == 4 && other.instance(521)->durability == 0);
}

void requirements_add_by_attribute_and_never_across() {
	const open::ItemCatalog made = catalog();

	// docs/weapons.md: two enchantments requiring 2 Intelligence each require 4
	// Intelligence in total; a Profanity requirement stays separate; the weapon's own
	// requirement of the same attribute is added to, not replaced.
	open::ItemInstance item = owned(made, 531, 3);
	item.enchantments.push_back(
		fixture(kBluntEdge, 0.0, open::Requirement{open::Attribute::Intelligence, 2.0}));
	item.enchantments.push_back(
		fixture(kBluntEdge, 0.0, open::Requirement{open::Attribute::Intelligence, 2.0}));
	item.enchantments.push_back(
		fixture(kBluntEdge, 0.0, open::Requirement{open::Attribute::Profanity, 3.0}));

	const std::vector<open::Requirement> aggregated = open::enchantment_requirements(item);
	expect_true("two attributes are named, no duplicates", aggregated.size() == 2);
	double intelligence = 0.0;
	double profanity = 0.0;
	for (const open::Requirement &need : aggregated) {
		if (need.attribute == open::Attribute::Intelligence) {
			intelligence = need.value;
		}
		if (need.attribute == open::Attribute::Profanity) {
			profanity = need.value;
		}
	}
	expect_close("the two Intelligence requirements add", intelligence, 4.0, 1e-12);
	expect_close("the Profanity requirement stays on its own", profanity, 3.0, 1e-12);

	// Nothing attached, nothing required.
	const std::vector<open::Requirement> empty =
		open::enchantment_requirements(owned(made, 532, 3));
	expect_true("a clean weapon adds no requirements", empty.empty());
}

void effects_add_and_quality_scales_only_the_base() {
	const open::ItemCatalog made = catalog();

	// The blunt the enchantment adds joins the base after quality has been applied:
	// tier 7 on 8 blunt is 800, plus 2 is 802, and NOT (8 + 2) x 100 = 1000.
	open::WeaponAttackProfile profile;
	profile.job = {open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword};
	profile.trained = 200;         // trained fraction 1
	profile.item = open::ItemId{601};
	profile.stats.damage = parts(0.0, 0.0, 8.0);
	profile.stats.max_reach = 1.5;
	profile.stats.speed = 1.0;
	profile.quality = 7;           // x100
	profile.durability = 100;
	profile.effectiveness = 1.0;
	profile.enchantments.push_back(fixture(kBluntEdge, 1.0));
	profile.enchantments.push_back(fixture(kBluntEdge, 1.0));

	const std::optional<open::DamageComponents> hit =
		open::scaled_offense(profile, parts(1.0, 1.0, 1.0), false, 1.0);
	expect_true("the combined blow resolves", hit.has_value());
	if (hit) {
		expect_close("8 x 100 base plus 1 plus 1, before any other factor", hit->blunt, 802.0, 1e-9);
		expect_true("and the other components are untouched by a blunt enchantment",
					same(*hit, 0.0, 0.0, 802.0));
	}

	// A technique that favours blunt multiplies the combined component, so the
	// enchantment rides along with the technique rather than being added afterwards.
	const std::optional<open::DamageComponents> heavy =
		open::scaled_offense(profile, parts(1.0, 1.0, 1.25), false, 1.0);
	expect_true("the heavy strike resolves", heavy.has_value());
	if (heavy) {
		expect_close("1.25 of 802", heavy->blunt, 802.0 * 1.25, 1e-9);
	}

	// Every other penalty still stacks on top of the combination, including at zero
	// durability, where the added blunt is discounted with the rest.
	const open::DamageComponents neutral = parts(1.0, 1.0, 1.0);
	open::WeaponAttackProfile ruined = profile;
	ruined.durability = 0;
	const std::optional<open::DamageComponents> discounted =
		open::scaled_offense(ruined, neutral, false, 1.0);
	expect_true("a ruined enchanted blade resolves", discounted.has_value());
	if (discounted) {
		expect_close("the whole combination is multiplied by 0.33", discounted->blunt, 802.0 * 0.33,
					 1e-9);
	}
	open::WeaponAttackProfile tired = profile;
	tired.effectiveness = 0.5;
	const std::optional<open::DamageComponents> both =
		open::scaled_offense(tired, neutral, true, 1.0);
	expect_true("exhaustion and a shortfall together resolve", both.has_value());
	if (both) {
		expect_close("802 x 0.5 x 0.5", both->blunt, 802.0 * 0.25, 1e-9);
	}

	// Cut and pierce are scaled by quality on their own, and a weapon with no
	// enchantments is unaffected by any of this.
	open::WeaponAttackProfile plain = profile;
	plain.enchantments.clear();
	plain.stats.damage = parts(6.0, 2.0, 0.0);
	const std::optional<open::DamageComponents> clean =
		open::scaled_offense(plain, neutral, false, 1.0);
	expect_true("the unenchanted blow resolves", clean.has_value());
	if (clean) {
		expect_true("quality only, exactly as before", same(*clean, 600.0, 200.0, 0.0));
	}
}

void the_wielder_pays_for_aggregated_requirements() {
	const open::ItemCatalog made = catalog();
	open::Character wielder = smith();

	open::WeaponStats stats;
	stats.damage = parts(6.0, 2.0, 0.0);
	stats.max_reach = 1.5;
	stats.speed = 1.0;
	stats.stamina_cost = 8.0;
	stats.required_trained_rank = 1;
	stats.requirements = {{open::Attribute::Intelligence, 1.0}};
	const open::SkillJob job{open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword};

	std::vector<open::AppliedEnchantment> two;
	two.push_back(fixture(kBluntEdge, 0.2, open::Requirement{open::Attribute::Intelligence, 2.0}));
	two.push_back(fixture(kBluntEdge, 0.2, open::Requirement{open::Attribute::Intelligence, 2.0}));

	// Base 1 plus two enchantments at 2 each: 5 Intelligence, checked as one total.
	// Effective Intelligence 5 meets it exactly; 2 of 5 is (0.4)^2.
	wielder.set_base(open::Attribute::Intelligence, 5.0f);
	const std::optional<double> met = open::weapon_effectiveness(wielder, stats, job, two);
	expect_true("the combined requirement is checkable", met.has_value());
	if (met) {
		expect_close("meeting the aggregate is whole", *met, 1.0, 1e-9);
	}
	open::Character weak = smith();
	weak.set_base(open::Attribute::Intelligence, 2.0f);
	const std::optional<double> short_of = open::weapon_effectiveness(weak, stats, job, two);
	expect_true("a shortfall against the aggregate is measured", short_of.has_value());
	if (short_of) {
		expect_close("two of five squared", *short_of, 0.16, 1e-9);
	}

	// A requirement on another attribute is its own check, and the worst one still
	// limits the swing: Profanity 2 of 3 is 0.444, worse than the Intelligence side.
	std::vector<open::AppliedEnchantment> mixed = two;
	mixed.push_back(fixture(kBluntEdge, 0.2, open::Requirement{open::Attribute::Profanity, 3.0}));
	open::Character dull = smith();
	dull.set_base(open::Attribute::Intelligence, 5.0f);
	dull.set_base(open::Attribute::Profanity, 2.0f);
	const std::optional<double> worst = open::weapon_effectiveness(dull, stats, job, mixed);
	expect_true("the second attribute is considered", worst.has_value());
	if (worst) {
		expect_close("the profanity shortfall limits it", *worst, 4.0 / 9.0, 1e-9);
	}

	// No attachments, no extra requirement: the weapon is judged on its own.
	const std::optional<double> clean = open::weapon_effectiveness(weak, stats, job, {});
	expect_true("an unenchanted weapon is judged alone", clean.has_value());
	if (clean) {
		expect_close("only the base Intelligence 1 applies", *clean, 1.0, 1e-9);
	}
}

void the_attack_profile_carries_the_blade_as_it_stands() {
	const std::shared_ptr<const open::ItemCatalog> made =
		std::make_shared<open::ItemCatalog>(catalog());
	open::CharacterConfig setup;
	setup.id = 7;
	setup.name = "Wielder";
	setup.level = 1;
	setup.blessing = open::Attribute::Strength;
	auto armed = open::Character::create(setup, made);
	if (!armed.has_value()) {
		g_failed++;
		return;
	}
	open::Character who = std::move(armed).value();
	who.set_base(open::Attribute::Dexterity, 5.0f);

	const open::ItemInstance blade = owned(*made, 541, 7);
	expect_true("the tier-7 blade is owned", who.add_instance(blade));
	expect_true("and worn", who.equip(541, open::Slot::RightHand));

	const std::optional<open::WeaponAttackProfile> before = open::weapon_attack_profile(who);
	expect_true("the profile starts clean",
				before.has_value() && before->enchantments.empty());
	expect_true("an edge is applied", who.attach_enchantment(541, *open::make_applied_enchantment(blunt_edge(), 200)));

	const std::optional<open::WeaponAttackProfile> after = open::weapon_attack_profile(who);
	expect_true("the profile now carries the fixed edge",
				after.has_value() && after->enchantments.size() == 1 &&
					std::fabs(after->enchantments[0].added_blunt - 2.0) <= 1e-12);
	expect_true("with the blade's own tier and durability",
				after.has_value() && after->quality == 7u && after->durability == 100u);
	expect_true("and its capacity is the tier, so six more fit and a seventh does not",
				[&who] {
					for (unsigned i = 0; i < 6; ++i) {
						if (!who.attach_enchantment(541, fixture(kBluntEdge, 0.1))) {
							return false;
						}
					}
					return !who.attach_enchantment(541, fixture(kBluntEdge, 0.1));
				}());
}

}  // namespace

int main() {
	the_starter_formula_is_captured_once();
	slots_are_the_quality_tier_and_nothing_is_ever_removed();
	attachments_survive_every_move_a_record_makes();
	requirements_add_by_attribute_and_never_across();
	effects_add_and_quality_scales_only_the_base();
	the_wielder_pays_for_aggregated_requirements();
	the_attack_profile_carries_the_blade_as_it_stands();
	return test_summary();
}
