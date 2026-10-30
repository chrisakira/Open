#pragma once

#include "open/character.hpp"
#include "open/item_instance.hpp"
#include "open/items.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace open {

// The starter slice's production content: one validated item catalog - definitions 101–105,
// the temporary starter material and the three starter techniques - and the one command that
// puts a starting backpack on a character. docs/weapons.md "Starter playable equipment" owns
// the table; this is that table, in code, built once.
//
// The catalog is shared and immutable. Every sheet made from it resolves the same
// definitions, and nothing here offers a way to describe an item that is not in it: there is
// no definition to add, no instance factory on show, and minting is
// `initialize_starter_inventory`'s job with the caller's own allocator. The workbench actions
// of Task 16 will be the only other way anything comes into existence, and they will extend
// this catalog with recipes and benches rather than compete with it.

inline constexpr DefinitionId kStarterSword = 101;
inline constexpr DefinitionId kStarterDagger = 102;
inline constexpr DefinitionId kStarterSpear = 103;
inline constexpr DefinitionId kStarterHammer = 104;
inline constexpr DefinitionId kStarterShield = 105;
inline constexpr MaterialId kStarterMaterial = 100;

// The five starter definitions, in table order: the order the backpack is filled in and the
// order the equipment popup lists them while nothing has been moved.
inline constexpr std::size_t kStarterItemCount = 5u;
std::vector<DefinitionId> starter_definitions();

// The one validated starter catalog, built on first use and shared ever after. A catalog is
// only ever returned whole: `ItemCatalog::create` rejects a single bad entry, and this table
// is fixed, so a null answer would mean the build itself is wrong.
std::shared_ptr<const ItemCatalog> make_starter_catalog();

// Puts one instance of every starter definition this character does not already own into its
// backpack - quality tier 1, full durability, no enchantments - and equips nothing: the player
// starts carrying a kit, not wearing one.
//
// Idempotent, and duplicate-free per definition: a second call finds nothing missing and
// changes nothing, and a character that already carries a sword from somewhere else keeps that
// one rather than being handed a second. Atomic: if the sheet cannot carry anything, or the
// allocator cannot reserve the ids, the backpack is left exactly as it was. The answer is
// whether the character ends up owning the whole starter set.
bool initialize_starter_inventory(Character &character, ItemIdAllocator &allocator);

}  // namespace open
