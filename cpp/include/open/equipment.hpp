#pragma once

#include "open/item_instance.hpp"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace open {

// ItemId, ItemRef and ItemInstance come from items.hpp and item_instance.hpp:
// identity and definition first, then the owned record, then this container, so
// the container is the only thing that knows about slots.

// What a character carries plus what it wears. A two-handed item is stored once,
// in a fixed primary held slot, and the other held slot reports the same id.
// There is no capacity, no weight and no automatic swapping.
class Equipment {
public:
	// The caller passes the character's current capabilities; Equipment does not
	// own vitals.
	bool add(const ItemRef &item, bool can_carry);

	// An owned instance, taken apart from any catalog: it carries quality and
	// durability alongside the placement record, and it is stored whole. Passing a
	// bare ItemRef through add() still works and yields a record with no
	// definition, which is what keeps placement-only items statless.
	bool add_instance(const ItemInstance &item, bool can_carry);

	bool equip(ItemId id, Slot slot, bool one_hand, bool two_hands);
	bool unequip(Slot slot);

	// Checked getter: throws std::invalid_argument for a bad slot id.
	std::optional<ItemId> equipped(Slot slot) const;

	std::vector<ItemId> backpack_items() const;
	bool held_usable(ItemId id, bool one_hand, bool two_hands) const;

	// Read-only lookup of the record this container owns, whether it is stored or
	// worn. A two-handed item keeps one record, so both held views find the same
	// one. Unknown and zero ids are empty; callers never get a reference into the
	// container.
	std::optional<ItemRef> item(ItemId id) const;

	// The owned record itself, copied out: mutating the copy cannot wear, repair
	// or rename what this container owns. Wear and repair are the only ways to
	// change durability, and they are refused for a record that has no definition
	// and therefore no durability to change.
	std::optional<ItemInstance> instance(ItemId id) const;

	// Fixes an enchantment to an owned item, permanently. Only a weapon has slots, and
	// a tier has exactly as many as its number, so the next application past that is
	// refused. There is deliberately no counterpart to this call: an enchantment is
	// never removed, replaced or expired, and its values came from the bench, so a
	// caller cannot edit them here either. Settlement code only - it is not a way for a
	// player to enchant anything, which is what the workbench action is for.
	bool attach_enchantment(ItemId id, const AppliedEnchantment &enchantment);

	bool apply_wear(ItemId id, unsigned amount);
	bool restore_durability(ItemId id, unsigned amount);

private:
	// The one canonical record for an id, whether carried or worn. Never handed out:
	// wear and repair mutate through it, callers only ever get a copy.
	ItemInstance *find_owned(ItemId id);
	static constexpr Slot kTwoHandedPrimary = Slot::LeftHand;

	bool coherent(const ItemRef &ref) const;
	bool owned_anywhere(ItemId id) const;
	std::optional<Slot> holder_of(ItemId id) const;
	std::optional<Slot> hand_holder(Slot slot) const;  // follows a two-handed reservation

	// One canonical record per owned item: either carried or worn, never both, and
	// a two-handed reservation names the same record from both hands.
	std::vector<ItemInstance> stored_;
	std::array<std::optional<ItemInstance>, static_cast<std::size_t>(Slot::Count)> slots_{};
};

}  // namespace open
