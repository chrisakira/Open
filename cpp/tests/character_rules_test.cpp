#include "open/character_rules.hpp"
#include "open/move_order.hpp"
#include "test_support.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>

// Task 1 catalog tests. Break these catches: a renamed or reordered enum row,
// a skill linked to the wrong attribute, an advanced option attached to the
// wrong parent, a missing or duplicated table entry, a wrong level multiplier,
// or an unchecked ID that would index outside a fixed array.

namespace {

struct ExpectedBasic {
	open::BasicSkill basic;
	const char *name;
	std::array<open::Attribute, 2> links;
	int link_count;
};

// Order is the enum order; the test pins the ordinals.
constexpr ExpectedBasic kExpectedBasics[] = {
	{open::BasicSkill::OneHandedWeapons, "One-Handed Weapons", {open::Attribute::Strength, open::Attribute::Dexterity}, 2},
	{open::BasicSkill::TwoHandedWeapons, "Two-Handed Weapons", {open::Attribute::Strength, open::Attribute::Toughness}, 2},
	{open::BasicSkill::Polearms, "Polearms", {open::Attribute::Strength, open::Attribute::Dexterity}, 2},
	{open::BasicSkill::UnarmedCombat, "Unarmed Combat", {open::Attribute::Strength, open::Attribute::Agility}, 2},
	{open::BasicSkill::Archery, "Archery", {open::Attribute::Dexterity, open::Attribute::Perception}, 2},
	{open::BasicSkill::Blocking, "Blocking", {open::Attribute::Strength, open::Attribute::Toughness}, 2},
	{open::BasicSkill::Dodging, "Dodging", {open::Attribute::Agility, open::Attribute::Agility}, 1},
	{open::BasicSkill::Athletics, "Athletics", {open::Attribute::Agility, open::Attribute::Constitution}, 2},
	{open::BasicSkill::Acrobatics, "Acrobatics", {open::Attribute::Agility, open::Attribute::Dexterity}, 2},
	{open::BasicSkill::Swimming, "Swimming", {open::Attribute::Strength, open::Attribute::Constitution}, 2},
	{open::BasicSkill::Endurance, "Endurance", {open::Attribute::Constitution, open::Attribute::Constitution}, 1},
	{open::BasicSkill::Hunting, "Hunting", {open::Attribute::Perception, open::Attribute::Luck}, 2},
	{open::BasicSkill::Foraging, "Foraging", {open::Attribute::Perception, open::Attribute::Intelligence}, 2},
	{open::BasicSkill::Medicine, "Medicine", {open::Attribute::Intelligence, open::Attribute::Dexterity}, 2},
	{open::BasicSkill::AnimalHandling, "Animal Handling", {open::Attribute::Charisma, open::Attribute::Perception}, 2},
	{open::BasicSkill::Smithing, "Smithing", {open::Attribute::Strength, open::Attribute::Dexterity}, 2},
	{open::BasicSkill::Construction, "Construction", {open::Attribute::Strength, open::Attribute::Intelligence}, 2},
	{open::BasicSkill::Cooking, "Cooking", {open::Attribute::Dexterity, open::Attribute::Intelligence}, 2},
	{open::BasicSkill::Alchemy, "Alchemy", {open::Attribute::Intelligence, open::Attribute::Profanity}, 2},
	{open::BasicSkill::Research, "Research", {open::Attribute::Intelligence, open::Attribute::Profanity}, 2},
	{open::BasicSkill::Mining, "Mining", {open::Attribute::Strength, open::Attribute::Constitution}, 2},
	{open::BasicSkill::Farming, "Farming", {open::Attribute::Constitution, open::Attribute::Intelligence}, 2},
	{open::BasicSkill::Persuasion, "Persuasion", {open::Attribute::Charisma, open::Attribute::Charisma}, 1},
	{open::BasicSkill::Intimidation, "Intimidation", {open::Attribute::Charisma, open::Attribute::Strength}, 2},
	{open::BasicSkill::Trading, "Trading", {open::Attribute::Charisma, open::Attribute::Intelligence}, 2},
	{open::BasicSkill::Leadership, "Leadership", {open::Attribute::Charisma, open::Attribute::Charisma}, 1},
	{open::BasicSkill::Spellcasting, "Spellcasting", {open::Attribute::Intelligence, open::Attribute::Profanity}, 2},
	{open::BasicSkill::CastingSpeed, "Casting Speed", {open::Attribute::Agility, open::Attribute::Profanity}, 2},
	{open::BasicSkill::ManaControl, "Mana Control", {open::Attribute::Constitution, open::Attribute::Profanity}, 2},
	{open::BasicSkill::ElementalMagic, "Elemental Magic", {open::Attribute::Profanity, open::Attribute::Profanity}, 1},
	{open::BasicSkill::Restoration, "Restoration", {open::Attribute::Intelligence, open::Attribute::Profanity}, 2},
	{open::BasicSkill::Enchanting, "Enchanting", {open::Attribute::Intelligence, open::Attribute::Dexterity}, 2},
	{open::BasicSkill::Lockpicking, "Lockpicking", {open::Attribute::Dexterity, open::Attribute::Perception}, 2},
	{open::BasicSkill::Thievery, "Thievery", {open::Attribute::Dexterity, open::Attribute::Luck}, 2},
	{open::BasicSkill::Stealth, "Stealth", {open::Attribute::Agility, open::Attribute::Perception}, 2},
};

struct ExpectedAdvanced {
	open::AdvancedSkill advanced;
	open::BasicSkill parent;
	const char *name;
};

// Order is the enum order, grouped by parent in the spec's table order.
constexpr ExpectedAdvanced kExpectedAdvanced[] = {
	{open::AdvancedSkill::Katana, open::BasicSkill::OneHandedWeapons, "Katana"},
	{open::AdvancedSkill::Sword, open::BasicSkill::OneHandedWeapons, "Sword"},
	{open::AdvancedSkill::Axe, open::BasicSkill::OneHandedWeapons, "Axe"},
	{open::AdvancedSkill::Mace, open::BasicSkill::OneHandedWeapons, "Mace"},
	{open::AdvancedSkill::Dagger, open::BasicSkill::OneHandedWeapons, "Dagger"},

	{open::AdvancedSkill::Greatsword, open::BasicSkill::TwoHandedWeapons, "Greatsword"},
	{open::AdvancedSkill::GreatAxe, open::BasicSkill::TwoHandedWeapons, "GreatAxe"},
	{open::AdvancedSkill::ColosalSword, open::BasicSkill::TwoHandedWeapons, "ColosalSword"},
	{open::AdvancedSkill::Warhammer, open::BasicSkill::TwoHandedWeapons, "Warhammer"},

	{open::AdvancedSkill::Spear, open::BasicSkill::Polearms, "Spear"},
	{open::AdvancedSkill::Halberd, open::BasicSkill::Polearms, "Halberd"},

	{open::AdvancedSkill::Boxing, open::BasicSkill::UnarmedCombat, "Boxing"},
	{open::AdvancedSkill::Taekwondo, open::BasicSkill::UnarmedCombat, "Taekwondo"},

	{open::AdvancedSkill::Bow, open::BasicSkill::Archery, "Bow"},
	{open::AdvancedSkill::Crossbow, open::BasicSkill::Archery, "Crossbow"},

	{open::AdvancedSkill::SmallShields, open::BasicSkill::Blocking, "Small Shields"},
	{open::AdvancedSkill::BigShields, open::BasicSkill::Blocking, "Big Shields"},
	{open::AdvancedSkill::WallShields, open::BasicSkill::Blocking, "Wall Shields"},

	{open::AdvancedSkill::Footwork, open::BasicSkill::Dodging, "Footwork"},
	{open::AdvancedSkill::AcrobatEvasion, open::BasicSkill::Dodging, "Acrobat Evasion"},

	{open::AdvancedSkill::Sprinting, open::BasicSkill::Athletics, "Sprinting"},
	{open::AdvancedSkill::DistanceRunning, open::BasicSkill::Athletics, "Distance Running"},

	{open::AdvancedSkill::Climbing, open::BasicSkill::Acrobatics, "Climbing"},
	{open::AdvancedSkill::Parkour, open::BasicSkill::Acrobatics, "Parkour"},

	{open::AdvancedSkill::SpeedSwimming, open::BasicSkill::Swimming, "Speed Swimming"},

	{open::AdvancedSkill::BigGameHunting, open::BasicSkill::Hunting, "Big Game Hunting"},
	{open::AdvancedSkill::Trapping, open::BasicSkill::Hunting, "Trapping"},

	{open::AdvancedSkill::Herbalism, open::BasicSkill::Foraging, "Herbalism"},
	{open::AdvancedSkill::WildProvisioning, open::BasicSkill::Foraging, "Wild Provisioning"},

	{open::AdvancedSkill::Surgery, open::BasicSkill::Medicine, "Surgery"},
	{open::AdvancedSkill::FieldAid, open::BasicSkill::Medicine, "Field aid"},

	{open::AdvancedSkill::Mounts, open::BasicSkill::AnimalHandling, "Mounts"},
	{open::AdvancedSkill::Beasts, open::BasicSkill::AnimalHandling, "Beasts"},

	{open::AdvancedSkill::Weaponsmith, open::BasicSkill::Smithing, "Weaponsmith"},
	{open::AdvancedSkill::Armoursmith, open::BasicSkill::Smithing, "Armoursmith"},

	{open::AdvancedSkill::Fortification, open::BasicSkill::Construction, "Fortification"},
	{open::AdvancedSkill::CivilEngineering, open::BasicSkill::Construction, "Civil Engineering"},

	{open::AdvancedSkill::FieldCooking, open::BasicSkill::Cooking, "Field Cooking"},
	{open::AdvancedSkill::GourmetCooking, open::BasicSkill::Cooking, "Gourmet Cooking"},

	{open::AdvancedSkill::Potions, open::BasicSkill::Alchemy, "Potions"},
	{open::AdvancedSkill::Poisons, open::BasicSkill::Alchemy, "Poisons"},

	{open::AdvancedSkill::Engineering, open::BasicSkill::Research, "Engineering"},
	{open::AdvancedSkill::ArcaneResearch, open::BasicSkill::Research, "Arcane Research"},

	{open::AdvancedSkill::OreExtraction, open::BasicSkill::Mining, "Ore Extraction"},
	{open::AdvancedSkill::Quarrying, open::BasicSkill::Mining, "Quarrying"},

	{open::AdvancedSkill::Agriculture, open::BasicSkill::Farming, "Agriculture"},
	{open::AdvancedSkill::Horticulture, open::BasicSkill::Farming, "Horticulture"},

	{open::AdvancedSkill::Diplomacy, open::BasicSkill::Persuasion, "Diplomacy"},

	{open::AdvancedSkill::Terror, open::BasicSkill::Intimidation, "Terror"},

	{open::AdvancedSkill::Haggling, open::BasicSkill::Trading, "Haggling"},

	{open::AdvancedSkill::BattlefieldCommand, open::BasicSkill::Leadership, "Battlefield Command"},
	{open::AdvancedSkill::Governance, open::BasicSkill::Leadership, "Governance"},

	{open::AdvancedSkill::RitualCasting, open::BasicSkill::Spellcasting, "Ritual Casting"},
	{open::AdvancedSkill::CombatCasting, open::BasicSkill::Spellcasting, "Combat Casting"},

	{open::AdvancedSkill::ReactionCasting, open::BasicSkill::CastingSpeed, "Reaction Casting"},
	{open::AdvancedSkill::SpellChaining, open::BasicSkill::CastingSpeed, "Spell Chaining"},

	{open::AdvancedSkill::ManaConservation, open::BasicSkill::ManaControl, "Mana Conservation"},
	{open::AdvancedSkill::Channeling, open::BasicSkill::ManaControl, "Channeling"},

	{open::AdvancedSkill::Fire, open::BasicSkill::ElementalMagic, "Fire"},
	{open::AdvancedSkill::Ice, open::BasicSkill::ElementalMagic, "Ice"},
	{open::AdvancedSkill::Lightning, open::BasicSkill::ElementalMagic, "Lightning"},
	{open::AdvancedSkill::Earth, open::BasicSkill::ElementalMagic, "Earth"},
	{open::AdvancedSkill::Light, open::BasicSkill::ElementalMagic, "Light"},
	{open::AdvancedSkill::Dark, open::BasicSkill::ElementalMagic, "Dark"},

	{open::AdvancedSkill::Healing, open::BasicSkill::Restoration, "Healing"},
	{open::AdvancedSkill::Wards, open::BasicSkill::Restoration, "Wards"},

	{open::AdvancedSkill::WeaponEnchant, open::BasicSkill::Enchanting, "Weapon enchant"},
	{open::AdvancedSkill::ArmourEnchant, open::BasicSkill::Enchanting, "Armour enchant"},

	{open::AdvancedSkill::Pickpocket, open::BasicSkill::Thievery, "Pickpocket"},
	{open::AdvancedSkill::Burglary, open::BasicSkill::Thievery, "Burglary"},

	{open::AdvancedSkill::Infiltration, open::BasicSkill::Stealth, "Infiltration"},
	{open::AdvancedSkill::Assassination, open::BasicSkill::Stealth, "Assassination"},
};

struct ExpectedTitle {
	open::Title title;
	const char *name;
	unsigned threshold;
	open::BasicSkill skill;
	bool has_skill;
};

constexpr ExpectedTitle kExpectedTitles[] = {
	{open::Title::Dragonslayer, "Dragonslayer", 100, open::BasicSkill::OneHandedWeapons, false},
	{open::Title::Duelist, "Duelist", 100, open::BasicSkill::OneHandedWeapons, true},
	{open::Title::Breaker, "Breaker", 100, open::BasicSkill::TwoHandedWeapons, true},
	{open::Title::Wall, "Wall", 200, open::BasicSkill::Blocking, true},
	{open::Title::Ghost, "Ghost", 100, open::BasicSkill::Stealth, true},
	{open::Title::Huntsman, "Huntsman", 100, open::BasicSkill::Hunting, true},
	{open::Title::Sawbones, "Sawbones", 50, open::BasicSkill::Medicine, true},
	{open::Title::Hammerhand, "Hammerhand", 100, open::BasicSkill::Smithing, true},
	{open::Title::Mason, "Mason", 10, open::BasicSkill::Construction, true},
	{open::Title::GreenThumb, "Green Thumb", 100, open::BasicSkill::Farming, true},
	{open::Title::Hexer, "Hexer", 100, open::BasicSkill::Spellcasting, true},
	{open::Title::Cutpurse, "Cutpurse", 50, open::BasicSkill::Thievery, true},
	{open::Title::SilverTongue, "Silver Tongue", 50, open::BasicSkill::Persuasion, true},
	{open::Title::IronBack, "Iron Back", 100, open::BasicSkill::Endurance, true},
};

constexpr struct {
	open::Attribute attribute;
	const char *god;
	const char *blessing;
} kExpectedBlessings[] = {
	{open::Attribute::Strength, "Korr", "Iron"},
	{open::Attribute::Dexterity, "Sile", "Sure Hand"},
	{open::Attribute::Agility, "Ryn", "Wind"},
	{open::Attribute::Toughness, "Groth", "Stone"},
	{open::Attribute::Constitution, "Vel", "Blood"},
	{open::Attribute::Perception, "Isk", "Sight"},
	{open::Attribute::Intelligence, "Quen", "Lore"},
	{open::Attribute::Charisma, "Ael", "Voice"},
	{open::Attribute::Luck, "Nyx", "Fortune"},
	{open::Attribute::Profanity, "Vex", "Blasphemy"},
};

constexpr float kExpectedMultipliers[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 100.0f};

template <class Enum>
void expect_enum_bounds(const char *name, int count, int valid_hi) {
	expect_true(name, count == valid_hi);
	for (int i = 0; i < count; ++i) {
		expect_true("enum row is valid", valid(static_cast<Enum>(i)));
	}
	expect_true("one past last row is invalid", !valid(static_cast<Enum>(count)));
	expect_true("negative id is invalid", !valid(static_cast<Enum>(-1)));
	expect_true("far out of range id is invalid", !valid(static_cast<Enum>(1000000)));
}

bool name_is_unique(const std::vector<std::string> &names, const std::string &candidate) {
	for (const std::string &existing : names) {
		if (existing == candidate) {
			return false;
		}
	}
	return true;
}

}  // namespace

int main() {
	{
		// Break this catches: an added, removed or duplicated enum row.
		expect_true("ten attributes", static_cast<int>(open::Attribute::Count) == 10);
		expect_true("thirty-five basic skills", static_cast<int>(open::BasicSkill::Count) == 35);
		expect_true("seventy-two advanced options", static_cast<int>(open::AdvancedSkill::Count) == 72);
		expect_true("fourteen titles", static_cast<int>(open::Title::Count) == 14);
		expect_true("six body parts", static_cast<int>(open::BodyPart::Count) == 6);
		expect_true("three pools", static_cast<int>(open::PoolKind::Count) == 3);
		expect_true("twenty slots", static_cast<int>(open::Slot::Count) == 20);

		expect_true("attribute ordinals match the spec order",
					static_cast<int>(open::Attribute::Profanity) == 9);
		expect_true("body part ordinals match the spec order",
					static_cast<int>(open::BodyPart::RightLeg) == 5);
		expect_true("pool ordinals match the spec order", static_cast<int>(open::PoolKind::Mana) == 2);
		expect_true("slot ordinals match the spec order", static_cast<int>(open::Slot::Collar) == 19);
		expect_true("gloves are armour, not a held slot",
					static_cast<int>(open::Slot::Hands) < static_cast<int>(open::Slot::LeftHand));

		// Break this catches: a faction value that drifts from the bridge's 0/1.
		expect_true("friendly faction is zero", static_cast<int>(open::Faction::Friendly) == 0);
		expect_true("enemy faction is one", static_cast<int>(open::Faction::Enemy) == 1);
	}

	{
		// Break this catches: a checked getter indexing outside its fixed array.
		expect_enum_bounds<open::Attribute>("attribute bounds", static_cast<int>(open::Attribute::Count), 10);
		expect_enum_bounds<open::BasicSkill>("basic bounds", static_cast<int>(open::BasicSkill::Count), 35);
		expect_enum_bounds<open::AdvancedSkill>("advanced bounds",
												static_cast<int>(open::AdvancedSkill::Count), 72);
		expect_enum_bounds<open::Title>("title bounds", static_cast<int>(open::Title::Count), 14);
		expect_enum_bounds<open::BodyPart>("body bounds", static_cast<int>(open::BodyPart::Count), 6);
		expect_enum_bounds<open::PoolKind>("pool bounds", static_cast<int>(open::PoolKind::Count), 3);
		expect_enum_bounds<open::Slot>("slot bounds", static_cast<int>(open::Slot::Count), 20);
	}

	{
		// Break this catches: a wrong level multiplier or a level that is accepted
		// outside 1..7.
		for (int level = 1; level <= 7; ++level) {
			expect_true("level is valid", open::valid(level));
			expect_close("level multiplier", open::level_multiplier(level),
						 kExpectedMultipliers[static_cast<std::size_t>(level - 1)]);
		}
		for (int level : {0, -1, 8, 100}) {
			expect_true("level outside 1..7 is invalid", !open::valid(level));
			expect_throws_invalid("level multiplier rejects an out-of-range level",
								  [level] { open::level_multiplier(level); });
		}
	}

	{
		// Break this catches: a basic skill row out of order, a wrong link, or a
		// wrong display name.
		std::vector<std::string> names;
		std::array<int, 10> attribute_use{};
		int basic_rows = 0;
		for (const ExpectedBasic &expected : kExpectedBasics) {
			++basic_rows;
			expect_true("basic skill row order matches the spec table",
						static_cast<int>(expected.basic) == basic_rows - 1);
			const open::SkillRule &rule = open::skill_rule(expected.basic);
			expect_true("basic skill has a name", rule.name != nullptr && rule.name[0] != '\0');
			expect_true("basic skill display name matches the spec",
						rule.name != nullptr && std::strcmp(rule.name, expected.name) == 0);
			expect_true("basic skill link count is one or two",
						rule.link_count == 1 || rule.link_count == 2);
			expect_true("basic skill link count matches the spec", rule.link_count == expected.link_count);
			for (int i = 0; i < expected.link_count; ++i) {
				expect_true("basic skill link matches the spec",
							rule.links[static_cast<std::size_t>(i)] == expected.links[static_cast<std::size_t>(i)]);
				attribute_use[static_cast<std::size_t>(rule.links[static_cast<std::size_t>(i)])]++;
			}
			expect_true("basic skill name is not duplicated",
						name_is_unique(names, rule.name ? rule.name : ""));
			names.push_back(rule.name ? rule.name : "");
		}
		expect_true("every basic skill row is covered by the test table",
					basic_rows == static_cast<int>(open::BasicSkill::Count));
		for (int i = 0; i < static_cast<int>(open::Attribute::Count); ++i) {
			expect_true("every attribute is linked by at least one basic skill", attribute_use[static_cast<std::size_t>(i)] > 0);
		}
	}

	{
		// Break this catches: an option attached to the wrong parent, an option
		// that is missing, or a basic that gained options the spec does not list.
		std::vector<std::string> names;
		int advanced_rows = 0;
		std::array<int, 35> listed{};
		for (const ExpectedAdvanced &expected : kExpectedAdvanced) {
			++advanced_rows;
			expect_true("advanced option order matches the grouped table",
						static_cast<int>(expected.advanced) == advanced_rows - 1);
			const open::AdvancedRule &rule = open::advanced_rule(expected.advanced);
			expect_true("advanced option has a name", rule.name != nullptr && rule.name[0] != '\0');
			expect_true("advanced option display name matches the spec",
						rule.name != nullptr && std::strcmp(rule.name, expected.name) == 0);
			expect_true("advanced option parent matches the spec", rule.parent == expected.parent);
			expect_true("advanced option parent is valid", open::valid(rule.parent));
			listed[static_cast<std::size_t>(rule.parent)]++;
			expect_true("advanced option name is not duplicated",
						name_is_unique(names, rule.name ? rule.name : ""));
			names.push_back(rule.name ? rule.name : "");
		}
		expect_true("every advanced option is covered by the test table",
					advanced_rows == static_cast<int>(open::AdvancedSkill::Count));
		for (int i = 0; i < static_cast<int>(open::BasicSkill::Count); ++i) {
			const open::BasicSkill basic = static_cast<open::BasicSkill>(i);
			int actual = 0;
			for (int a = 0; a < static_cast<int>(open::AdvancedSkill::Count); ++a) {
				if (open::advanced_rule(static_cast<open::AdvancedSkill>(a)).parent == basic) {
					actual++;
				}
			}
			expect_true("a basic's option list has no missing or extra rows",
						actual == listed[static_cast<std::size_t>(i)]);
		}
		expect_true("endurance has no advanced options",
					listed[static_cast<std::size_t>(open::BasicSkill::Endurance)] == 0);
		expect_true("lockpicking has no advanced options",
					listed[static_cast<std::size_t>(open::BasicSkill::Lockpicking)] == 0);
		expect_true("elemental magic has six elements",
					listed[static_cast<std::size_t>(open::BasicSkill::ElementalMagic)] == 6);
	}

	{
		// Break this catches: a title threshold drift, a title wired to the wrong
		// skill, or Dragonslayer becoming a second skill bonus.
		std::vector<std::string> names;
		int title_rows = 0;
		for (const ExpectedTitle &expected : kExpectedTitles) {
			++title_rows;
			expect_true("title row order matches the spec table",
						static_cast<int>(expected.title) == title_rows - 1);
			const open::TitleRule &rule = open::title_rule(expected.title);
			expect_true("title has a name", rule.name != nullptr && rule.name[0] != '\0');
			expect_true("title display name matches the spec",
						rule.name != nullptr && std::strcmp(rule.name, expected.name) == 0);
			expect_true("title threshold matches the spec", rule.threshold == expected.threshold);
			expect_true("title threshold is positive", rule.threshold > 0u);
			if (expected.has_skill) {
				expect_true("title names a basic skill", rule.skill.has_value());
				expect_true("title names the right basic skill", rule.skill.value_or(expected.skill) == expected.skill);
			} else {
				expect_true("specialist title names no skill", !rule.skill.has_value());
			}
			expect_true("title name is not duplicated", name_is_unique(names, rule.name ? rule.name : ""));
			names.push_back(rule.name ? rule.name : "");
		}
		expect_true("every title row is covered by the test table",
					title_rows == static_cast<int>(open::Title::Count));
	}

	{
		// Break this catches: a blessing wired to the wrong attribute or god.
		std::vector<std::string> gods;
		std::vector<std::string> blessings;
		for (const auto &expected : kExpectedBlessings) {
			const open::BlessingRule &rule = open::blessing_rule(expected.attribute);
			expect_true("blessing god matches the spec",
						rule.god != nullptr && std::strcmp(rule.god, expected.god) == 0);
			expect_true("blessing name matches the spec",
						rule.name != nullptr && std::strcmp(rule.name, expected.blessing) == 0);
			expect_true("blessing god is not duplicated", name_is_unique(gods, rule.god ? rule.god : ""));
			expect_true("blessing name is not duplicated", name_is_unique(blessings, rule.name ? rule.name : ""));
			gods.push_back(rule.god ? rule.god : "");
			blessings.push_back(rule.name ? rule.name : "");
		}
		expect_true("every attribute has a blessing row",
					sizeof(kExpectedBlessings) / sizeof(kExpectedBlessings[0]) ==
						static_cast<int>(open::Attribute::Count));
	}

	{
		// Break this catches: a locked constant edited by accident.
		expect_close("pool base", open::kPoolBase, 100.0f);
		expect_close("blessing multiplier", open::kBlessingMultiplier, 1.25f);
		expect_close("title skill multiplier", open::kTitleSkillMultiplier, 1.1f);
		expect_close("specialist target multiplier", open::kSpecialistTargetMultiplier, 1.25f);
		expect_close("walking speed", open::kWalkingSpeed, 4.5f);
		expect_close("head body fraction", open::kHeadBodyFraction, 0.25f);
		expect_close("torso body fraction", open::kTorsoBodyFraction, 0.5f);
		expect_close("limb body fraction", open::kLimbBodyFraction, 0.4f);
		expect_close("sprint stamina per second", open::kSprintStaminaPerSecond, 10.0f);
		expect_close("melee swing stamina", open::kMeleeSwingStamina, 8.0f);
		expect_close("labour stamina", open::kLabourStamina, 5.0f);
		expect_close("swim stamina per second", open::kSwimStaminaPerSecond, 6.0f);
		expect_close("spell mana", open::kSpellMana, 20.0f);
		expect_close("out-of-combat stamina regen base", open::kStaminaRegenBasePerSecond, 10.0f);
		expect_close("out-of-combat mana regen base", open::kManaRegenBasePerSecond, 4.0f);
		expect_close("in-combat stamina regen factor", open::kCombatStaminaRegenFactor, 0.5f);
		expect_close("in-combat mana regen factor", open::kCombatManaRegenFactor, 0.25f);
		expect_close("arrive radius stays the spike value", open::MoveOrder::kArriveRadius, 0.4f);
	}

	{
		// Break this catches: an unchecked ID that would be silent instead of loud.
		expect_throws_invalid("skill rule rejects a bad basic id",
							  [] { open::skill_rule(static_cast<open::BasicSkill>(35)); });
		expect_throws_invalid("advanced rule rejects a bad option id",
							  [] { open::advanced_rule(static_cast<open::AdvancedSkill>(72)); });
		expect_throws_invalid("title rule rejects a bad title id",
							  [] { open::title_rule(static_cast<open::Title>(14)); });
		expect_throws_invalid("blessing rule rejects a bad attribute id",
							  [] { open::blessing_rule(static_cast<open::Attribute>(10)); });
	}

	return test_summary();
}
