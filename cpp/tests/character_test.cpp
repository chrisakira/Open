#include "open/character.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "test_support.hpp"

// Task 7 aggregate tests. Break these catches: a factory that accepts an invalid
// sheet, a composed character that shares state with another, a skill formula
// that squares level or applies a title twice, a pool that level multiplies, a
// rejected change that half-applied, a dead character that keeps acting, or
// movement that ignores legs, death and the raw switch.

namespace {

open::CharacterConfig config(std::uint64_t id, const char *name, open::Faction faction, int level,
							 open::Attribute blessing) {
	open::CharacterConfig made;
	made.id = id;
	made.name = name;
	made.faction = faction;
	made.level = level;
	made.blessing = blessing;
	return made;
}

open::Character make_character(std::uint64_t id = 1, open::Attribute blessing = open::Attribute::Strength) {
	const std::optional<open::Character> made =
		open::Character::create(config(id, "", open::Faction::Friendly, 1, blessing));
	expect_true("the test character is created", made.has_value());
	return *made;
}

open::ItemRef chest_armour(open::ItemId id) {
	open::ItemRef item;
	item.id = id;
	item.hand_use = open::HandUse::None;
	item.allowed_slots.set(static_cast<std::size_t>(static_cast<int>(open::Slot::Chest)));
	return item;
}

open::ItemRef head_armour(open::ItemId id) {
	open::ItemRef item;
	item.id = id;
	item.hand_use = open::HandUse::None;
	item.allowed_slots.set(static_cast<std::size_t>(static_cast<int>(open::Slot::Head)));
	return item;
}

open::ItemRef one_handed_sword(open::ItemId id) {
	open::ItemRef item;
	item.id = id;
	item.hand_use = open::HandUse::OneHanded;
	item.allowed_slots.set(static_cast<std::size_t>(static_cast<int>(open::Slot::LeftHand)));
	item.allowed_slots.set(static_cast<std::size_t>(static_cast<int>(open::Slot::RightHand)));
	return item;
}

}  // namespace

int main() {
	{
		// Break this catches: the factory letting an invalid sheet exist.
		expect_true("a valid config creates a character",
					open::Character::create(config(1, "Ann", open::Faction::Friendly, 7, open::Attribute::Luck))
						.has_value());
		expect_true("a zero id is refused",
					!open::Character::create(config(0, "", open::Faction::Friendly, 1, open::Attribute::Strength))
						 .has_value());
		expect_true("level zero is refused",
					!open::Character::create(config(1, "", open::Faction::Friendly, 0, open::Attribute::Strength))
						 .has_value());
		expect_true("level eight is refused",
					!open::Character::create(config(1, "", open::Faction::Friendly, 8, open::Attribute::Strength))
						 .has_value());
		expect_true("a bad faction is refused",
					!open::Character::create(config(1, "", static_cast<open::Faction>(7), 1,
													open::Attribute::Strength))
							 .has_value());
		expect_true("a bad blessing is refused",
					!open::Character::create(config(1, "", open::Faction::Enemy, 1,
													static_cast<open::Attribute>(10)))
							 .has_value());
	}
	{
		// Break this catches: identity being lost or a name being required.
		const std::optional<open::Character> made =
			open::Character::create(config(42, "", open::Faction::Enemy, 3, open::Attribute::Profanity));
		expect_true("created", made.has_value());
		const open::Character &c = *made;
		expect_true("id round trips", c.id() == 42u);
		expect_true("an empty name is allowed", c.name().empty());
		expect_true("faction round trips", c.faction() == open::Faction::Enemy);
		expect_true("level round trips", c.level() == 3);
		expect_true("blessing round trips", c.blessing() == open::Attribute::Profanity);
	}
	{
		// Break this catches: the spike's Strength 10 / four bools surviving as the
		// sheet, or a fresh character not starting full.
		const open::Character c = make_character();
		expect_close("every attribute base starts at one", c.base(open::Attribute::Strength), 1.0f);
		expect_close("a level-one blessed attribute is 1.25", c.attribute(open::Attribute::Strength), 1.25f);
		expect_close("an unblessed attribute is one", c.attribute(open::Attribute::Dexterity), 1.0f);
		expect_close("main hp starts full", c.pool(open::PoolKind::Hp).current, 100.0f);
		expect_close("head starts full", c.body_part(open::BodyPart::Head).current, 25.0f);
		expect_true("every basic skill starts at one", c.basic(open::BasicSkill::Stealth).rank == 1u);
		expect_true("nothing is specialised yet", !c.chosen(open::BasicSkill::Stealth).has_value());
		expect_true("no advanced track yet", !c.advanced(open::AdvancedSkill::Katana).has_value());
		expect_true("no titles yet", !c.has_title(open::Title::Duelist));
		expect_true("no title count yet", c.title_count(open::Title::Duelist) == 0u);
		expect_true("nothing is equipped", !c.equipped(open::Slot::Chest).has_value());
		expect_true("the backpack is empty", c.backpack_items().empty());
		expect_true("alive", c.alive());
		expect_true("the movement switch starts on", c.can_move());
		expect_true("can walk", c.can_walk());
		expect_close("walking speed is the spike's 4.5", c.walk_speed(), 4.5f);
		expect_true("can carry", c.can_carry());
		expect_true("can use one hand", c.can_use_one_hand());
		expect_true("can use two hands", c.can_use_two_hands());
	}
	{
		// Break this catches: two characters sharing a sheet.
		open::Character first = make_character(1);
		open::Character second = make_character(2);
		expect_true("distinct ids", first.id() != second.id());
		first.set_name("First");
		expect_true("one can be named without naming the other", second.name().empty());
		expect_true("the name is kept", first.name() == "First");
		expect_true("damage lands on one only", first.damage(open::BodyPart::Torso, 10.0f));
		expect_close("the other is untouched", second.body_part(open::BodyPart::Torso).current, 50.0f);
	}
	{
		// Break this catches: a skill formula that squares level, multiplies two
		// attributes, or applies a title twice.
		open::Character c = open::Character::create(config(1, "", open::Faction::Friendly, 7,
														   open::Attribute::Strength))
								.value();
		expect_close("one-handed at rank one is the attribute average",
					 c.effective_skill(open::BasicSkill::OneHandedWeapons), 112.5f);
		expect_close("a single-linked skill uses its one attribute",
					 c.effective_skill(open::BasicSkill::Endurance), 100.0f);
		for (int i = 0; i < 39600; ++i) {
			c.record_use(open::BasicSkill::OneHandedWeapons, std::nullopt, true);
		}
		expect_true("the parent reached its cap",
					c.basic(open::BasicSkill::OneHandedWeapons).rank == 100u);
		expect_close("the capped parent is the frozen fallback",
					 c.effective_skill(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Axe), 11250.0f);
		expect_true("katana is chosen", c.advance(open::AdvancedSkill::Katana));
		expect_close("the fresh specialisation is worth nothing yet",
					 c.effective_skill(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana), 0.0f);
		expect_true("the choice is reported",
					c.chosen(open::BasicSkill::OneHandedWeapons) ==
						std::optional<open::AdvancedSkill>(open::AdvancedSkill::Katana));
		expect_true("the specialisation has its own track",
					c.advanced(open::AdvancedSkill::Katana).has_value());
		for (int i = 0; i < 8; ++i) {
			expect_true("a katana use is recorded",
						c.record_use(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana, true));
		}
		expect_close("eight katana uses reach the attribute average",
					 c.effective_skill(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana), 112.5f);
		expect_close("the sibling job still uses the frozen basic",
					 c.effective_skill(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Axe), 11250.0f);
		for (int i = 0; i < 100; ++i) {
			c.record_title_progress(open::Title::Duelist, true);
		}
		expect_true("duelist is held", c.has_title(open::Title::Duelist));
		expect_close("the title multiplies the specialised job once",
					 c.effective_skill(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana), 123.75f);
		expect_close("and the fallback job once",
					 c.effective_skill(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Axe), 12375.0f);
		expect_close("the bare query uses the parent title too",
					 c.effective_skill(open::BasicSkill::OneHandedWeapons), 12375.0f);
		expect_close("an unrelated skill is untouched", c.effective_skill(open::BasicSkill::Stealth), 100.0f);
		expect_true("the specialist target bonus is separate", c.target_multiplier(false) == 1.0f);
		expect_true("and absent before the title", c.target_multiplier(true) == 1.0f);
	}
	{
		// Break this catches: level or a specialisation leaking into a pool.
		open::Character c = open::Character::create(config(1, "", open::Faction::Friendly, 7,
														   open::Attribute::Strength))
								.value();
		expect_close("level seven does not stretch the hp bar", c.pool(open::PoolKind::Hp).maximum, 100.0f);
		for (int i = 0; i < 39600; ++i) {
			c.record_use(open::BasicSkill::ManaControl, std::nullopt, true);
		}
		expect_true("mana control capped", c.basic(open::BasicSkill::ManaControl).rank == 100u);
		expect_close("mana max follows the frozen basic counter",
					 c.pool(open::PoolKind::Mana).maximum, 10000.0f);
		expect_true("a mana specialisation is chosen", c.advance(open::AdvancedSkill::Channeling));
		expect_close("choosing it leaves the mana bar alone",
					 c.pool(open::PoolKind::Mana).maximum, 10000.0f);
	}
	{
		// Break this catches: training Endurance refilling the stamina bar.
		open::Character c = make_character();
		expect_true("a sprint spends stamina", c.spend(open::ResourceUse::Sprint, 1.0f));
		expect_close("stamina is ninety", c.pool(open::PoolKind::Stamina).current, 90.0f);
		for (int i = 0; i < 8; ++i) {
			c.record_use(open::BasicSkill::Endurance, std::nullopt, true);
		}
		expect_true("endurance rose to two", c.basic(open::BasicSkill::Endurance).rank == 2u);
		expect_close("the stamina maximum followed", c.pool(open::PoolKind::Stamina).maximum, 200.0f);
		expect_close("the stamina current was preserved", c.pool(open::PoolKind::Stamina).current, 90.0f);
	}
	{
		// Break this catches: a base change that half-applies when a pool would
		// overflow.
		open::Character c = make_character();
		expect_true("an overflowing base change is refused",
					!c.set_base(open::Attribute::Constitution, 1.0e37f));
		expect_close("the base is unchanged", c.base(open::Attribute::Constitution), 1.0f);
		expect_close("the hp maximum is unchanged", c.pool(open::PoolKind::Hp).maximum, 100.0f);
		expect_close("the head maximum is unchanged", c.body_part(open::BodyPart::Head).maximum, 25.0f);
		expect_true("a legal base change is applied", c.set_base(open::Attribute::Constitution, 2.0f));
		expect_close("the hp maximum grew", c.pool(open::PoolKind::Hp).maximum, 200.0f);
		expect_close("current hp was not refilled", c.pool(open::PoolKind::Hp).current, 100.0f);
		expect_true("an invalid level is refused", !c.set_level(9));
		expect_true("the level is unchanged", c.level() == 1);
		expect_true("a legal level is applied", c.set_level(4));
		expect_close("the effective attribute followed the level (base 1 x 8 x 1.25 blessing)",
					 c.attribute(open::Attribute::Strength), 10.0f);
		expect_close("the pool did not follow the level", c.pool(open::PoolKind::Hp).maximum, 200.0f);
	}
	{
		// Break this catches: a failed action training anyway, or title progress
		// that is not gated on success.
		open::Character c = make_character();
		expect_true("a failed use is accepted",
					c.record_use(open::BasicSkill::Smithing, std::nullopt, false));
		expect_true("a failed use trains nothing", c.basic(open::BasicSkill::Smithing).rank == 1u);
		expect_true("a failed use leaves no residual", c.basic(open::BasicSkill::Smithing).xp == 0u);
		expect_true("a failed title report is accepted", c.record_title_progress(open::Title::Hexer, false));
		expect_true("a failed title report counts nothing", c.title_count(open::Title::Hexer) == 0u);
		expect_true("an invalid skill is refused",
					!c.record_use(static_cast<open::BasicSkill>(35), std::nullopt, true));
		expect_true("an invalid title is refused",
					!c.record_title_progress(static_cast<open::Title>(14), true));
		expect_true("an invalid advance is refused",
					!c.advance(static_cast<open::AdvancedSkill>(72)));
	}
	{
		// Break this catches: gear being lost on death, and a dead character acting.
		open::Character c = make_character();
		expect_true("armour picked up", c.add_item(chest_armour(101)));
		expect_true("sword picked up", c.add_item(one_handed_sword(202)));
		expect_true("helm picked up", c.add_item(head_armour(104)));
		expect_true("armour equipped", c.equip(101, open::Slot::Chest));
		expect_true("sword equipped", c.equip(202, open::Slot::LeftHand));
		expect_true("the sword is usable", c.held_usable(202));
		expect_true("one item is left in the backpack", c.backpack_items().size() == 1);

		expect_true("a lethal hit lands", c.damage(open::BodyPart::Torso, 50.0f));
		expect_true("dead", !c.alive());
		expect_true("gear is retained", c.equipped(open::Slot::Chest) == std::optional<open::ItemId>(101));
		expect_true("the held sword is retained",
					c.equipped(open::Slot::LeftHand) == std::optional<open::ItemId>(202));
		expect_true("a dead character cannot spend", !c.spend(open::ResourceUse::MeleeSwing));
		expect_true("a dead character cannot regenerate", !c.regenerate(10.0f, false));
		expect_true("a dead character cannot train",
					!c.record_use(open::BasicSkill::Smithing, std::nullopt, true));
		expect_true("training really did not happen", c.basic(open::BasicSkill::Smithing).rank == 1u);
		expect_true("a dead character cannot specialise",
					!c.advance(open::AdvancedSkill::Weaponsmith));
		expect_true("a dead character cannot pick things up", !c.add_item(chest_armour(103)));
		expect_true("a dead character cannot equip", !c.equip(104, open::Slot::Head));
		expect_true("a dead character cannot move into an empty slot either",
					!c.equip(104, open::Slot::Backpack));
		expect_true("a dead character cannot walk", !c.can_walk());
		expect_close("a dead character has no speed", c.walk_speed(), 0.0f);
		expect_close("a dead character does not move", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
		expect_true("reads still work", c.equipped(open::Slot::Chest).has_value());
		expect_true("the backpack can still be read", c.backpack_items().size() == 1);
		expect_true("and still holds the helm", c.backpack_items()[0] == 104u);
		expect_true("the skill sheet can still be read",
					c.effective_skill(open::BasicSkill::OneHandedWeapons) > 0.0f);
		expect_true("taking gear off is still possible", c.unequip(open::Slot::Chest));
		expect_true("and it lands in the backpack", c.backpack_items().size() == 2);
		expect_true("the held sword can come off too", c.unequip(open::Slot::LeftHand));
		expect_true("and it is usable no more", !c.held_usable(202));
	}
	{
		// Break this catches: movement rules that ignore the raw switch or the legs.
		open::Character c = make_character();
		expect_close("no order means no velocity", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
		expect_true("the destination query is empty", !c.destination().has_value());
		expect_true("a destination is accepted", c.set_destination(10.0f, 0.0f, 0.0f));
		expect_true("the destination is queryable", c.destination().has_value());
		expect_close("the destination is kept as given", c.destination()->x, 10.0f);
		const open::Vec3 walking = c.velocity_at(0.0f, 0.0f, 0.0f);
		expect_close("the healthy character walks at 4.5", walking.x, 4.5f);
		expect_close("and stays on the ground", walking.y, 0.0f);
		expect_close("with no sideways drift", walking.z, 0.0f);

		c.clear_order();
		expect_true("clearing removes the order", !c.destination().has_value());
		expect_close("clearing stops the character", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);

		expect_true("an exact arrive radius stops", c.set_destination(0.4f, 0.0f, 0.0f));
		expect_close("at 0.4 there is no velocity", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
		expect_true("just outside the radius moves", c.set_destination(0.41f, 0.0f, 0.0f));
		expect_true("just outside 0.4 there is velocity",
					c.velocity_at(0.0f, 0.0f, 0.0f).x > 0.0f);

		expect_true("a height in the order is allowed", c.set_destination(10.0f, 8.0f, 0.0f));
		const open::Vec3 flat = c.velocity_at(0.0f, 0.0f, 0.0f);
		expect_close("height is ignored on the way", flat.x, 4.5f);
		expect_close("height is never entered", flat.y, 0.0f);

		const float nan = std::numeric_limits<float>::quiet_NaN();
		expect_true("a nan destination is refused", !c.set_destination(nan, 0.0f, 0.0f));
		expect_close("the previous order survives", c.destination()->x, 10.0f);
		expect_close("and movement continues", c.velocity_at(0.0f, 0.0f, 0.0f).x, 4.5f);
		expect_close("a nan position yields no velocity", c.velocity_at(nan, 0.0f, 0.0f).x, 0.0f);
		expect_close("an infinite position yields no velocity",
					 c.velocity_at(std::numeric_limits<float>::infinity(), 0.0f, 0.0f).x, 0.0f);

		expect_true("one leg goes", c.damage(open::BodyPart::LeftLeg, 40.0f));
		expect_close("one leg halves the speed", c.walk_speed(), 2.25f);
		expect_close("one leg halves the velocity", c.velocity_at(0.0f, 0.0f, 0.0f).x, 2.25f);
		expect_true("still walking", c.can_walk());
		expect_true("the other leg goes", c.damage(open::BodyPart::RightLeg, 40.0f));
		expect_close("both legs stop walking", c.walk_speed(), 0.0f);
		expect_true("can_walk is false", !c.can_walk());
		expect_close("and the velocity is zero", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
		expect_true("the raw switch is still on", c.can_move());

		c.clear_order();
		c.set_destination(10.0f, 0.0f, 0.0f);
		c.set_can_move(false);
		expect_close("the raw switch off stops walking", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
		expect_true("the raw switch reads false while it is off", !c.can_move());
		c.set_can_move(true);
		expect_close("switch true cannot override legs", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
	}
	{
		// Break this catches: a switch that survives death, and death that lets the
		// switch keep the character walking.
		open::Character c = make_character();
		c.set_destination(10.0f, 0.0f, 0.0f);
		c.set_can_move(false);
		expect_close("the switch stops a healthy walker", c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
		expect_true("a head hit lands", c.damage(open::BodyPart::Head, 25.0f));
		expect_true("dead", !c.alive());
		expect_true("the switch is still readable", !c.can_move());
		c.set_can_move(true);
		expect_close("a dead character cannot walk even with the switch on",
					 c.velocity_at(0.0f, 0.0f, 0.0f).x, 0.0f);
		expect_true("the raw switch was turned on", c.can_move());
		expect_true("can_walk is false for the dead", !c.can_walk());
	}
	{
		// Break this catches: an arm loss that strips gear or keeps two-handing.
		open::Character c = make_character();
		expect_true("hammer-ish sword picked up", c.add_item(one_handed_sword(202)));
		expect_true("equipped in the left hand", c.equip(202, open::Slot::LeftHand));
		expect_true("left arm goes", c.damage(open::BodyPart::LeftArm, 40.0f));
		expect_true("two-handing is forbidden", !c.can_use_two_hands());
		expect_true("one hand still works", c.can_use_one_hand());
		expect_true("carrying still works", c.can_carry());
		expect_true("the sword is still held and usable", c.held_usable(202));
		expect_true("right arm goes too", c.damage(open::BodyPart::RightArm, 40.0f));
		expect_true("carrying is gone", !c.can_carry());
		expect_true("held use is gone", !c.can_use_one_hand());
		expect_true("the sword is still in the hand",
					c.equipped(open::Slot::LeftHand) == std::optional<open::ItemId>(202));
		expect_true("but it cannot be used", !c.held_usable(202));
		expect_true("it can still come off", c.unequip(open::Slot::LeftHand));
		expect_true("and it is kept", c.backpack_items().size() == 1);
		expect_true("a new pickup is impossible", !c.add_item(chest_armour(105)));
	}

	return test_summary();
}
