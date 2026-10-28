#include "open/character_rules.hpp"

#include <cstddef>
#include <stdexcept>

namespace open {
namespace {

template <class Enum>
bool in_range(Enum value) {
	const int id = static_cast<int>(value);
	return id >= 0 && id < static_cast<int>(Enum::Count);
}

template <class Enum>
std::size_t index(Enum value) {
	return static_cast<std::size_t>(static_cast<int>(value));
}

[[noreturn]] void reject(const char *what) {
	throw std::invalid_argument(what);
}

// Order must match the enum declaration order.
constexpr std::array<SkillRule, static_cast<std::size_t>(BasicSkill::Count)> kSkillRules = {{
	{"One-Handed Weapons", {Attribute::Strength, Attribute::Dexterity}, 2},
	{"Two-Handed Weapons", {Attribute::Strength, Attribute::Toughness}, 2},
	{"Polearms", {Attribute::Strength, Attribute::Dexterity}, 2},
	{"Unarmed Combat", {Attribute::Strength, Attribute::Agility}, 2},
	{"Archery", {Attribute::Dexterity, Attribute::Perception}, 2},
	{"Blocking", {Attribute::Strength, Attribute::Toughness}, 2},
	{"Dodging", {Attribute::Agility, Attribute::Agility}, 1},
	{"Athletics", {Attribute::Agility, Attribute::Constitution}, 2},
	{"Acrobatics", {Attribute::Agility, Attribute::Dexterity}, 2},
	{"Swimming", {Attribute::Strength, Attribute::Constitution}, 2},
	{"Endurance", {Attribute::Constitution, Attribute::Constitution}, 1},
	{"Hunting", {Attribute::Perception, Attribute::Luck}, 2},
	{"Foraging", {Attribute::Perception, Attribute::Intelligence}, 2},
	{"Medicine", {Attribute::Intelligence, Attribute::Dexterity}, 2},
	{"Animal Handling", {Attribute::Charisma, Attribute::Perception}, 2},
	{"Smithing", {Attribute::Strength, Attribute::Dexterity}, 2},
	{"Construction", {Attribute::Strength, Attribute::Intelligence}, 2},
	{"Cooking", {Attribute::Dexterity, Attribute::Intelligence}, 2},
	{"Alchemy", {Attribute::Intelligence, Attribute::Profanity}, 2},
	{"Research", {Attribute::Intelligence, Attribute::Profanity}, 2},
	{"Mining", {Attribute::Strength, Attribute::Constitution}, 2},
	{"Farming", {Attribute::Constitution, Attribute::Intelligence}, 2},
	{"Persuasion", {Attribute::Charisma, Attribute::Charisma}, 1},
	{"Intimidation", {Attribute::Charisma, Attribute::Strength}, 2},
	{"Trading", {Attribute::Charisma, Attribute::Intelligence}, 2},
	{"Leadership", {Attribute::Charisma, Attribute::Charisma}, 1},
	{"Spellcasting", {Attribute::Intelligence, Attribute::Profanity}, 2},
	{"Casting Speed", {Attribute::Agility, Attribute::Profanity}, 2},
	{"Mana Control", {Attribute::Constitution, Attribute::Profanity}, 2},
	{"Elemental Magic", {Attribute::Profanity, Attribute::Profanity}, 1},
	{"Restoration", {Attribute::Intelligence, Attribute::Profanity}, 2},
	{"Enchanting", {Attribute::Intelligence, Attribute::Dexterity}, 2},
	{"Lockpicking", {Attribute::Dexterity, Attribute::Perception}, 2},
	{"Thievery", {Attribute::Dexterity, Attribute::Luck}, 2},
	{"Stealth", {Attribute::Agility, Attribute::Perception}, 2},
}};

// Order must match the enum declaration order; each block belongs to one parent.
constexpr std::array<AdvancedRule, static_cast<std::size_t>(AdvancedSkill::Count)> kAdvancedRules = {{
	{"Katana", BasicSkill::OneHandedWeapons},
	{"Sword", BasicSkill::OneHandedWeapons},
	{"Axe", BasicSkill::OneHandedWeapons},
	{"Mace", BasicSkill::OneHandedWeapons},
	{"Dagger", BasicSkill::OneHandedWeapons},

	{"Greatsword", BasicSkill::TwoHandedWeapons},
	{"GreatAxe", BasicSkill::TwoHandedWeapons},
	{"ColosalSword", BasicSkill::TwoHandedWeapons},
	{"Warhammer", BasicSkill::TwoHandedWeapons},

	{"Spear", BasicSkill::Polearms},
	{"Halberd", BasicSkill::Polearms},

	{"Boxing", BasicSkill::UnarmedCombat},
	{"Taekwondo", BasicSkill::UnarmedCombat},

	{"Bow", BasicSkill::Archery},
	{"Crossbow", BasicSkill::Archery},

	{"Small Shields", BasicSkill::Blocking},
	{"Big Shields", BasicSkill::Blocking},
	{"Wall Shields", BasicSkill::Blocking},

	{"Footwork", BasicSkill::Dodging},
	{"Acrobat Evasion", BasicSkill::Dodging},

	{"Sprinting", BasicSkill::Athletics},
	{"Distance Running", BasicSkill::Athletics},

	{"Climbing", BasicSkill::Acrobatics},
	{"Parkour", BasicSkill::Acrobatics},

	{"Speed Swimming", BasicSkill::Swimming},

	{"Big Game Hunting", BasicSkill::Hunting},
	{"Trapping", BasicSkill::Hunting},

	{"Herbalism", BasicSkill::Foraging},
	{"Wild Provisioning", BasicSkill::Foraging},

	{"Surgery", BasicSkill::Medicine},
	{"Field aid", BasicSkill::Medicine},

	{"Mounts", BasicSkill::AnimalHandling},
	{"Beasts", BasicSkill::AnimalHandling},

	{"Weaponsmith", BasicSkill::Smithing},
	{"Armoursmith", BasicSkill::Smithing},

	{"Fortification", BasicSkill::Construction},
	{"Civil Engineering", BasicSkill::Construction},

	{"Field Cooking", BasicSkill::Cooking},
	{"Gourmet Cooking", BasicSkill::Cooking},

	{"Potions", BasicSkill::Alchemy},
	{"Poisons", BasicSkill::Alchemy},

	{"Engineering", BasicSkill::Research},
	{"Arcane Research", BasicSkill::Research},

	{"Ore Extraction", BasicSkill::Mining},
	{"Quarrying", BasicSkill::Mining},

	{"Agriculture", BasicSkill::Farming},
	{"Horticulture", BasicSkill::Farming},

	{"Diplomacy", BasicSkill::Persuasion},

	{"Terror", BasicSkill::Intimidation},

	{"Haggling", BasicSkill::Trading},

	{"Battlefield Command", BasicSkill::Leadership},
	{"Governance", BasicSkill::Leadership},

	{"Ritual Casting", BasicSkill::Spellcasting},
	{"Combat Casting", BasicSkill::Spellcasting},

	{"Reaction Casting", BasicSkill::CastingSpeed},
	{"Spell Chaining", BasicSkill::CastingSpeed},

	{"Mana Conservation", BasicSkill::ManaControl},
	{"Channeling", BasicSkill::ManaControl},

	{"Fire", BasicSkill::ElementalMagic},
	{"Ice", BasicSkill::ElementalMagic},
	{"Lightning", BasicSkill::ElementalMagic},
	{"Earth", BasicSkill::ElementalMagic},
	{"Light", BasicSkill::ElementalMagic},
	{"Dark", BasicSkill::ElementalMagic},

	{"Healing", BasicSkill::Restoration},
	{"Wards", BasicSkill::Restoration},

	{"Weapon enchant", BasicSkill::Enchanting},
	{"Armour enchant", BasicSkill::Enchanting},

	{"Pickpocket", BasicSkill::Thievery},
	{"Burglary", BasicSkill::Thievery},

	{"Infiltration", BasicSkill::Stealth},
	{"Assassination", BasicSkill::Stealth},
}};

// std::optional keeps this table non-literal; it is still a fixed table built
// once at load time, with no runtime registration.
const std::array<TitleRule, static_cast<std::size_t>(Title::Count)> kTitleRules = {{
	TitleRule{"Dragonslayer", 100u, std::nullopt},
	TitleRule{"Duelist", 100u, BasicSkill::OneHandedWeapons},
	TitleRule{"Breaker", 100u, BasicSkill::TwoHandedWeapons},
	TitleRule{"Wall", 200u, BasicSkill::Blocking},
	TitleRule{"Ghost", 100u, BasicSkill::Stealth},
	TitleRule{"Huntsman", 100u, BasicSkill::Hunting},
	TitleRule{"Sawbones", 50u, BasicSkill::Medicine},
	TitleRule{"Hammerhand", 100u, BasicSkill::Smithing},
	TitleRule{"Mason", 10u, BasicSkill::Construction},
	TitleRule{"Green Thumb", 100u, BasicSkill::Farming},
	TitleRule{"Hexer", 100u, BasicSkill::Spellcasting},
	TitleRule{"Cutpurse", 50u, BasicSkill::Thievery},
	TitleRule{"Silver Tongue", 50u, BasicSkill::Persuasion},
	TitleRule{"Iron Back", 100u, BasicSkill::Endurance},
}};

// Order must match Attribute declaration order.
constexpr std::array<BlessingRule, static_cast<std::size_t>(Attribute::Count)> kBlessings = {{
	{"Korr", "Iron"},
	{"Sile", "Sure Hand"},
	{"Ryn", "Wind"},
	{"Groth", "Stone"},
	{"Vel", "Blood"},
	{"Isk", "Sight"},
	{"Quen", "Lore"},
	{"Ael", "Voice"},
	{"Nyx", "Fortune"},
	{"Vex", "Blasphemy"},
}};

constexpr std::array<float, 7> kLevelMultipliers = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 100.0f};

}  // namespace

bool valid(Faction faction) {
	return faction == Faction::Friendly || faction == Faction::Enemy;
}

bool valid(Attribute attribute) {
	return in_range(attribute);
}

bool valid(BasicSkill skill) {
	return in_range(skill);
}

bool valid(AdvancedSkill skill) {
	return in_range(skill);
}

bool valid(Title title) {
	return in_range(title);
}

bool valid(BodyPart part) {
	return in_range(part);
}

bool valid(PoolKind pool) {
	return in_range(pool);
}

bool valid(Slot slot) {
	return in_range(slot);
}

bool valid(int level) {
	return level >= 1 && level <= static_cast<int>(kLevelMultipliers.size());
}

float level_multiplier(int level) {
	if (!valid(level)) {
		reject("level_multiplier: level must be 1..7");
	}
	return kLevelMultipliers[static_cast<std::size_t>(level - 1)];
}

const SkillRule &skill_rule(BasicSkill skill) {
	if (!valid(skill)) {
		reject("skill_rule: unknown basic skill id");
	}
	return kSkillRules[index(skill)];
}

const AdvancedRule &advanced_rule(AdvancedSkill skill) {
	if (!valid(skill)) {
		reject("advanced_rule: unknown advanced skill id");
	}
	return kAdvancedRules[index(skill)];
}

const TitleRule &title_rule(Title title) {
	if (!valid(title)) {
		reject("title_rule: unknown title id");
	}
	return kTitleRules[index(title)];
}

const BlessingRule &blessing_rule(Attribute attribute) {
	if (!valid(attribute)) {
		reject("blessing_rule: unknown attribute id");
	}
	return kBlessings[index(attribute)];
}

}  // namespace open
