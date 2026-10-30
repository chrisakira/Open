#include "open/weapon_rules.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "open/character.hpp"
#include "open/character_rules.hpp"
#include "open/equipment.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"
#include "test_support.hpp"

// Task 6 tests. Break these catches: prevention charged to armour instead of the
// defense, penetration reaching past armour into defense or Toughness, resistance
// going negative under penetration, a part reading two pieces at once, Feet
// armour mitigating something, the unarmed swing charged with a zero-durability
// penalty it cannot have, and a penalty applied twice by folding exhaustion into
// the minimum instead of stacking them.
//
// The oracle rows are hand-computed from docs/weapons.md, not from the code.

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

constexpr open::DefinitionId kHeadPiece = 201;
constexpr open::DefinitionId kChestPiece = 202;
constexpr open::DefinitionId kLegsPiece = 203;
constexpr open::DefinitionId kHandsPiece = 204;
constexpr open::DefinitionId kFeetPiece = 205;
constexpr open::DefinitionId kSwordDef = 101;
constexpr open::MaterialId kMaterial = 11;

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

// ---------------------------------------------------------------- the pipeline

void the_documented_oracle_row() {
	// incoming (10, 20, 30), block 50%, armour (50, 25, 100), penetration (20, 5, 0),
	// defender base Toughness 2.
	//   defense:  halve each            -> (5, 10, 15), prevented 30
	//   armour:   resistance less penetration, floored at 0 -> (30, 20, 100)%
	//             -> (5 x 0.7, 10 x 0.8, 15 x 0)            -> (3.5, 8, 0)
	//   toughness: / (1 + 0.05 x 2) = /1.1
	const std::optional<open::DamageResolution> hit = open::resolve_typed_damage(
		parts(10.0, 20.0, 30.0), 50.0, parts(50.0, 25.0, 100.0), parts(20.0, 5.0, 0.0), 2.0);
	expect_true("the row resolves", hit.has_value());
	if (!hit) {
		return;
	}
	expect_true("offensive is reported unchanged", same(hit->offensive, 10.0, 20.0, 30.0));
	expect_true("defense halves each component", same(hit->after_defense, 5.0, 10.0, 15.0));
	expect_close("prevention is what the defense took", hit->prevented, 30.0, 1e-9);
	expect_true("armour then reduces each by its own resistance", same(hit->after_armour, 3.5, 8.0, 0.0));
	expect_true("toughness divides every remaining component",
				same(hit->final, 3.5 / 1.1, 8.0 / 1.1, 0.0, 1e-9));
	expect_close("total is the sum after toughness", hit->total, 11.5 / 1.1, 1e-9);
	// The blunt component is reported separately from the total: a hit with no blunt
	// still has a total, and stagger reads the blunt alone.
	expect_close("final blunt is zero even though the total is not", hit->final.blunt, 0.0, 1e-12);
	expect_true("and the total is positive", hit->total > 0.0);
}

void prevention_is_the_defense_alone() {
	// Full armor must not move the credit for a good block: prevention is measured
	// before armour, even when armour would have eaten everything anyway.
	const std::optional<open::DamageResolution> soaked = open::resolve_typed_damage(
		parts(10.0, 20.0, 30.0), 50.0, parts(100.0, 100.0, 100.0), parts(0.0, 0.0, 0.0), 0.0);
	expect_true("it still resolves", soaked.has_value());
	if (soaked) {
		expect_close("prevention is still 30", soaked->prevented, 30.0, 1e-9);
		expect_true("nothing gets through full armour", same(soaked->after_armour, 0.0, 0.0, 0.0));
		expect_close("and the total is zero", soaked->total, 0.0, 1e-12);
	}

	// Zero offense: nothing prevented, nothing to draw for, nothing to soak.
	const std::optional<open::DamageResolution> none = open::resolve_typed_damage(
		parts(0.0, 0.0, 0.0), 50.0, parts(0.0, 0.0, 0.0), parts(0.0, 0.0, 0.0), 5.0);
	expect_true("zero offense resolves", none.has_value());
	if (none) {
		expect_close("no prevention", none->prevented, 0.0, 1e-12);
		expect_close("no total", none->total, 0.0, 1e-12);
	}

	// A 0% block is selected but prevents nothing, so it earns nothing.
	const std::optional<open::DamageResolution> open_block = open::resolve_typed_damage(
		parts(10.0, 20.0, 30.0), 0.0, parts(0.0, 0.0, 0.0), parts(0.0, 0.0, 0.0), 0.0);
	expect_true("a 0% block resolves", open_block.has_value());
	if (open_block) {
		expect_close("prevents nothing", open_block->prevented, 0.0, 1e-12);
		expect_true("everything arrives", same(open_block->after_defense, 10.0, 20.0, 30.0));
		expect_close("and the whole 60 lands with no armour or toughness", open_block->total, 60.0, 1e-9);
	}

	// A ready dodge turns all of it aside; an exhausted one halves it.
	const std::optional<open::DamageResolution> dodged = open::resolve_typed_damage(
		parts(10.0, 20.0, 30.0), 100.0, parts(0.0, 0.0, 0.0), parts(0.0, 0.0, 0.0), 0.0);
	expect_true("a dodge resolves", dodged.has_value());
	if (dodged) {
		expect_close("a ready dodge prevents everything", dodged->prevented, 60.0, 1e-9);
		expect_close("and lands nothing", dodged->total, 0.0, 1e-12);
	}
	const std::optional<open::DamageResolution> tired_dodge = open::resolve_typed_damage(
		parts(10.0, 20.0, 30.0), 50.0, parts(0.0, 0.0, 0.0), parts(0.0, 0.0, 0.0), 0.0);
	expect_true("an exhausted dodge halves", tired_dodge.has_value());
	if (tired_dodge) {
		expect_true("post-dodge components halved", same(tired_dodge->after_defense, 5.0, 10.0, 15.0));
		expect_close("prevention is the other half", tired_dodge->prevented, 30.0, 1e-9);
	}

	// Partial defense: some prevented and some arriving, so both sides can earn.
	const std::optional<open::DamageResolution> partial = open::resolve_typed_damage(
		parts(8.0, 0.0, 0.0), 25.0, parts(0.0, 0.0, 0.0), parts(0.0, 0.0, 0.0), 0.0);
	expect_true("a quarter block resolves", partial.has_value());
	if (partial) {
		expect_close("2 prevented", partial->prevented, 2.0, 1e-9);
		expect_close("6 arrive", partial->total, 6.0, 1e-9);
	}
}

void penetration_floors_resistance_and_touches_nothing_else() {
	// 20 points of cut penetration against 50% cut resistance leaves 30%.
	const std::optional<open::DamageResolution> pierced = open::resolve_typed_damage(
		parts(10.0, 0.0, 0.0), 0.0, parts(50.0, 0.0, 0.0), parts(20.0, 0.0, 0.0), 0.0);
	expect_true("penetration resolves", pierced.has_value());
	if (pierced) {
		expect_close("30% of the cut is stopped", pierced->after_armour.cut, 7.0, 1e-9);
	}

	// More penetration than resistance is zero resistance, not negative armour.
	const std::optional<open::DamageResolution> through = open::resolve_typed_damage(
		parts(10.0, 0.0, 0.0), 0.0, parts(20.0, 0.0, 0.0), parts(100.0, 0.0, 0.0), 0.0);
	expect_true("over-penetration resolves", through.has_value());
	if (through) {
		expect_close("all of it arrives", through->after_armour.cut, 10.0, 1e-9);
		expect_close("and none of it arrives twice", through->total, 10.0, 1e-9);
	}

	// Penetration never reaches the defense: the same block with and without it
	// post-defends identically.
	const std::optional<open::DamageResolution> with_pen = open::resolve_typed_damage(
		parts(10.0, 20.0, 30.0), 50.0, parts(0.0, 0.0, 0.0), parts(100.0, 100.0, 100.0), 0.0);
	const std::optional<open::DamageResolution> without_pen = open::resolve_typed_damage(
		parts(10.0, 20.0, 30.0), 50.0, parts(0.0, 0.0, 0.0), parts(0.0, 0.0, 0.0), 0.0);
	expect_true("both resolve", with_pen.has_value() && without_pen.has_value());
	if (with_pen && without_pen) {
		expect_true("the defense step is identical", same(with_pen->after_defense,
													 without_pen->after_defense.cut,
													 without_pen->after_defense.pierce,
													 without_pen->after_defense.blunt));
		expect_close("and prevention does not move", with_pen->prevented, without_pen->prevented, 1e-9);
	}

	// Nor does it reach Toughness: an unarmoured, fully penetrated target still soaks
	// with its base Toughness.
	const std::optional<open::DamageResolution> tough = open::resolve_typed_damage(
		parts(10.0, 0.0, 0.0), 0.0, parts(0.0, 0.0, 0.0), parts(100.0, 100.0, 100.0), 20.0);
	expect_true("the tough target resolves", tough.has_value());
	if (tough) {
		expect_close("1 / (1 + 0.05 x 20) of it lands", tough->total, 10.0 / 2.0, 1e-9);
	}

	// Unarmoured is not unmitigated: Toughness applies on its own.
	const std::optional<open::DamageResolution> naked = open::resolve_typed_damage(
		parts(10.0, 0.0, 0.0), 0.0, parts(0.0, 0.0, 0.0), parts(0.0, 0.0, 0.0), 2.0);
	expect_true("the naked target resolves", naked.has_value());
	if (naked) {
		expect_close("base Toughness 2 still soaks a tenth", naked->total, 10.0 / 1.1, 1e-9);
	}
}

void bad_numbers_are_refused_not_clamped() {
	const open::DamageComponents good = parts(10.0, 0.0, 0.0);
	const open::DamageComponents zero = parts(0.0, 0.0, 0.0);
	const open::DamageComponents percent = parts(50.0, 50.0, 50.0);

	expect_true("NaN mitigation is refused",
				!open::resolve_typed_damage(good, kNan, zero, zero, 0.0).has_value());
	expect_true("mitigation below 0 is refused",
				!open::resolve_typed_damage(good, -0.5, zero, zero, 0.0).has_value());
	expect_true("mitigation above 100 is refused",
				!open::resolve_typed_damage(good, 100.5, zero, zero, 0.0).has_value());
	expect_true("infinite mitigation is refused",
				!open::resolve_typed_damage(good, kInf, zero, zero, 0.0).has_value());
	expect_true("negative offence is refused",
				!open::resolve_typed_damage(parts(-1.0, 0.0, 0.0), 0.0, zero, zero, 0.0).has_value());
	expect_true("infinite offence is refused",
				!open::resolve_typed_damage(parts(kInf, 0.0, 0.0), 0.0, zero, zero, 0.0).has_value());
	expect_true("NaN resistance is refused",
				!open::resolve_typed_damage(good, 0.0, parts(kNan, 0.0, 0.0), zero, 0.0).has_value());
	expect_true("resistance above 100 is refused",
				!open::resolve_typed_damage(good, 0.0, parts(101.0, 0.0, 0.0), zero, 0.0).has_value());
	expect_true("negative penetration is refused",
				!open::resolve_typed_damage(good, 0.0, percent, parts(-1.0, 0.0, 0.0), 0.0).has_value());
	expect_true("penetration above 100 points is refused",
				!open::resolve_typed_damage(good, 0.0, percent, parts(0.0, 100.5, 0.0), 0.0).has_value());
	expect_true("negative Toughness is refused",
				!open::resolve_typed_damage(good, 0.0, zero, zero, -1.0).has_value());
	expect_true("NaN Toughness is refused", !open::resolve_typed_damage(good, 0.0, zero, zero, kNan).has_value());

	// An overflowing amount is refused rather than silently capped at some new
	// balance ceiling.
	// Three plausible components whose sum is not: refusing this is what keeps a
	// monster hit from being silently capped at some new balance ceiling.
	expect_true("an overflowing total is refused",
				!open::resolve_typed_damage(parts(1.0e308, 1.0e308, 1.0e308), 0.0, zero, zero, 0.0)
						 .has_value());
}

// ------------------------------------------------------------------ the armour

const open::ItemCatalog &armour_catalog() {
	static const std::shared_ptr<const open::ItemCatalog> made = [] {
		const open::Slot slots[5] = {open::Slot::Head,    open::Slot::Chest, open::Slot::Legs,
									 open::Slot::Hands, open::Slot::Feet};
		const open::DefinitionId ids[5] = {kHeadPiece, kChestPiece, kLegsPiece, kHandsPiece, kFeetPiece};
		const double resistances[5][3] = {{50.0, 0.0, 0.0},
										  {0.0, 25.0, 0.0},
										  {0.0, 0.0, 100.0},
										  {10.0, 10.0, 10.0},
										  {99.0, 99.0, 99.0}};
		std::vector<open::ItemDefinition> items;
		for (int i = 0; i < 5; ++i) {
			open::ItemDefinition piece;
			piece.id = ids[i];
			piece.model_key = "Armour_Fixture";
			piece.allowed_slots.set(static_cast<std::size_t>(slots[i]));
			open::ArmourDefinition armour;
			armour.resistance_percent = parts(resistances[i][0], resistances[i][1], resistances[i][2]);
			armour.weight = 1.0;
			piece.armour = armour;
			items.push_back(piece);
		}
		open::MaterialDefinition material;
		material.id = kMaterial;
		material.max_durability_by_quality = {100, 100, 100, 100, 100, 100, 100};
		auto built = open::ItemCatalog::create(std::move(items), {material});
		return std::make_shared<open::ItemCatalog>(std::move(built).value());
	}();
	return *made;
}

open::Character wearer(const open::ItemCatalog &made) {
	static std::uint64_t next_id = 1;
	open::CharacterConfig setup;
	setup.id = next_id++;
	setup.name = "Wearer";
	setup.level = 1;
	setup.blessing = open::Attribute::Strength;
	auto made_character = open::Character::create(setup, std::make_shared<open::ItemCatalog>(made));
	return std::move(made_character).value();
}

void wear_armour(open::Character &who, open::DefinitionId what, open::Slot where, open::ItemId which) {
	const open::MaterialId material = kMaterial;
	const std::optional<open::ItemInstance> instance =
		open::make_item_instance(*who.catalog(), what, material, which, 1);
	expect_true("the armour fixture is describable", instance.has_value());
	if (!instance) {
		return;
	}
	expect_true("and carried", who.add_instance(*instance));
	expect_true("and worn", who.equip(which, where));
}

void one_piece_per_part_and_feet_soak_nothing() {
	const open::ItemCatalog &made = armour_catalog();
	open::Character naked = wearer(made);

	expect_true("with nothing worn every part reads zero",
				same(open::armour_for_part(naked, open::BodyPart::Head), 0.0, 0.0, 0.0) &&
					same(open::armour_for_part(naked, open::BodyPart::Torso), 0.0, 0.0, 0.0) &&
					same(open::armour_for_part(naked, open::BodyPart::LeftArm), 0.0, 0.0, 0.0) &&
					same(open::armour_for_part(naked, open::BodyPart::RightLeg), 0.0, 0.0, 0.0));

	open::Character clad = wearer(made);
	wear_armour(clad, kHeadPiece, open::Slot::Head, 901);
	wear_armour(clad, kChestPiece, open::Slot::Chest, 902);
	wear_armour(clad, kLegsPiece, open::Slot::Legs, 903);
	wear_armour(clad, kHandsPiece, open::Slot::Hands, 904);
	wear_armour(clad, kFeetPiece, open::Slot::Feet, 905);

	// The locked routing: Head to Head, Torso to Chest, either arm to Hands, either
	// leg to Legs, and Feet to nothing at all.
	expect_true("head reads the head piece",
				same(open::armour_for_part(clad, open::BodyPart::Head), 50.0, 0.0, 0.0));
	expect_true("torso reads the chest",
				same(open::armour_for_part(clad, open::BodyPart::Torso), 0.0, 25.0, 0.0));
	expect_true("left arm reads the gloves",
				same(open::armour_for_part(clad, open::BodyPart::LeftArm), 10.0, 10.0, 10.0));
	expect_true("right arm reads the same gloves, once",
				same(open::armour_for_part(clad, open::BodyPart::RightArm), 10.0, 10.0, 10.0));
	expect_true("left leg reads the leggings",
				same(open::armour_for_part(clad, open::BodyPart::LeftLeg), 0.0, 0.0, 100.0));
	expect_true("right leg reads the same leggings",
				same(open::armour_for_part(clad, open::BodyPart::RightLeg), 0.0, 0.0, 100.0));
	// Feet armour is the one piece no hit reads: the six parts route to Head, Chest,
	// Hands and Legs, and no part routes to Slot::Feet. Proved on its own, with the
	// 99% piece the only thing worn.
	open::Character booted = wearer(made);
	wear_armour(booted, kFeetPiece, open::Slot::Feet, 909);
	const open::BodyPart routable[6] = {open::BodyPart::Head, open::BodyPart::Torso,
										open::BodyPart::LeftArm, open::BodyPart::RightArm,
										open::BodyPart::LeftLeg, open::BodyPart::RightLeg};
	bool feet_mitigates_anything = false;
	for (const open::BodyPart part : routable) {
		const open::DamageComponents read = open::armour_for_part(booted, part);
		if (read.cut != 0.0 || read.pierce != 0.0 || read.blunt != 0.0) {
			feet_mitigates_anything = true;
		}
	}
	expect_true("feet armour mitigates nothing while there is no foot location",
				!feet_mitigates_anything);
	expect_true("and a part that is not a part reads nothing rather than everything",
				same(open::armour_for_part(clad, open::BodyPart::Count), 0.0, 0.0, 0.0));

	// Pieces do not combine or overlap: the head has 50% cut and nothing else, even
	// though four other pieces are worn.
	const open::DamageComponents head = open::armour_for_part(clad, open::BodyPart::Head);
	expect_true("only one piece answers for the head",
				head.cut == 50.0 && head.pierce == 0.0 && head.blunt == 0.0);

	// Equipment is not multiplied by level or blessing: the same piece on a level 7
	// sheet is the same armour.
	open::Character veteran = wearer(made);
	veteran.set_level(7);
	veteran.set_base(open::Attribute::Constitution, 100.0f);
	wear_armour(veteran, kHeadPiece, open::Slot::Head, 906);
	expect_true("level and blessing do not enter the readout",
				same(open::armour_for_part(veteran, open::BodyPart::Head), 50.0, 0.0, 0.0));

	// Armour on a disabled limb stays on and still soaks: a dead arm blocks new use,
	// it does not strip gear.
	open::Character maimed = wearer(made);
	wear_armour(maimed, kHandsPiece, open::Slot::Hands, 907);
	const float arm_max = maimed.body_part(open::BodyPart::LeftArm).maximum;
	expect_true("the arm goes dead", maimed.damage(open::BodyPart::LeftArm, arm_max));
	expect_true("and the gloves still work",
				same(open::armour_for_part(maimed, open::BodyPart::LeftArm), 10.0, 10.0, 10.0));

	// A worn record the catalog cannot describe mitigates nothing.
	open::Character blind_sheet = wearer(made);
	open::ItemRef legacy;
	legacy.id = 908;
	legacy.allowed_slots.set(static_cast<std::size_t>(open::Slot::Chest));
	expect_true("the undefined piece is placeable", blind_sheet.add_item(legacy));
	expect_true("and wearable", blind_sheet.equip(908, open::Slot::Chest));
	expect_true("but it has no resistance to offer",
				same(open::armour_for_part(blind_sheet, open::BodyPart::Torso), 0.0, 0.0, 0.0));
}

// ------------------------------------------------------------- scaled offense

open::WeaponAttackProfile profile_for(open::DamageComponents damage, unsigned quality, unsigned trained,
									  double effectiveness, unsigned durability, bool held) {
	open::WeaponAttackProfile profile;
	profile.job = {open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword};
	profile.trained = trained;
	if (held) {
		profile.item = open::ItemId{401};
	}
	profile.stats.damage = damage;
	profile.stats.min_reach = 0.0;
	profile.stats.max_reach = 1.5;
	profile.stats.speed = 1.0;
	profile.quality = quality;
	profile.durability = durability;
	profile.effectiveness = effectiveness;
	return profile;
}

void every_penalty_stands_on_its_own() {
	const open::DamageComponents one = parts(1.0, 1.0, 1.0);
	const open::DamageComponents neutral = parts(1.0, 1.0, 1.0);

	// Baseline: tier 1, rank 200, fully effective, paid, at range, undamaged.
	const std::optional<open::DamageComponents> plain =
		open::scaled_offense(profile_for(one, 1, 200, 1.0, 100, true), neutral, false, 1.0);
	expect_true("the baseline resolves", plain.has_value());
	if (plain) {
		expect_true("nothing scales a perfect swing", same(*plain, 1.0, 1.0, 1.0));
	}

	// Quality multiplies damage components and nothing else; tier 7 is x100.
	const std::optional<open::DamageComponents> masterwork =
		open::scaled_offense(profile_for(one, 7, 200, 1.0, 100, true), neutral, false, 1.0);
	expect_true("the top tier resolves", masterwork.has_value());
	if (masterwork) {
		expect_true("tier 7 is one hundred times tier 1", same(*masterwork, 100.0, 100.0, 100.0));
	}

	// Each penalty on its own.
	const std::optional<open::DamageComponents> half_skill =
		open::scaled_offense(profile_for(one, 1, 100, 1.0, 100, true), neutral, false, 1.0);
	expect_true("trained 100 of 200", half_skill.has_value() && same(*half_skill, 0.5, 0.5, 0.5));
	const std::optional<open::DamageComponents> half_effect =
		open::scaled_offense(profile_for(one, 1, 200, 0.5, 100, true), neutral, false, 1.0);
	expect_true("half effective", half_effect.has_value() && same(*half_effect, 0.5, 0.5, 0.5));
	const std::optional<open::DamageComponents> tired =
		open::scaled_offense(profile_for(one, 1, 200, 1.0, 100, true), neutral, true, 1.0);
	expect_true("exhausted", tired.has_value() && same(*tired, 0.5, 0.5, 0.5));
	const std::optional<open::DamageComponents> too_close =
		open::scaled_offense(profile_for(one, 1, 200, 1.0, 100, true), neutral, false, 0.5);
	expect_true("inside the minimum", too_close.has_value() && same(*too_close, 0.5, 0.5, 0.5));
	const std::optional<open::DamageComponents> broken =
		open::scaled_offense(profile_for(one, 1, 200, 1.0, 0, true), neutral, false, 1.0);
	expect_true("zero durability", broken.has_value() && same(*broken, 0.33, 0.33, 0.33));
	const std::optional<open::DamageComponents> scratched =
		open::scaled_offense(profile_for(one, 1, 200, 1.0, 1, true), neutral, false, 1.0);
	expect_true("durability 1 costs nothing",
				scratched.has_value() && same(*scratched, 1.0, 1.0, 1.0));

	// And all of them at once, multiplied: 1 x 1 (quality 1) x 0.5 (trained 100)
	// x 0.5 (effectiveness) x 0.5 (exhausted) x 0.5 (inside minimum) x 0.33 (zero
	// durability) = 0.020625.
	const std::optional<open::DamageComponents> stacked = open::scaled_offense(
		profile_for(one, 1, 100, 0.5, 0, true), neutral, true, 0.5);
	expect_true("the stack resolves", stacked.has_value());
	if (stacked) {
		expect_close("every penalty applies separately", stacked->cut, 0.5 * 0.5 * 0.5 * 0.5 * 0.33, 1e-12);
	}

	// The same stack with a masterwork blade: quality multiplies the base first.
	const std::optional<open::DamageComponents> stacked_masterwork = open::scaled_offense(
		profile_for(one, 7, 100, 0.5, 0, true), neutral, true, 0.5);
	expect_true("the masterwork stack resolves", stacked_masterwork.has_value());
	if (stacked_masterwork) {
		expect_close("quality 100 multiplies the whole stack",
					 stacked_masterwork->cut, 100.0 * 0.5 * 0.5 * 0.5 * 0.5 * 0.33, 1e-10);
	}

	// Technique factors hit the matching component only, and a zero component stays
	// zero however generous the factor.
	const std::optional<open::DamageComponents> slash = open::scaled_offense(
		profile_for(parts(6.0, 2.0, 0.0), 1, 200, 1.0, 100, true), parts(1.25, 1.0, 1.0), false, 1.0);
	expect_true("the slash resolves", slash.has_value());
	if (slash) {
		expect_true("cut favoured, pierce untouched, blunt still zero",
					same(*slash, 7.5, 2.0, 0.0));
	}
	const std::optional<open::DamageComponents> heavy = open::scaled_offense(
		profile_for(parts(6.0, 2.0, 0.0), 1, 200, 1.0, 100, true), parts(1.0, 1.0, 1.25), false, 1.0);
	expect_true("the heavy strike resolves", heavy.has_value());
	if (heavy) {
		expect_true("a zero blunt component does not become positive", same(*heavy, 6.0, 2.0, 0.0));
	}

	// The unarmed fallback has no durability of its own, so the zero-durability
	// penalty never applies to it: no item means nothing to wear out.
	open::WeaponAttackProfile bare = profile_for(parts(0.0, 0.0, 8.0), 1, 200, 1.0, 0, false);
	const std::optional<open::DamageComponents> punch =
		open::scaled_offense(bare, neutral, false, 1.0);
	expect_true("the punch resolves", punch.has_value());
	if (punch) {
		expect_true("and is not discounted for a weapon it does not have",
					same(*punch, 0.0, 0.0, 8.0));
	}

	// Refusals.
	open::WeaponAttackProfile nonsense = profile_for(one, 1, 200, 1.0, 100, true);
	nonsense.effectiveness = kNan;
	expect_true("a NaN effectiveness is refused",
				!open::scaled_offense(nonsense, neutral, false, 1.0).has_value());
	nonsense = profile_for(one, 1, 200, 1.0, 100, true);
	nonsense.quality = 9;
	expect_true("a tier outside the table is refused",
				!open::scaled_offense(nonsense, neutral, false, 1.0).has_value());
	expect_true("a negative technique factor is refused",
				!open::scaled_offense(profile_for(one, 1, 200, 1.0, 100, true), parts(-1.0, 1.0, 1.0),
									false, 1.0)
						 .has_value());
	expect_true("a NaN technique factor is refused",
				!open::scaled_offense(profile_for(one, 1, 200, 1.0, 100, true), parts(kNan, 1.0, 1.0),
									false, 1.0)
						 .has_value());
	expect_true("a factor outside (0, 1] is refused",
				!open::scaled_offense(profile_for(one, 1, 200, 1.0, 100, true), neutral, false, 0.0)
					 .has_value() &&
					!open::scaled_offense(profile_for(one, 1, 200, 1.0, 100, true), neutral, false, 1.5)
						 .has_value());
	expect_true("and an overflow is refused rather than capped",
				!open::scaled_offense(profile_for(parts(1.0e308, 0.0, 0.0), 7, 200, 1.0, 100, true),
									neutral, false, 1.0)
						 .has_value());
}

}  // namespace

int main() {
	the_documented_oracle_row();
	prevention_is_the_defense_alone();
	penetration_floors_resistance_and_touches_nothing_else();
	bad_numbers_are_refused_not_clamped();
	one_piece_per_part_and_feet_soak_nothing();
	every_penalty_stands_on_its_own();
	return test_summary();
}
