#pragma once

#include "open/items.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace open {

// The owned thing. A definition says what an item is; this says which one you
// have, how good a job the crafter did, how much wear is left, and what is fixed
// to it. Everything here travels with the item: quality, durability and later
// enchantments survive equipping, storing and changing hands, and they are only
// ever changed through the equipment container that owns the record.
struct ItemInstance {
	ItemRef ref;
	DefinitionId definition = 0;  // 0 for a placement-only legacy record
	MaterialId material = 0;
	unsigned quality = 0;             // 1..7 for a made item, 0 when it was never made
	unsigned durability = 0;          // 0..max_durability
	unsigned max_durability = 0;      // read from the material at this quality
};

// Builds an owned instance from a catalog: the placement record is filled from
// the definition, the durability maximum is read from the material's table at
// the chosen tier, and a new item is whole. Returns nothing rather than a
// half-described item: an unknown definition or material, a zero instance id, or
// a tier outside the table is refused.
std::optional<ItemInstance> make_item_instance(const ItemCatalog &catalog,
											   DefinitionId definition, MaterialId material, ItemId id,
											   unsigned quality);

// Hands out instance ids for one session. Ids are reserved before the work that
// uses them starts, are never recycled, and never wrap: an allocator with nowhere
// left to go reports nothing, so a caller cannot mint a second owner of an id it
// already gave away. Failed work leaves a gap, which is cheap; a collision is not.
class ItemIdAllocator {
public:
	ItemIdAllocator() = default;

	// Adopts ids that already exist — a character's backpack, a loaded scene — so
	// the first id handed out cannot alias one of them. Zero ids are ignored.
	explicit ItemIdAllocator(const std::vector<ItemId> &existing);

	// The next unused id, marked used before it is returned.
	std::optional<ItemId> allocate();

	// Reserves one specific id: refused for zero or an id already accounted for,
	// and a refusal changes nothing.
	bool claim(ItemId id);

private:
	bool used(ItemId id) const;
	void adopt(ItemId id);

	std::vector<ItemId> used_;
	ItemId next_ = 1;  // 0 means the range is spent: there is nothing left to hand out
};

}  // namespace open
