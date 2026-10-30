#include "open/equipment.hpp"

#include <stdexcept>

namespace open {
namespace {

std::size_t slot_index(Slot slot) {
	return static_cast<std::size_t>(static_cast<int>(slot));
}

bool is_held(Slot slot) {
	return slot == Slot::LeftHand || slot == Slot::RightHand;
}

Slot other_hand(Slot slot) {
	return slot == Slot::LeftHand ? Slot::RightHand : Slot::LeftHand;
}

std::bitset<static_cast<std::size_t>(Slot::Count)> held_mask() {
	std::bitset<static_cast<std::size_t>(Slot::Count)> mask;
	mask.set(slot_index(Slot::LeftHand));
	mask.set(slot_index(Slot::RightHand));
	return mask;
}

bool allows(const ItemRef &item, Slot slot) {
	return slot_index(slot) < item.allowed_slots.size() && item.allowed_slots.test(slot_index(slot));
}

// The melee basics an item may be tagged with. Archery is deliberately absent:
// no ranged weapons exist yet, and a tag must not invent one.
bool melee_tag(BasicSkill skill) {
	return skill == BasicSkill::OneHandedWeapons || skill == BasicSkill::TwoHandedWeapons ||
		   skill == BasicSkill::Polearms || skill == BasicSkill::Blocking;
}

bool held_by_hand(const ItemRef &item, HandUse use) {
	return item.hand_use == use;
}

// A tag says what job the item is for, so a malformed tag would silently invent
// a weapon, a shield or a specialisation. Refused as a whole record: an advanced
// tag needs its own basic, the basic must be a melee one, and the hand use must
// match the job (a shield is one-handed, a polearm is not).
bool tagged_ok(const ItemRef &item) {
	if (!item.combat_option.has_value() && !item.combat_skill.has_value()) {
		return true;  // untagged records stay valid
	}
	if (!item.combat_skill.has_value()) {
		return false;
	}
	const BasicSkill basic = *item.combat_skill;
	if (!valid(basic) || !melee_tag(basic)) {
		return false;
	}
	if (item.combat_option.has_value()) {
		if (!valid(*item.combat_option) || advanced_rule(*item.combat_option).parent != basic) {
			return false;
		}
	}
	if (basic == BasicSkill::OneHandedWeapons || basic == BasicSkill::Blocking) {
		return held_by_hand(item, HandUse::OneHanded);
	}
	return held_by_hand(item, HandUse::TwoHanded);
}

}  // namespace

bool valid(HandUse use) {
	const int id = static_cast<int>(use);
	return id >= 0 && id < static_cast<int>(HandUse::Count);
}

bool Equipment::coherent(const ItemRef &item) const {
	if (item.id == 0) {
		return false;
	}
	if (!valid(item.hand_use)) {
		return false;
	}
	if (!item.allowed_slots.any()) {
		return false;
	}
	if (!tagged_ok(item)) {
		return false;
	}
	const std::bitset<static_cast<std::size_t>(Slot::Count)> held = held_mask();
	switch (item.hand_use) {
	case HandUse::None:
		return true;  // armour, rings, bags: any slots the record names
	case HandUse::OneHanded:
		return (item.allowed_slots & ~held).none() && (item.allowed_slots & held).any();
	case HandUse::TwoHanded:
		return (item.allowed_slots & ~held).none() && (item.allowed_slots & held) == held;
	case HandUse::Count:
		return false;
	}
	return false;
}

bool Equipment::owned_anywhere(ItemId id) const {
	if (id == 0) {
		return false;
	}
	for (const ItemRef &item : stored_) {
		if (item.id == id) {
			return true;
		}
	}
	for (const std::optional<ItemRef> &slot : slots_) {
		if (slot.has_value() && slot->id == id) {
			return true;
		}
	}
	return false;
}

std::optional<Slot> Equipment::holder_of(ItemId id) const {
	if (id == 0) {
		return std::nullopt;
	}
	for (std::size_t i = 0; i < slots_.size(); ++i) {
		if (slots_[i].has_value() && slots_[i]->id == id) {
			return static_cast<Slot>(static_cast<int>(i));
		}
	}
	return std::nullopt;
}

std::optional<Slot> Equipment::hand_holder(Slot slot) const {
	if (!is_held(slot)) {
		return std::nullopt;
	}
	if (slots_[slot_index(slot)].has_value()) {
		return slot;
	}
	// A two-handed item lives in one slot and reserves the other.
	const Slot neighbour = other_hand(slot);
	const std::optional<ItemRef> &entry = slots_[slot_index(neighbour)];
	if (entry.has_value() && entry->hand_use == HandUse::TwoHanded) {
		return neighbour;
	}
	return std::nullopt;
}

bool Equipment::add(const ItemRef &item, bool can_carry) {
	if (!can_carry) {
		return false;
	}
	if (!coherent(item)) {
		return false;
	}
	if (owned_anywhere(item.id)) {
		return false;
	}
	stored_.push_back(item);
	return true;
}

std::optional<ItemId> Equipment::equipped(Slot slot) const {
	if (!valid(slot)) {
		throw std::invalid_argument("Equipment::equipped: unknown slot id");
	}
	if (is_held(slot)) {
		const std::optional<Slot> holder = hand_holder(slot);
		if (!holder.has_value()) {
			return std::nullopt;
		}
		return slots_[slot_index(*holder)]->id;
	}
	const std::optional<ItemRef> &entry = slots_[slot_index(slot)];
	if (!entry.has_value()) {
		return std::nullopt;
	}
	return entry->id;
}

bool Equipment::equip(ItemId id, Slot slot, bool one_hand, bool two_hands) {
	if (id == 0 || !valid(slot)) {
		return false;
	}
	std::size_t stored_at = stored_.size();
	for (std::size_t i = 0; i < stored_.size(); ++i) {
		if (stored_[i].id == id) {
			stored_at = i;
			break;
		}
	}
	if (stored_at == stored_.size()) {
		return false;  // not carried, or already worn: never a second owner
	}

	const ItemRef item = stored_[stored_at];
	if (!coherent(item) || !allows(item, slot)) {
		return false;
	}
	if (item.hand_use != HandUse::None && !is_held(slot)) {
		return false;  // a held type cannot be worn as armour
	}

	Slot target = slot;
	if (is_held(slot)) {
		if (item.hand_use == HandUse::TwoHanded) {
			if (!two_hands) {
				return false;
			}
			if (slots_[slot_index(Slot::LeftHand)].has_value() ||
				slots_[slot_index(Slot::RightHand)].has_value()) {
				return false;  // both hands must be free
			}
			target = kTwoHandedPrimary;
		} else {
			if (!one_hand) {
				return false;
			}
			if (slots_[slot_index(slot)].has_value()) {
				return false;
			}
			if (hand_holder(slot).has_value()) {
				return false;  // the other hand is busy with a two-handed item
			}
		}
	} else if (slots_[slot_index(slot)].has_value()) {
		return false;  // occupied: the caller unequips first
	}

	slots_[slot_index(target)] = item;
	stored_.erase(stored_.begin() + static_cast<std::ptrdiff_t>(stored_at));
	return true;
}

bool Equipment::unequip(Slot slot) {
	if (!valid(slot)) {
		return false;
	}
	Slot target = slot;
	if (is_held(slot)) {
		const std::optional<Slot> holder = hand_holder(slot);
		if (!holder.has_value()) {
			return false;
		}
		target = *holder;
	}
	std::optional<ItemRef> &entry = slots_[slot_index(target)];
	if (!entry.has_value()) {
		return false;
	}
	stored_.push_back(*entry);
	entry.reset();
	return true;
}

std::vector<ItemId> Equipment::backpack_items() const {
	std::vector<ItemId> ids;
	ids.reserve(stored_.size());
	for (const ItemRef &item : stored_) {
		ids.push_back(item.id);
	}
	return ids;
}

std::optional<ItemRef> Equipment::item(ItemId id) const {
	if (id == 0) {
		return std::nullopt;
	}
	// Worn first: an equipped record is the one a held-item query needs.
	for (const std::optional<ItemRef> &entry : slots_) {
		if (entry.has_value() && entry->id == id) {
			return *entry;
		}
	}
	for (const ItemRef &entry : stored_) {
		if (entry.id == id) {
			return entry;
		}
	}
	return std::nullopt;
}

bool Equipment::held_usable(ItemId id, bool one_hand, bool two_hands) const {
	const std::optional<Slot> holder = holder_of(id);
	if (!holder.has_value() || !is_held(*holder)) {
		return false;
	}
	const ItemRef &item = *slots_[slot_index(*holder)];
	if (item.hand_use == HandUse::TwoHanded) {
		return two_hands;
	}
	return one_hand;
}

}  // namespace open
