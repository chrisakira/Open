#include "open/item_instance.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

#include "open/character_rules.hpp"

namespace open {

std::optional<ItemInstance> make_item_instance(const ItemCatalog &catalog, DefinitionId definition,
											   MaterialId material, ItemId id, unsigned quality) {
	if (id == 0) {
		return std::nullopt;
	}
	const ItemDefinition *made = catalog.item(definition);
	const MaterialDefinition *stuff = catalog.material(material);
	if (made == nullptr || stuff == nullptr) {
		return std::nullopt;
	}
	// The tier indexes the material's own table; there is no formula that could
	// invent a maximum the material never declared.
	if (quality < 1 || quality > static_cast<unsigned>(stuff->max_durability_by_quality.size())) {
		return std::nullopt;
	}
	const unsigned maximum = stuff->max_durability_by_quality[quality - 1];

	ItemInstance instance;
	instance.ref.id = id;
	instance.ref.allowed_slots = made->allowed_slots;
	instance.ref.hand_use = made->hand_use;
	// The job tag is copied from the definition, so the held-item gates keep
	// working and cannot disagree with what the item actually is.
	if (made->weapon.has_value()) {
		instance.ref.combat_skill = made->weapon->job.basic;
		instance.ref.combat_option = made->weapon->job.option;
	} else if (made->shield.has_value()) {
		instance.ref.combat_skill = made->shield->job.basic;
		instance.ref.combat_option = made->shield->job.option;
	}
	instance.definition = definition;
	instance.material = material;
	instance.quality = quality;
	instance.max_durability = maximum;
	instance.durability = maximum;  // a new item is whole
	// Only a weapon has enchantment slots, and one per quality tier.
	if (made->weapon.has_value()) {
		instance.enchantment_slots = quality;
	}
	return instance;
}

std::vector<Requirement> enchantment_requirements(const ItemInstance &item) {
	std::vector<Requirement> totals;
	for (const AppliedEnchantment &applied : item.enchantments) {
		if (!applied.requirement.has_value()) {
			continue;
		}
		const Attribute attribute = applied.requirement->attribute;
		const double value = applied.requirement->value;
		bool merged = false;
		for (Requirement &total : totals) {
			if (total.attribute == attribute) {
				// Same-type requirements add; different attributes stay separate checks.
				total.value += value;
				merged = true;
				break;
			}
		}
		if (!merged) {
			totals.push_back(Requirement{attribute, value});
		}
	}
	return totals;
}

namespace {

constexpr ItemId kMaxItemId = std::numeric_limits<ItemId>::max();

}  // namespace

ItemIdAllocator::ItemIdAllocator(const std::vector<ItemId> &existing) {
	for (const ItemId id : existing) {
		adopt(id);
	}
}

bool ItemIdAllocator::used(ItemId id) const {
	return std::find(used_.begin(), used_.end(), id) != used_.end();
}

void ItemIdAllocator::adopt(ItemId id) {
	if (id == 0 || used(id)) {
		return;
	}
	used_.push_back(id);
	if (next_ == 0) {
		return;  // already spent: an adopted id cannot hand the range back
	}
	if (next_ <= id) {
		next_ = id == kMaxItemId ? 0 : id + 1;
	}
}

std::optional<ItemId> ItemIdAllocator::allocate() {
	while (next_ != 0 && used(next_)) {
		next_ = next_ == kMaxItemId ? 0 : next_ + 1;
	}
	if (next_ == 0) {
		return std::nullopt;
	}
	// Reserved before the caller's work begins, so nothing else can take it while
	// that work is still deciding whether it can pay.
	const ItemId id = next_;
	used_.push_back(id);
	next_ = id == kMaxItemId ? 0 : id + 1;
	return id;
}

bool ItemIdAllocator::claim(ItemId id) {
	if (id == 0 || used(id)) {
		return false;
	}
	adopt(id);
	return true;
}

}  // namespace open
