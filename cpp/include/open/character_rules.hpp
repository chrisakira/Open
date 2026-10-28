#pragma once

#include <array>
#include <optional>

namespace open {

// Locked sheet rules from docs/characters.md: the enum vocabulary, the fixed
// rule tables, and the constants. Nothing here is configurable at runtime and
// nothing here knows about Godot.

enum class Faction {
	Friendly = 0,
	Enemy = 1,
};

enum class Attribute {
	Strength,
	Dexterity,
	Agility,
	Toughness,
	Constitution,
	Perception,
	Intelligence,
	Charisma,
	Luck,
	Profanity,
	Count,
};

enum class BasicSkill {
	OneHandedWeapons,
	TwoHandedWeapons,
	Polearms,
	UnarmedCombat,
	Archery,
	Blocking,
	Dodging,
	Athletics,
	Acrobatics,
	Swimming,
	Endurance,
	Hunting,
	Foraging,
	Medicine,
	AnimalHandling,
	Smithing,
	Construction,
	Cooking,
	Alchemy,
	Research,
	Mining,
	Farming,
	Persuasion,
	Intimidation,
	Trading,
	Leadership,
	Spellcasting,
	CastingSpeed,
	ManaControl,
	ElementalMagic,
	Restoration,
	Enchanting,
	Lockpicking,
	Thievery,
	Stealth,
	Count,
};

// Options are grouped by their parent basic skill, in the spec's table order.
enum class AdvancedSkill {
	Katana,
	Sword,
	Axe,
	Mace,
	Dagger,

	Greatsword,
	GreatAxe,
	ColosalSword,
	Warhammer,

	Spear,
	Halberd,

	Boxing,
	Taekwondo,

	Bow,
	Crossbow,

	SmallShields,
	BigShields,
	WallShields,

	Footwork,
	AcrobatEvasion,

	Sprinting,
	DistanceRunning,

	Climbing,
	Parkour,

	SpeedSwimming,

	BigGameHunting,
	Trapping,

	Herbalism,
	WildProvisioning,

	Surgery,
	FieldAid,

	Mounts,
	Beasts,

	Weaponsmith,
	Armoursmith,

	Fortification,
	CivilEngineering,

	FieldCooking,
	GourmetCooking,

	Potions,
	Poisons,

	Engineering,
	ArcaneResearch,

	OreExtraction,
	Quarrying,

	Agriculture,
	Horticulture,

	Diplomacy,

	Terror,

	Haggling,

	BattlefieldCommand,
	Governance,

	RitualCasting,
	CombatCasting,

	ReactionCasting,
	SpellChaining,

	ManaConservation,
	Channeling,

	Fire,
	Ice,
	Lightning,
	Earth,
	Light,
	Dark,

	Healing,
	Wards,

	WeaponEnchant,
	ArmourEnchant,

	Pickpocket,
	Burglary,

	Infiltration,
	Assassination,

	Count,
};

enum class Title {
	Dragonslayer,
	Duelist,
	Breaker,
	Wall,
	Ghost,
	Huntsman,
	Sawbones,
	Hammerhand,
	Mason,
	GreenThumb,
	Hexer,
	Cutpurse,
	SilverTongue,
	IronBack,
	Count,
};

enum class BodyPart {
	Head,
	Torso,
	LeftArm,
	RightArm,
	LeftLeg,
	RightLeg,
	Count,
};

enum class PoolKind {
	Hp,
	Stamina,
	Mana,
	Count,
};

// Hands is gloves armour; LeftHand/RightHand are the held slots.
enum class Slot {
	Head,
	Chest,
	Legs,
	Feet,
	Hands,
	LeftHand,
	RightHand,
	Ring0,
	Ring1,
	Ring2,
	Ring3,
	Ring4,
	Ring5,
	Ring6,
	Ring7,
	Ring8,
	Ring9,
	Backpack,
	Belt,
	Collar,
	Count,
};

// Checked before any conversion or array index. Callers that cannot throw (the
// Godot bridge) use these instead of catching.
bool valid(Faction faction);
bool valid(Attribute attribute);
bool valid(BasicSkill skill);
bool valid(AdvancedSkill skill);
bool valid(Title title);
bool valid(BodyPart part);
bool valid(PoolKind pool);
bool valid(Slot slot);
bool valid(int level);

// Throws std::invalid_argument for a level outside 1..7.
float level_multiplier(int level);

struct SkillRule {
	const char *name;
	std::array<Attribute, 2> links;
	int link_count;
};

struct AdvancedRule {
	const char *name;
	BasicSkill parent;
};

struct TitleRule {
	const char *name;
	unsigned threshold;
	std::optional<BasicSkill> skill;  // empty for a specialist title
};

struct BlessingRule {
	const char *god;
	const char *name;
};

// Every checked getter throws std::invalid_argument for an id outside its enum.
const SkillRule &skill_rule(BasicSkill skill);
const AdvancedRule &advanced_rule(AdvancedSkill skill);
const TitleRule &title_rule(Title title);
const BlessingRule &blessing_rule(Attribute attribute);

inline constexpr float kPoolBase = 100.0f;
inline constexpr float kBlessingMultiplier = 1.25f;
inline constexpr float kTitleSkillMultiplier = 1.1f;
inline constexpr float kSpecialistTargetMultiplier = 1.25f;
inline constexpr float kWalkingSpeed = 4.5f;

// Part maxima as a fraction of main HP maximum.
inline constexpr float kHeadBodyFraction = 0.25f;
inline constexpr float kTorsoBodyFraction = 0.5f;
inline constexpr float kLimbBodyFraction = 0.4f;

// Resource costs (placeholders, balance later).
inline constexpr float kSprintStaminaPerSecond = 10.0f;
inline constexpr float kMeleeSwingStamina = 8.0f;
inline constexpr float kLabourStamina = 5.0f;
inline constexpr float kSwimStaminaPerSecond = 6.0f;
inline constexpr float kSpellMana = 20.0f;

// Regen per second, before the attribute factor (placeholders, balance later).
inline constexpr float kStaminaRegenBasePerSecond = 10.0f;
inline constexpr float kManaRegenBasePerSecond = 4.0f;
inline constexpr float kCombatStaminaRegenFactor = 0.5f;
inline constexpr float kCombatManaRegenFactor = 0.25f;

}  // namespace open
