#include "open/equipment.hpp"

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <vector>

#include "open/character_rules.hpp"
#include "test_support.hpp"

// Task 6 tests. Break these catches: an item owned twice, a two-handed weapon
// stored in both hands, a slot that accepts an item its record forbids, a
// failed transfer that loses the source item, or a disabled arm that strips
// gear instead of only blocking use.

namespace {

constexpr open::ItemId kInvalidId = 0;

open::ItemRef make_item(open::ItemId id, std::initializer_list<open::Slot> slots,
						open::HandUse use = open::HandUse::None) {
	open::ItemRef ref;
	ref.id = id;
	ref.hand_use = use;
	for (const open::Slot slot : slots) {
		ref.allowed_slots.set(static_cast<std::size_t>(static_cast<int>(slot)));
	}
	return ref;
}

open::ItemRef chest(int id) {
	return make_item(static_cast<open::ItemId>(id), {open::Slot::Chest});
}

open::ItemRef sword(int id) {
	return make_item(static_cast<open::ItemId>(id), {open::Slot::LeftHand, open::Slot::RightHand},
					 open::HandUse::OneHanded);
}

open::ItemRef hammer(int id) {
	return make_item(static_cast<open::ItemId>(id), {open::Slot::LeftHand, open::Slot::RightHand},
					 open::HandUse::TwoHanded);
}

open::ItemRef gloves(int id) {
	return make_item(static_cast<open::ItemId>(id), {open::Slot::Hands});
}

open::ItemRef bag(int id) {
	return make_item(static_cast<open::ItemId>(id), {open::Slot::Backpack});
}

open::Equipment loaded() {
	open::Equipment equipment;
	expect_true("chest piece added", equipment.add(chest(101), true));
	expect_true("sword added", equipment.add(sword(202), true));
	expect_true("hammer added", equipment.add(hammer(303), true));
	expect_true("gloves added", equipment.add(gloves(404), true));
	expect_true("bag added", equipment.add(bag(505), true));
	return equipment;
}

std::vector<int> all_slots() {
	std::vector<int> ids;
	for (int i = 0; i < static_cast<int>(open::Slot::Count); ++i) {
		ids.push_back(i);
	}
	return ids;
}

bool is_held_slot(int slot) {
	return slot == static_cast<int>(open::Slot::LeftHand) || slot == static_cast<int>(open::Slot::RightHand);
}

}  // namespace

int main() {
	{
		// Break this catches: a slot that starts occupied.
		const open::Equipment equipment;
		for (const int slot : all_slots()) {
			expect_true("every slot starts empty",
						!equipment.equipped(static_cast<open::Slot>(slot)).has_value());
		}
		expect_true("the backpack starts empty", equipment.backpack_items().empty());
	}
	{
		// Break this catches: a slot that cannot be worn in and out again.
		for (const int slot : all_slots()) {
			if (is_held_slot(slot)) {
				continue;
			}
			open::Equipment equipment;
			const open::ItemId id = static_cast<open::ItemId>(1000 + slot);
			expect_true("an item for this slot is accepted",
						equipment.add(make_item(id, {static_cast<open::Slot>(slot)}), true));
			expect_true("the item can be equipped",
						equipment.equip(id, static_cast<open::Slot>(slot), true, true));
			expect_true("the slot reports it",
						equipment.equipped(static_cast<open::Slot>(slot)) == std::optional<open::ItemId>(id));
			expect_true("the item left the backpack", equipment.backpack_items().empty());
			expect_true("the item can be unequipped", equipment.unequip(static_cast<open::Slot>(slot)));
			expect_true("the slot is empty again",
						!equipment.equipped(static_cast<open::Slot>(slot)).has_value());
			expect_true("the item is back in the backpack", equipment.backpack_items().size() == 1);
			expect_true("and it is the same item", equipment.backpack_items()[0] == id);
		}
	}
	{
		// Break this catches: gloves being treated as a held weapon, or one held
		// slot being the other.
		open::Equipment equipment = loaded();
		expect_true("gloves equipped", equipment.equip(404, open::Slot::Hands, true, true));
		expect_true("sword equipped", equipment.equip(202, open::Slot::LeftHand, true, true));
		expect_true("gloves stay in their own slot",
					equipment.equipped(open::Slot::Hands) == std::optional<open::ItemId>(404));
		expect_true("the left hand holds the sword",
					equipment.equipped(open::Slot::LeftHand) == std::optional<open::ItemId>(202));
		expect_true("the right hand is still free", !equipment.equipped(open::Slot::RightHand).has_value());
		expect_true("gloves unequipped", equipment.unequip(open::Slot::Hands));
		expect_true("the sword survived the gloves coming off",
					equipment.equipped(open::Slot::LeftHand) == std::optional<open::ItemId>(202));
	}
	{
		// Break this catches: an item being owned twice.
		open::Equipment equipment = loaded();
		expect_true("a duplicate id is refused", !equipment.add(chest(101), true));
		expect_true("the original is still there", equipment.backpack_items().size() == 5);
		expect_true("an equipped id is refused as a duplicate",
					(equipment.equip(101, open::Slot::Chest, true, true) && !equipment.add(chest(101), true)));
		expect_true("nothing was duplicated into the backpack", equipment.backpack_items().size() == 4);
		expect_true("a zero id is refused", !equipment.add(make_item(kInvalidId, {open::Slot::Chest}), true));
		expect_true("an item with no legal slot is refused", !equipment.add(make_item(900, {}), true));
		expect_true("a held record that names a non-held slot is refused",
					!equipment.add(make_item(901, {open::Slot::Chest, open::Slot::LeftHand},
											 open::HandUse::OneHanded),
								   true));
		expect_true("a two-handed record that names one hand is refused",
					!equipment.add(make_item(902, {open::Slot::LeftHand}, open::HandUse::TwoHanded), true));
		expect_true("the refused records did not enter the backpack",
					equipment.backpack_items().size() == 4);
	}
	{
		// Break this catches: a failed transfer destroying the source item.
		open::Equipment equipment = loaded();
		expect_true("a second chest piece is available", equipment.add(chest(111), true));
		expect_true("chest piece equipped", equipment.equip(101, open::Slot::Chest, true, true));
		expect_true("equipping into an occupied slot fails",
					!equipment.equip(111, open::Slot::Chest, true, true));
		expect_true("the refused item is still in the backpack",
					std::find(equipment.backpack_items().begin(), equipment.backpack_items().end(), 111) !=
						equipment.backpack_items().end());
		expect_true("the equipped item was not lost",
					equipment.equipped(open::Slot::Chest) == std::optional<open::ItemId>(101));
		expect_true("the sword is still carried", equipment.equip(202, open::Slot::LeftHand, true, true));
		expect_true("an id that is not carried cannot be equipped",
					!equipment.equip(999, open::Slot::Chest, true, true));
		expect_true("an equipped id cannot be equipped again elsewhere",
					!equipment.equip(202, open::Slot::Chest, true, true));
		expect_true("the sword is where it was",
					equipment.equipped(open::Slot::LeftHand) == std::optional<open::ItemId>(202));
		expect_true("a held item cannot go into armour slots",
					!equipment.equip(202, open::Slot::Feet, true, true));
		expect_true("a held item cannot go into the backpack slot",
					!equipment.equip(202, open::Slot::Backpack, true, true));
		expect_true("an incompatible slot target is refused",
					!equipment.equip(101, open::Slot::Feet, true, true));
		expect_true("nothing moved out of the backpack", equipment.backpack_items().size() == 4);
	}
	{
		// Break this catches: a pickup that should be impossible without an arm.
		open::Equipment equipment;
		expect_true("a pickup with a usable arm is accepted", equipment.add(chest(101), true));
		expect_true("a pickup without any usable arm is refused", !equipment.add(chest(102), false));
		expect_true("the refused pickup left one item", equipment.backpack_items().size() == 1);
		expect_true("an equipped item is unequippable without arms",
					(equipment.equip(101, open::Slot::Chest, false, false) && equipment.unequip(open::Slot::Chest)));
		expect_true("the item came back", equipment.backpack_items().size() == 1);
		expect_true("it is the same item", equipment.backpack_items()[0] == 101);
	}
	{
		// Break this catches: a two-handed weapon needing one arm, or being stored
		// in both hands.
		open::Equipment equipment = loaded();
		expect_true("a two-handed weapon needs two usable arms",
					!equipment.equip(303, open::Slot::LeftHand, true, false));
		expect_true("still carried, not lost", equipment.backpack_items().size() == 5);
		expect_true("equipping with two usable arms works",
					equipment.equip(303, open::Slot::LeftHand, true, true));
		expect_true("the primary held slot reports it",
					equipment.equipped(open::Slot::LeftHand) == std::optional<open::ItemId>(303));
		expect_true("the other held query reports the same single id",
					equipment.equipped(open::Slot::RightHand) == std::optional<open::ItemId>(303));
		expect_true("it is stored once, so nothing was duplicated",
					 equipment.backpack_items().size() == 4);
		expect_true("the right hand cannot also hold something while the hammer is up",
					!equipment.equip(202, open::Slot::RightHand, true, true));
		expect_true("and the sword is still in the backpack", equipment.backpack_items().size() == 4);
		expect_true("unequipping the off hand releases the whole weapon",
					equipment.unequip(open::Slot::RightHand));
		expect_true("the primary slot is clear too", !equipment.equipped(open::Slot::LeftHand).has_value());
		expect_true("exactly one entry returned to the backpack", equipment.backpack_items().size() == 5);
		expect_true("and it is the hammer",
					std::find(equipment.backpack_items().begin(), equipment.backpack_items().end(), 303) !=
						equipment.backpack_items().end());
	}
	{
		// Break this catches: a two-handed weapon squeezing into a hand that is
		// already busy.
		open::Equipment equipment = loaded();
		expect_true("a sword takes the right hand", equipment.equip(202, open::Slot::RightHand, true, true));
		expect_true("the hammer cannot be raised alongside it",
					!equipment.equip(303, open::Slot::LeftHand, true, true));
		expect_true("the sword is untouched",
					equipment.equipped(open::Slot::RightHand) == std::optional<open::ItemId>(202));
		expect_true("the hammer is still carried", equipment.backpack_items().size() == 4);
		expect_true("the sword came out", equipment.unequip(open::Slot::RightHand));
		expect_true("now the hammer fits", equipment.equip(303, open::Slot::LeftHand, true, true));
	}
	{
		// Break this catches: a same-side requirement nobody specified, and one
		// usable arm being enough.
		open::Equipment equipment = loaded();
		expect_true("one usable arm is enough for the left hand",
					equipment.equip(202, open::Slot::LeftHand, true, false));
		open::Equipment other = loaded();
		expect_true("one usable arm is enough for the right hand",
					other.equip(202, open::Slot::RightHand, true, false));
		expect_true("no arm at all forbids a held item",
					!equipment.equip(303, open::Slot::LeftHand, false, false));
	}
	{
		// Break this catches: a disabled arm stripping gear instead of blocking use.
		open::Equipment equipment = loaded();
		expect_true("gloves stay on the arm", equipment.equip(404, open::Slot::Hands, true, true));
		expect_true("sword in hand", equipment.equip(202, open::Slot::LeftHand, true, true));
		expect_true("the sword is usable with two arms", equipment.held_usable(202, true, true));
		expect_true("one dead arm still leaves the sword equipped",
					equipment.equipped(open::Slot::LeftHand) == std::optional<open::ItemId>(202));
		expect_true("one arm forbids a held two-handed use", !equipment.held_usable(202, false, false));
		expect_true("one arm still allows the one-handed sword", equipment.held_usable(202, true, false));
		expect_true("gear can still come off", equipment.unequip(open::Slot::LeftHand));
		expect_true("and it lands in the backpack", equipment.backpack_items().size() == 4);
		expect_true("gloves are still equipped on the disabled arm",
					equipment.equipped(open::Slot::Hands) == std::optional<open::ItemId>(404));
	}
	{
		// Break this catches: held_usable answering for an item that is not held.
		open::Equipment equipment = loaded();
		expect_true("the sword is held", equipment.equip(202, open::Slot::LeftHand, true, true));
		expect_true("a carried but sheathed item is not usable as held", !equipment.held_usable(101, true, true));
		expect_true("an unknown id is not usable", !equipment.held_usable(999, true, true));
		expect_true("a two-handed weapon is not usable with one arm",
					(equipment.unequip(open::Slot::LeftHand) && equipment.equip(303, open::Slot::LeftHand, true, true) &&
					 !equipment.held_usable(303, true, false)));
		expect_true("and it is usable with two", equipment.held_usable(303, true, true));
	}
	{
		// Break this catches: the bag being the storage, instead of a slot.
		open::Equipment equipment;
		expect_true("first item", equipment.add(chest(101), true));
		expect_true("second item", equipment.add(chest(102), true));
		expect_true("the bag", equipment.add(bag(505), true));
		expect_true("the bag is equipped", equipment.equip(505, open::Slot::Backpack, true, true));
		expect_true("contents stay stored while the bag is worn", equipment.backpack_items().size() == 2);
		expect_true("the bag comes off", equipment.unequip(open::Slot::Backpack));
		expect_true("contents are still there", equipment.backpack_items().size() == 3);
		expect_true("the bag itself came back with them",
					 std::find(equipment.backpack_items().begin(), equipment.backpack_items().end(), 505) !=
						 equipment.backpack_items().end());
		expect_true("no capacity was invented for them", equipment.backpack_items().size() == 3);
	}
	{
		// Break this catches: an unchecked slot id indexing the array.
		open::Equipment equipment = loaded();
		expect_throws_invalid("equipped rejects a bad slot id",
							  [&equipment] { equipment.equipped(static_cast<open::Slot>(20)); });
		expect_throws_invalid("equipped rejects a negative slot id",
							  [&equipment] { equipment.equipped(static_cast<open::Slot>(-1)); });
		expect_true("equip rejects a bad slot id",
					!equipment.equip(101, static_cast<open::Slot>(20), true, true));
		expect_true("equip rejects a negative slot id",
					!equipment.equip(101, static_cast<open::Slot>(-1), true, true));
		expect_true("unequip rejects a bad slot id", !equipment.unequip(static_cast<open::Slot>(20)));
		expect_true("equip rejects a zero id", !equipment.equip(kInvalidId, open::Slot::Chest, true, true));
		expect_true("unequip on an empty slot is refused", !equipment.unequip(open::Slot::Chest));
		expect_true("unequip on the reserved off hand is refused", !equipment.unequip(open::Slot::RightHand));
		expect_true("nothing changed", equipment.backpack_items().size() == 5);
	}

	return test_summary();
}
