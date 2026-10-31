#pragma once

#include "open/character_rules.hpp"
#include "open/items.hpp"
#include "open/move_order.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace open {

class ItemCatalog;
class ItemIdAllocator;

// The workbench slice's shared vocabulary: what may be asked for, what a sheet is doing
// about it, and the ledger the materials come out of. Like the fight, work is a schedule
// and a transaction - the actual deciding lives with the rules that already own the
// numbers (work_actions.hpp) and with the sheet that owns the result.
//
// One fact shapes everything here: materials are paid when an action starts and are never
// paid back. A cancelled job, a fight that interrupts one, and a bench that vanishes under
// it all cost the same as success did - nothing returns, nothing is charged twice. That is
// why validation is front-loaded and settlement is a formality: the only failure left for
// the fifteen seconds to bring is the failure the smith was warned about by standing at
// the right bench with the wrong materials, and that one cannot start at all.

// What may be asked for at a bench. The enum itself lives in items.hpp beside the recipe
// records that carry it; `valid` lives with the other domain checks in items.cpp.

// What is being asked for. A request is data, never a command that already succeeded:
// `recipe` belongs to the catalog, `bench` to the caller's current observations, and
// `target` to the sheet that owns items - all three are re-resolved where they are read.
// Every request names its recipe (the catalog also holds exactly one Repair recipe and
// one enchanting recipe for that purpose); fields the kind does not use stay at zero.
struct WorkRequest {
	WorkKind kind = WorkKind::CraftWeapon;
	RecipeId recipe = 0;
	WorkbenchId bench = 0;
	std::optional<ItemId> target;
	unsigned quality = 0;  // craft only: the tier chosen at request, 1..kQualityTierCount
	std::optional<EnchantmentId> enchantment;  // enchant only
};

// How far a job has come. Queued means "walk there, nothing is paid yet"; Active means
// "paid and on the bench". The two terminal phases are kept as the result of the last job
// until the next one is accepted, so a popup closing loses nothing.
enum class WorkPhase {
	Idle,
	Queued,
	Active,
	Completed,
	Failed,
};

// Why a job that did not finish did not finish. Queued clears are the free ones: the
// player moved, attacked, was attacked, cancelled, or the bench went away before anything
// was paid. Everything else on this list already paid.
enum class WorkFailure {
	None,
	Cancelled,
	Combat,
	Movement,
	BenchLost,
	TargetLost,
	Dead,
	RequirementsUnmet,  // queued work that arrival found unpayable: free, and why
};

// What the fifteen seconds are sampled from: the job the result pays, the trained rank
// behind it, the id a craft output already owns, the cost that was paid, and - for an
// enchantment - the application captured at that rank. This is the transaction record:
// it exists only after `work_begin` has taken the materials, and settlement can never
// re-derive it from who the smith has become in the meantime.
struct WorkStart {
	WorkRequest request;
	SkillJob job;
	unsigned trained = 0;
	ItemId reserved = 0;  // craft only; 0 otherwise
	std::vector<MaterialQuantity> paid;
	std::optional<AppliedEnchantment> enchantment;  // enchant only, captured at `trained`
};

// One fifteen-second bench job, with everything the start sampled: `start` is the paid
// transaction - the rank, the job, the reserved output id, the cost that left the purse,
// and the enchantment captured at that rank - so training mid-work changes the result of
// nothing. Phase, elapsed and reason run beside it.
struct WorkRuntime {
	WorkPhase phase = WorkPhase::Idle;
	WorkRequest request;
	double elapsed = 0.0;
	WorkFailure failure = WorkFailure::None;
	std::optional<ItemId> output;  // the crafted instance, on completion
	WorkStart start;
};

// Every bench action takes exactly this long, whether it forges, mends or enchants.
inline constexpr double kWorkDuration = 15.0;

// The horizontal distance a smith must stand within to work a bench, inclusive, Y ignored
// - the same reading melee reach uses.
inline constexpr double kBenchReach = 1.5;

// What a sheet reads about its own work. A copy: the caller cannot push a schedule back.
struct WorkStatus {
	WorkPhase phase = WorkPhase::Idle;
	WorkRequest request;
	double elapsed = 0.0;
	double duration = kWorkDuration;
	WorkFailure failure = WorkFailure::None;
	std::optional<ItemId> output;

	bool busy() const { return phase == WorkPhase::Queued || phase == WorkPhase::Active; }
	bool queued() const { return phase == WorkPhase::Queued; }
	bool active() const { return phase == WorkPhase::Active; }
};

// Where a bench was observed to be. Observations are the caller's snapshot of the scene -
// they enter the session with each call and are never stored by it - and a job holds only
// its bench id, so a bench that has moved or vanished is discovered at the next reading
// exactly like a fight target who died there.
struct WorkbenchObservation {
	WorkbenchId id = 0;
	BenchType type = BenchType::Smithing;
	Vec3 position;
};

// Which instances a scene owns ids for is one ledger; which stacks of material whom owns is
// another. Quantities are whole units, per owner and per material; a missing entry is zero,
// not an error.
//
// `consume` is the transaction: all of a cost or none of it, checked before any of it
// moves. There is no reservation, no refund token, and no second way to take anything -
// completion does not charge, and interruption does not return.
//
// `grant` is not a cheat command. It exists so validated one-time setup (and only that, and
// the ledger remembers who was already set up) can fill a starting backpack's material side;
// no bridge, session order or popup reaches it.
class MaterialLedger {
public:
	std::uint64_t quantity(std::uint64_t owner, MaterialId material) const;

	// True and debited when every line can be paid from one call; false and untouched
	// otherwise. A zero-quantity line is a shape error, not a free pass, and an unknown
	// material is simply a zero the cost can never cover.
	bool consume(std::uint64_t owner, std::span<const MaterialQuantity> cost);

	// Setup-only credit. Refuses zero, non-finite owners have no meaning, and an amount
	// that would wrap the counter is refused with the stack untouched.
	bool grant(std::uint64_t owner, MaterialId material, std::uint64_t amount);

	// One-time setup bookkeeping: whether this owner's starting supply was already issued,
	// so a second setup cannot top a spent backpack back up.
	bool was_granted(std::uint64_t owner) const;
	void mark_granted(std::uint64_t owner);

private:
	std::unordered_map<std::uint64_t, std::unordered_map<MaterialId, std::uint64_t>> stocks_;
	std::vector<std::uint64_t> granted_;
};

// Everything a bench action may need from outside the sheet, carried to it: the tables,
// the id mint, the material purse, and this frame's bench observations. The context is a
// bundle of references to things the caller keeps alive - never a pointer to a Godot
// object, and never itself a store of positions: observations are looked up, not cached.
struct WorkContext {
	WorkContext(const ItemCatalog &catalog_, ItemIdAllocator &ids_, MaterialLedger &materials_)
		: catalog(catalog_), ids(ids_), materials(materials_) {}

	const ItemCatalog &catalog;
	ItemIdAllocator &ids;
	MaterialLedger &materials;
	std::vector<WorkbenchObservation> benches;

	const WorkbenchObservation *bench(WorkbenchId id) const {
		for (const WorkbenchObservation &observed : benches) {
			if (observed.id == id) {
				return &observed;
			}
		}
		return nullptr;
	}
};

}  // namespace open
