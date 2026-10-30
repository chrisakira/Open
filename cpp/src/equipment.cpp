#include "open/equipment.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>

#include "open/item_instance.hpp"

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

// A tag says what job the item is for, so a malformed tag would silently invent
// a weapon, a shield or a specialisation. Refused as a whole record: an advanced
// tag needs its own basic, and the basic must be a melee one.
//
// Which hand use the job "should" have is not this check's business: family and
// handedness are independent, so a one-handed polearm is a real item. What is
// still refused is a tag on something no hand can hold, which would be a weapon
// nobody can swing. The limb gates that need two hands live in equip() and read
// the record's own hand use.
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
	return item.hand_use != HandUse::None;
}

// A record is a made item only when its definition, tier and durability band all
// agree. A placement-only record has none of them and stays that way.
bool carries_item_state(const ItemInstance &item) {
	return item.definition != 0;
}

bool instance_ok(const ItemInstance &item) {
	if (!carries_item_state(item)) {
		return true;  // a bare placement record: the legacy path
	}
	if (item.quality < 1 || item.quality > kQualityTierCount || item.max_durability == 0 ||
		item.durability > item.max_durability) {
		return false;
	}
	return item.material != 0;
}

}  // namespace

bool Equipment::coherent(const ItemRef &ref) const {
	if (ref.id == 0) {
		return false;
	}
	if (!valid(ref.hand_use)) {
		return false;
	}
	if (!ref.allowed_slots.any()) {
		return false;
	}
	if (!tagged_ok(ref)) {
		return false;
	}
	const std::bitset<static_cast<std::size_t>(Slot::Count)> held = held_mask();
	switch (ref.hand_use) {
	case HandUse::None:
		return true;  // armour, rings, bags: any slots the record names
	case HandUse::OneHanded:
		return (ref.allowed_slots & ~held).none() && (ref.allowed_slots & held).any();
	case HandUse::TwoHanded:
		return (ref.allowed_slots & ~held).none() && (ref.allowed_slots & held) == held;
	case HandUse::Count:
		return false;
	}
	return false;
}

bool Equipment::owned_anywhere(ItemId id) const {
	if (id == 0) {
		return false;
	}
	for (const ItemInstance &item : stored_) {
		if (item.ref.id == id) {
			return true;
		}
	}
	for (const std::optional<ItemInstance> &slot : slots_) {
		if (slot.has_value() && slot->ref.id == id) {
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
		if (slots_[i].has_value() && slots_[i]->ref.id == id) {
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
	// A two-handed item lives in one slot and reserves the other: one record, two
	// views of it, which is why wear charges it once.
	const Slot neighbour = other_hand(slot);
	const std::optional<ItemInstance> &entry = slots_[slot_index(neighbour)];
	if (entry.has_value() && entry->ref.hand_use == HandUse::TwoHanded) {
		return neighbour;
	}
	return std::nullopt;
}

ItemInstance *Equipment::find_owned(ItemId id) {
	if (id == 0) {
		return nullptr;
	}
	for (ItemInstance &item : stored_) {
		if (item.ref.id == id) {
			return &item;
		}
	}
	for (std::optional<ItemInstance> &slot : slots_) {
		if (slot.has_value() && slot->ref.id == id) {
			return &*slot;
		}
	}
	return nullptr;
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
	ItemInstance record;
	record.ref = item;
	stored_.push_back(record);
	return true;
}

bool Equipment::add_instance(const ItemInstance &item, bool can_carry) {
	if (!can_carry) {
		return false;
	}
	if (!coherent(item.ref) || !instance_ok(item)) {
		return false;
	}
	if (owned_anywhere(item.ref.id)) {
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
		return slots_[slot_index(*holder)]->ref.id;
	}
	const std::optional<ItemInstance> &entry = slots_[slot_index(slot)];
	if (!entry.has_value()) {
		return std::nullopt;
	}
	return entry->ref.id;
}

bool Equipment::equip(ItemId id, Slot slot, bool one_hand, bool two_hands) {
	if (id == 0 || !valid(slot)) {
		return false;
	}
	std::size_t stored_at = stored_.size();
	for (std::size_t i = 0; i < stored_.size(); ++i) {
		if (stored_[i].ref.id == id) {
			stored_at = i;
			break;
		}
	}
	if (stored_at == stored_.size()) {
		return false;  // not carried, or already worn: never a second owner
	}

	const ItemInstance item = stored_[stored_at];
	if (!coherent(item.ref) || !allows(item.ref, slot)) {
		return false;
	}
	if (item.ref.hand_use != HandUse::None && !is_held(slot)) {
		return false;  // a held type cannot be worn as armour
	}

	Slot target = slot;
	if (is_held(slot)) {
		if (item.ref.hand_use == HandUse::TwoHanded) {
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

	// The whole record moves, so quality and durability survive the trip.
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
	std::optional<ItemInstance> &entry = slots_[slot_index(target)];
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
	for (const ItemInstance &item : stored_) {
		ids.push_back(item.ref.id);
	}
	return ids;
}

std::optional<ItemRef> Equipment::item(ItemId id) const {
	const std::optional<ItemInstance> record = instance(id);
	if (!record.has_value()) {
		return std::nullopt;
	}
	return record->ref;
}

std::optional<ItemInstance> Equipment::instance(ItemId id) const {
	if (id == 0) {
		return std::nullopt;
	}
	// Worn first: an equipped record is the one a held-item query needs. A copy is
	// returned on purpose, so a caller cannot write through it.
	for (const std::optional<ItemInstance> &entry : slots_) {
		if (entry.has_value() && entry->ref.id == id) {
			return entry;
		}
	}
	for (const ItemInstance &entry : stored_) {
		if (entry.ref.id == id) {
			return entry;
		}
	}
	return std::nullopt;
}

bool Equipment::apply_wear(ItemId id, unsigned amount) {
	ItemInstance *record = find_owned(id);
	if (record == nullptr || !carries_item_state(*record)) {
		return false;
	}
	// Compare before subtracting: an oversized amount empties the item instead of
	// wrapping it into a full one.
	record->durability = record->durability > amount ? record->durability - amount : 0;
	return true;
}

bool Equipment::restore_durability(ItemId id, unsigned amount) {
	ItemInstance *record = find_owned(id);
	if (record == nullptr || !carries_item_state(*record)) {
		return false;
	}
	const unsigned missing = record->max_durability - record->durability;
	record->durability += amount > missing ? missing : amount;
	return true;
}

bool Equipment::held_usable(ItemId id, bool one_hand, bool two_hands) const {
	const std::optional<Slot> holder = holder_of(id);
	if (!holder.has_value() || !is_held(*holder)) {
		return false;
	}
	const ItemInstance &item = *slots_[slot_index(*holder)];
	if (item.ref.hand_use == HandUse::TwoHanded) {
		return two_hands;
	}
	return one_hand;
}

}  // namespace open
