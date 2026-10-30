#include "open/weapon_rules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "open/character_rules.hpp"
#include "open/items.hpp"

namespace open {
namespace {

// The same seven tiers for the same reasons in both directions: damage scales with
// craftsmanship, and the last tier is a cliff on purpose.
constexpr std::array<double, kQualityTierCount> kQualityMultipliers = {1.0,  2.0,  4.0,   8.0,
																	   16.0, 32.0, 100.0};

// A percentage point bound shared with the definition validator: resistances,
// penetrations and mitigation all live in 0..100.
constexpr double kPercentMax = 100.0;

// docs/weapons.md, Toughness reduction: damage after armour divided by
// 1 + 0.05 x base Toughness.
constexpr double kToughnessCoefficient = 0.05;

// docs/combat.md section 4: nothing in the game attacks faster than 3/s or slower
// than once per 5 s, whatever the weapon or the shortfall. weapon_rules.cpp owns
// these for the typed path; the scalar helper in combat_rules.cpp keeps its own
// copy until Task 10 replaces that path, and the two bands are the same numbers.
constexpr float kFastestCooldown = 1.0f / 3.0f;
constexpr float kSlowestCooldown = 5.0f;

// The rate floor and span from the same section: 0.2 + 2.8 x trained/200.
constexpr double kRateFloor = 0.2;
constexpr double kRateSpan = 2.8;
constexpr double kMaxTrained = 200.0;

double clamp(double value, double low, double high) {
	return value < low ? low : (value > high ? high : value);
}

bool finite(double value) {
	return std::isfinite(value);
}

// The locked unarmed fallback (docs/weapons.md, Unarmed fallback): 0 cut, 0 pierce,
// 8 blunt, reach 0..1.5, cost 8, speed 1, no requirements, nothing to wear. Its
// quality factor is 1, so it is reported as tier 1 rather than as a tier it does not
// have. These are the same numbers the scalar slice uses; the limb gate is its own
// too, so nothing here invents a new one.
inline constexpr double kUnarmedMaxReach = 1.5;
inline constexpr double kUnarmedCost = 8.0;
inline constexpr unsigned kUnarmedQuality = 1;

WeaponStats unarmed_stats() {
	WeaponStats stats;
	stats.damage = {0.0, 0.0, 8.0};
	stats.min_reach = 0.0;
	stats.max_reach = kUnarmedMaxReach;
	stats.speed = 1.0;
	stats.stamina_cost = kUnarmedCost;
	stats.block_percent = 0.0;
	stats.weight = 0.0;
	stats.weight_class = WeightClass::Light;
	stats.required_trained_rank = 0;  // unarmed declares no weapon requirement
	return stats;
}

bool has_surviving_limb(const Character &character) {
	const BodyPart limbs[4] = {BodyPart::LeftArm, BodyPart::RightArm, BodyPart::LeftLeg,
							   BodyPart::RightLeg};
	for (const BodyPart part : limbs) {
		if (character.body_part(part).current > 0.0f) {
			return true;
		}
	}
	return false;
}

// The held items this sheet can actually use, right hand first because that is the
// weapon it attacks with. A two-handed item reserves both hands but is owned once,
// so it appears once here and cannot be counted twice.
std::vector<ItemId> usable_held_ids(const Character &character) {
	std::vector<ItemId> ids;
	const std::optional<ItemId> hands[2] = {character.equipped(Slot::RightHand),
										   character.equipped(Slot::LeftHand)};
	for (const std::optional<ItemId> &id : hands) {
		if (!id.has_value() || *id == 0) {
			continue;
		}
		if (std::find(ids.begin(), ids.end(), *id) != ids.end()) {
			continue;
		}
		// Gated at query time: a disabled arm takes gear out of use without taking
		// it off the character.
		if (!character.held_usable(*id)) {
			continue;
		}
		ids.push_back(*id);
	}
	return ids;
}

// One owned instance, resolved to what it is.
struct Armed {
	ItemId id = 0;
	const ItemDefinition *definition = nullptr;
	unsigned quality = 0;
	unsigned durability = 0;
	unsigned max_durability = 0;
};

// The enchantments on the owned record being swung, as they were made: read at query
// time so a blade enchanted between two swings is priced from what it actually has.
std::vector<AppliedEnchantment> enchantments_of(const Character &character, ItemId id) {
	const std::optional<ItemInstance> instance = character.instance(id);
	if (!instance.has_value()) {
		return {};
	}
	return instance->enchantments;
}

// An item this sheet cannot describe has no combat stats: no catalog, no owned
// instance, or an instance whose definition is not in the catalog. None of those
// are guessed at, which is what keeps a placement-only record from arming anyone.
std::optional<Armed> arm_of(const Character &character, ItemId id) {
	const std::shared_ptr<const ItemCatalog> catalog = character.catalog();
	if (catalog == nullptr) {
		return std::nullopt;
	}
	const std::optional<ItemInstance> instance = character.instance(id);
	if (!instance.has_value() || instance->definition == 0) {
		return std::nullopt;
	}
	const ItemDefinition *definition = catalog->item(instance->definition);
	if (definition == nullptr) {
		return std::nullopt;
	}
	Armed armed;
	armed.id = id;
	armed.definition = definition;
	armed.quality = instance->quality;
	armed.durability = instance->durability;
	armed.max_durability = instance->max_durability;
	return armed;
}

// True with the horizontal distance in `out`, or false when the input cannot be
// measured. Doubles throughout, so a huge separation overflows to a non-finite
// answer rather than to a plausible float distance.
bool horizontal_separation(const Vec3 &self, const Vec3 &target, double &out) {
	if (!std::isfinite(self.x) || !std::isfinite(self.z) || !std::isfinite(target.x) ||
		!std::isfinite(target.z)) {
		return false;
	}
	const double dx = static_cast<double>(target.x) - static_cast<double>(self.x);
	const double dz = static_cast<double>(target.z) - static_cast<double>(self.z);
	const double distance = std::sqrt(dx * dx + dz * dz);
	if (!std::isfinite(distance)) {
		return false;
	}
	out = distance;
	return true;
}

}  // namespace

std::optional<double> quality_damage_multiplier(unsigned quality) {
	if (quality < 1 || quality > kQualityTierCount) {
		return std::nullopt;
	}
	return kQualityMultipliers[quality - 1];
}

std::optional<double> requirement_effectiveness(double actual, double required) {
	if (!std::isfinite(required) || required <= 0.0) {
		return std::nullopt;
	}
	if (!std::isfinite(actual) || actual < 0.0) {
		return std::nullopt;
	}
	const double ratio = clamp(actual / required, 0.0, 1.0);
	return ratio * ratio;
}

std::optional<double> weapon_effectiveness(const Character &character, const WeaponStats &stats,
											   const SkillJob &job,
											   const std::vector<AppliedEnchantment> &enchantments) {
	if (stats.required_trained_rank == 0) {
		return std::nullopt;  // a declared requirement is always positive
	}
	if (!valid(job.basic)) {
		return std::nullopt;
	}

	// The worst single shortfall limits the weapon. Multiplying them would punish a
	// character twice for one bad day, and the spec says the largest shortfall is
	// the limiting factor.
	// Every requirement the wielder actually faces: the weapon's own, with each
	// enchantment's requirement added to a matching attribute rather than competing
	// with it as a second line. Two edges asking 2 Intelligence alongside a base 1 ask
	// 5 in total, checked once, while a Profanity requirement stays separate.
	std::vector<Requirement> faced = stats.requirements;
	for (const AppliedEnchantment &applied : enchantments) {
		if (!applied.requirement.has_value()) {
			continue;
		}
		const Attribute attribute = applied.requirement->attribute;
		const double value = applied.requirement->value;
		if (!valid(attribute) || !std::isfinite(value) || value <= 0.0) {
			return std::nullopt;
		}
		bool merged = false;
		for (Requirement &existing : faced) {
			if (existing.attribute == attribute) {
				existing.value += value;
				merged = true;
				break;
			}
		}
		if (!merged) {
			faced.push_back(Requirement{attribute, value});
		}
	}

	double effectiveness = 1.0;
	for (const Requirement &need : faced) {
		if (!valid(need.attribute) || !std::isfinite(need.value) || need.value <= 0.0) {
			return std::nullopt;
		}
		// Effective, not base: the level multiplier and the blessing are already
		// inside the attribute, so they are never separate requirements.
		const std::optional<double> met =
			requirement_effectiveness(static_cast<double>(character.attribute(need.attribute)), need.value);
		if (!met.has_value()) {
			return std::nullopt;
		}
		effectiveness = *met < effectiveness ? *met : effectiveness;
	}

	const unsigned trained = character.resolve_skill(job.basic, job.option).rank;
	const std::optional<double> skilled = requirement_effectiveness(static_cast<double>(trained),
															 static_cast<double>(stats.required_trained_rank));
	if (!skilled.has_value()) {
		return std::nullopt;
	}
	return *skilled < effectiveness ? *skilled : effectiveness;
}

double weapon_attack_cost(double base_cost, double effectiveness) {
	return base_cost * (2.0 - effectiveness);
}

float weapon_cooldown(unsigned trained, double speed, double effectiveness, bool exhausted) {
	if (!std::isfinite(speed) || !std::isfinite(effectiveness) || speed <= 0.0 || effectiveness <= 0.0) {
		// Zero effectiveness is a real state (a wholly unfit wielder) and answers the
		// slowest permitted cooldown instead of dividing by it. Anything worse than
		// that is a caller bug, and the safe answer is still the same.
		return kSlowestCooldown;
	}

	const double skill_rate = kRateFloor + kRateSpan * (static_cast<double>(trained) / kMaxTrained);
	const double rate = skill_rate * speed * effectiveness;
	if (!std::isfinite(rate) || rate <= 0.0) {
		return kSlowestCooldown;
	}

	double seconds = 1.0 / rate;
	seconds = clamp(seconds, static_cast<double>(kFastestCooldown), static_cast<double>(kSlowestCooldown));
	if (exhausted) {
		// An unpaid swing is slower, and still cannot leave the band.
		seconds = clamp(seconds * 2.0, static_cast<double>(kFastestCooldown),
						static_cast<double>(kSlowestCooldown));
	}
	return static_cast<float>(seconds);
}

bool horizontal_in_reach(const Vec3 &self, const Vec3 &target, double max_reach) {
	if (!finite(max_reach) || max_reach <= 0.0) {
		return false;
	}
	double distance = 0.0;
	if (!horizontal_separation(self, target, distance)) {
		return false;
	}
	// Inclusive: the spec says exactly at maximum reach the target is in range.
	//
	// The comparison is made in float, because that is the domain the positions are
	// observed in: float 0.2 is 0.20000000298..., which a double comparison would
	// refuse as "past 0.2" even though the engine cannot express a target any closer
	// to a 0.2-reach weapon. Measuring in double keeps an absurd separation from
	// looking plausible; narrowing before the bound keeps a real one from looking
	// out of range.
	const float measured = static_cast<float>(distance);
	const float limit = static_cast<float>(max_reach);
	return std::isfinite(measured) && std::isfinite(limit) && measured <= limit;
}

double minimum_range_factor(double separation, double min_reach) {
	if (!finite(min_reach) || min_reach <= 0.0 || !finite(separation)) {
		return 1.0;  // no minimum declared, or no trustworthy measurement
	}
	// Strictly inside. Standing on the minimum costs nothing.
	return separation < min_reach ? 0.5 : 1.0;
}

Vec3 pursuit_velocity(const Vec3 &self, const Vec3 &target, double max_reach, float speed) {
	if (!std::isfinite(speed) || speed <= 0.0 || !finite(max_reach) || max_reach <= 0.0) {
		return {};
	}
	double distance = 0.0;
	if (!horizontal_separation(self, target, distance) || distance <= 0.0) {
		return {};
	}
	// The same test the swing itself will be gated by, so an actor never walks
	// "into" a reach the attack query disagrees about, and never retreats out of one
	// it agrees is reached.
	if (horizontal_in_reach(self, target, max_reach)) {
		return {};
	}

	// Walking stops kArriveRadius short of what it walks toward, so the aim point is
	// that much back from the target along the line of approach. When the reach is
	// shorter than the arrival radius the offset goes negative and the aim point
	// lands beyond the target, which is what lets a 0.2-reach weapon close instead
	// of stalling at 0.4.
	const double aim_back = max_reach - static_cast<double>(MoveOrder::kArriveRadius);
	const double unit_x = static_cast<double>(target.x - self.x) / distance;
	const double unit_z = static_cast<double>(target.z - self.z) / distance;
	const double aim_x = static_cast<double>(target.x) - unit_x * aim_back;
	const double aim_z = static_cast<double>(target.z) - unit_z * aim_back;

	MoveOrder pursuit;
	if (!pursuit.set_destination(static_cast<float>(aim_x), self.y, static_cast<float>(aim_z))) {
		return {};
	}
	// The caller's speed, so an injured leg stays half as fast, and the actor's own
	// Y, so the query stays horizontal.
	return pursuit.velocity_at(self.x, self.y, self.z, speed);
}

std::optional<WeaponAttackProfile> weapon_attack_profile(const Character &character) {
	if (!character.alive()) {
		return std::nullopt;
	}

	for (const ItemId id : usable_held_ids(character)) {
		const std::optional<Armed> armed = arm_of(character, id);
		if (!armed.has_value() || !armed->definition->weapon.has_value()) {
			continue;  // a shield, a carried thing, or a record with no definition
		}
		const WeaponDefinition &weapon = *armed->definition->weapon;
		const std::vector<AppliedEnchantment> enchantments = enchantments_of(character, id);
		const std::optional<double> effectiveness =
			weapon_effectiveness(character, weapon.stats, weapon.job, enchantments);
		if (!effectiveness.has_value()) {
			return std::nullopt;  // a definition the rules cannot price is not swung blindly
		}
		WeaponAttackProfile profile;
		profile.job = weapon.job;
		profile.trained = character.resolve_skill(weapon.job.basic, weapon.job.option).rank;
		profile.item = id;
		profile.stats = weapon.stats;
		profile.effectiveness = *effectiveness;
		profile.quality = armed->quality;
		profile.durability = armed->durability;
		profile.max_durability = armed->max_durability;
		profile.enchantments = enchantments;
		return profile;
	}

	if (!has_surviving_limb(character)) {
		return std::nullopt;  // nothing to swing with and nothing to stand on
	}
	WeaponAttackProfile bare;
	bare.job = {BasicSkill::UnarmedCombat, character.chosen(BasicSkill::UnarmedCombat)};
	bare.trained = character.resolve_skill(bare.job.basic, bare.job.option).rank;
	bare.stats = unarmed_stats();
	bare.quality = kUnarmedQuality;
	return bare;
}

std::optional<WeaponDefendProfile> weapon_defend_profile(const Character &character) {
	if (!character.alive()) {
		return std::nullopt;
	}

	std::optional<Armed> shield;
	std::optional<Armed> weapon;
	for (const ItemId id : usable_held_ids(character)) {
		const std::optional<Armed> armed = arm_of(character, id);
		if (!armed.has_value()) {
			continue;
		}
		if (armed->definition->shield.has_value() && !shield.has_value()) {
			shield = armed;
		} else if (armed->definition->weapon.has_value() && !weapon.has_value()) {
			weapon = armed;
		}
	}

	if (shield.has_value()) {
		// Chosen even at zero durability and even at zero mitigation: the item being
		// worn out is a reason for reduced mitigation, not for changing the defender's
		// mind about what they are defending with.
		const ShieldDefinition &board = *shield->definition->shield;
		WeaponDefendProfile profile;
		profile.action = DefendAction::Block;
		profile.job = board.job;
		profile.trained = character.resolve_skill(board.job.basic, board.job.option).rank;
		profile.item = shield->id;
		profile.mitigation = board.block_percent;
		profile.durability = shield->durability;
		return profile;
	}

	// No shield: a weapon block is worth trying only if it is the defender's better
	// answer. The comparison is on effective skills; the clock the caller starts reads
	// the trained rank in the profile instead.
	const double blocking = static_cast<double>(character.effective_skill(BasicSkill::Blocking, std::nullopt));
	const std::optional<AdvancedSkill> dodge_option = character.chosen(BasicSkill::Dodging);
	const double dodging = static_cast<double>(character.effective_skill(BasicSkill::Dodging, dodge_option));
	if (weapon.has_value() && finite(blocking) && finite(dodging) && blocking > dodging) {
		const WeaponDefinition &armed = *weapon->definition->weapon;
		WeaponDefendProfile profile;
		profile.action = DefendAction::Block;
		// A weapon block is plain Blocking: the frozen basic when the character
		// specialised in a shield they are not holding.
		profile.job = {BasicSkill::Blocking, std::nullopt};
		profile.trained = character.resolve_skill(BasicSkill::Blocking, std::nullopt).rank;
		profile.item = weapon->id;
		profile.mitigation = armed.stats.block_percent;
		profile.durability = weapon->durability;
		return profile;
	}

	WeaponDefendProfile dodge;
	dodge.action = DefendAction::Dodge;
	dodge.job = {BasicSkill::Dodging, dodge_option};
	dodge.trained = character.resolve_skill(dodge.job.basic, dodge.job.option).rank;
	dodge.mitigation = 100.0;  // a ready dodge turns all of it aside; exhaustion halves that
	return dodge;
}

namespace {

// Durability at zero is a third of the damage, and the locked number is 0.33: not
// one third computed, because the difference is what a test would have to chase.
inline constexpr double kZeroDurabilityFactor = 0.33;

bool within(double value, double low, double high) {
	return finite(value) && value >= low && value <= high;
}

bool triple_within(const DamageComponents &value, double low, double high) {
	return within(value.cut, low, high) && within(value.pierce, low, high) &&
		   within(value.blunt, low, high);
}

double total_of(const DamageComponents &value) {
	return value.cut + value.pierce + value.blunt;
}

}  // namespace

std::optional<DamageResolution> resolve_typed_damage(DamageComponents offensive,
													 double mitigation_percent,
													 DamageComponents armour_percent,
													 DamageComponents penetration_points,
													 double base_toughness) {
	if (!triple_within(offensive, 0.0, std::numeric_limits<double>::max()) ||
		!within(mitigation_percent, 0.0, kPercentMax) ||
		!triple_within(armour_percent, 0.0, kPercentMax) ||
		!triple_within(penetration_points, 0.0, kPercentMax) ||
		!within(base_toughness, 0.0, std::numeric_limits<double>::max())) {
		return std::nullopt;
	}

	const double mitigation = mitigation_percent / 100.0;
	DamageResolution out;
	out.offensive = offensive;

	// Defense first. Prevention is the part the defense took, and the post-defense
	// component is exactly what is left of it, so the two always add back up.
	auto defend = [&](double incoming) {
		const double taken = incoming * mitigation;
		return std::pair<double, double>{incoming - taken, taken};
	};
	const std::pair<double, double> cut = defend(offensive.cut);
	const std::pair<double, double> pierce = defend(offensive.pierce);
	const std::pair<double, double> blunt = defend(offensive.blunt);
	out.after_defense = {cut.first, pierce.first, blunt.first};
	out.prevented = cut.second + pierce.second + blunt.second;

	// Armour, one component at a time, with penetration subtracted from the matching
	// resistance and floored at zero: more penetration than resistance is no
	// resistance, not armour that heals.
	auto soak = [&](double incoming, double resistance, double penetration) {
		const double effective = std::max(0.0, resistance - penetration);
		return incoming * (1.0 - effective / 100.0);
	};
	out.after_armour = {soak(out.after_defense.cut, armour_percent.cut, penetration_points.cut),
						soak(out.after_defense.pierce, armour_percent.pierce, penetration_points.pierce),
						soak(out.after_defense.blunt, armour_percent.blunt, penetration_points.blunt)};

	// Base Toughness is a universal divisor, and it is the only thing that reaches a
	// hit that armour entirely prevented.
	const double divisor = 1.0 + kToughnessCoefficient * base_toughness;
	if (!finite(divisor) || divisor <= 0.0) {
		return std::nullopt;
	}
	out.final = {out.after_armour.cut / divisor, out.after_armour.pierce / divisor,
				 out.after_armour.blunt / divisor};
	out.total = total_of(out.final);
	if (!finite(out.prevented) || !finite(out.total) || !triple_within(out.final, 0.0,
																	  std::numeric_limits<double>::max())) {
		return std::nullopt;
	}
	return out;
}

DamageComponents armour_for_part(const Character &character, BodyPart part) {
	const DamageComponents none = {0.0, 0.0, 0.0};
	if (!valid(part) || part == BodyPart::Count) {
		return none;
	}

	Slot worn = Slot::Count;
	switch (part) {
	case BodyPart::Head:
		worn = Slot::Head;
		break;
	case BodyPart::Torso:
		worn = Slot::Chest;
		break;
	case BodyPart::LeftArm:
	case BodyPart::RightArm:
		worn = Slot::Hands;
		break;
	case BodyPart::LeftLeg:
	case BodyPart::RightLeg:
		worn = Slot::Legs;
		break;
	case BodyPart::Count:
		return none;
	}
	if (worn == Slot::Count) {
		return none;  // Belt, rings and feet have no hit location to answer for
	}

	const std::optional<ItemId> id = character.equipped(worn);
	if (!id.has_value() || *id == 0) {
		return none;
	}
	const std::shared_ptr<const ItemCatalog> catalog = character.catalog();
	if (catalog == nullptr) {
		return none;
	}
	const std::optional<ItemInstance> instance = character.instance(*id);
	if (!instance.has_value() || instance->definition == 0) {
		return none;
	}
	const ItemDefinition *definition = catalog->item(instance->definition);
	if (definition == nullptr || !definition->armour.has_value()) {
		return none;  // a record that is not armour does not soak because it was worn
	}
	return definition->armour->resistance_percent;
}

std::optional<DamageComponents> scaled_offense(const WeaponAttackProfile &profile,
											   DamageComponents technique_factors, bool exhausted,
											   double min_factor) {
	const std::optional<double> quality = quality_damage_multiplier(profile.quality);
	if (!quality.has_value()) {
		return std::nullopt;
	}
	if (!triple_within(profile.stats.damage, 0.0, std::numeric_limits<double>::max()) ||
		!triple_within(technique_factors, 0.0, std::numeric_limits<double>::max()) ||
		!within(profile.effectiveness, 0.0, 1.0) || !within(min_factor, 0.0, 1.0)) {
		return std::nullopt;
	}
	if (min_factor == 0.0) {
		return std::nullopt;  // a factor is a fraction of the hit, never all of it removed
	}

	const double skill = static_cast<double>(profile.trained) / kMaxTrained;
	const double exhaustion = exhausted ? 0.5 : 1.0;
	// A weapon with nothing left swings badly. A swing with no weapon has no
	// durability to run out, so the unarmed fallback is never charged for it.
	const double durability =
		(profile.item.has_value() && profile.durability == 0) ? kZeroDurabilityFactor : 1.0;

	// The enchantments' fixed blunt, added to the weapon's own after quality: a tier-7
	// blade with 8 base blunt and 2 added is 802 before any other factor, not
	// (8 + 2) x 100.
	double added_blunt = 0.0;
	for (const AppliedEnchantment &applied : profile.enchantments) {
		added_blunt += applied.added_blunt;
	}
	if (!finite(added_blunt) || added_blunt < 0.0) {
		return std::nullopt;
	}

	auto scale = [&](double base, double factor, double fixed) -> std::optional<double> {
		// Quality multiplies the base components, the enchantment contributes its fixed
		// amount beside them, and only then does every remaining factor apply - to the
		// combination, so a blunt-favouring technique scales the enchantment too and a
		// ruined blade discounts it along with the rest.
		double value = base * *quality + fixed;
		value *= skill * profile.effectiveness * factor * exhaustion * min_factor * durability;
		if (!finite(value)) {
			return std::nullopt;
		}
		return value;
	};

	const std::optional<double> cut = scale(profile.stats.damage.cut, technique_factors.cut, 0.0);
	const std::optional<double> pierce = scale(profile.stats.damage.pierce, technique_factors.pierce, 0.0);
	const std::optional<double> blunt =
		scale(profile.stats.damage.blunt, technique_factors.blunt, added_blunt);
	if (!cut.has_value() || !pierce.has_value() || !blunt.has_value()) {
		return std::nullopt;
	}
	return DamageComponents{*cut, *pierce, *blunt};
}

}  // namespace open
