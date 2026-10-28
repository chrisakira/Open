#pragma once

#include "open/character_rules.hpp"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace open {

// Item identity only. What an item is (damage, armour value, weight) belongs to
// the future item system; this container needs just enough to know where an item
// may go and how it is held.
using ItemId = std::uint64_t;

enum class HandUse {
	None,
	OneHanded,
	TwoHanded,
	Count,
};

bool valid(HandUse use);

struct ItemRef {
	ItemId id = 0;
	std::bitset<static_cast<std::size_t>(Slot::Count)> allowed_slots;
	HandUse hand_use = HandUse::None;
};

// What a character carries plus what it wears. A two-handed item is stored once,
// in a fixed primary held slot, and the other held slot reports the same id.
// There is no capacity, no weight and no automatic swapping.
class Equipment {
public:
	// The caller passes the character's current capabilities; Equipment does not
	// own vitals.
	bool add(const ItemRef &item, bool can_carry);
	bool equip(ItemId id, Slot slot, bool one_hand, bool two_hands);
	bool unequip(Slot slot);

	// Checked getter: throws std::invalid_argument for a bad slot id.
	std::optional<ItemId> equipped(Slot slot) const;

	std::vector<ItemId> backpack_items() const;
	bool held_usable(ItemId id, bool one_hand, bool two_hands) const;

private:
	static constexpr Slot kTwoHandedPrimary = Slot::LeftHand;

	bool coherent(const ItemRef &item) const;
	bool owned_anywhere(ItemId id) const;
	std::optional<Slot> holder_of(ItemId id) const;
	std::optional<Slot> hand_holder(Slot slot) const;  // follows a two-handed reservation

	std::vector<ItemRef> stored_;
	std::array<std::optional<ItemRef>, static_cast<std::size_t>(Slot::Count)> slots_{};
};

}  // namespace open
