#include "open/character.hpp"
#include "open/work.hpp"
#include "open/work_actions.hpp"

#include <utility>

namespace open {

// The sheet's share of the workbench slice: the one-job schedule, its interruption hooks,
// and the lock on an item under repair or enchantment. The rules of what a job may start
// with and what it delivers live in work_actions.cpp; the decision of when a frame ends,
// started, or finished one lives here and in the session.

WorkStatus Character::work_status() const {
	WorkStatus status;
	status.phase = work_.phase;
	status.request = work_.request;
	status.elapsed = work_.elapsed;
	status.failure = work_.failure;
	status.output = work_.output;
	return status;
}

bool Character::begin_work(const WorkRequest &request, const Vec3 &self_position,
						   WorkContext &env) {
	// Idle (a request that starts on the spot) or Queued (an arrival) may begin; only a job
	// already on the bench may not. A started job replaces the kept terminal result of the
	// last one - "kept until the next accepted job" ends here.
	if (!alive() || work_.phase == WorkPhase::Active) {
		return false;
	}
	const WorkbenchObservation *bench = env.bench(request.bench);
	if (bench == nullptr || !in_bench_reach(self_position, bench->position)) {
		return false;  // no bench at all, or not standing at it yet
	}
	const std::optional<WorkStart> start = work_begin(*this, request, env);
	if (!start.has_value()) {
		// A refused start is invisible: no job, no terminal result, no reason. Whatever the
		// sheet finished before this stays kept, exactly as it was.
		return false;
	}
	work_ = WorkRuntime{};
	work_.phase = WorkPhase::Active;
	work_.request = request;
	work_.start = *start;
	return true;
}

bool Character::queue_work(const WorkRequest &request) {
	if (!alive() || work_.phase == WorkPhase::Queued || work_.phase == WorkPhase::Active) {
		return false;
	}
	work_ = WorkRuntime{};
	work_.phase = WorkPhase::Queued;
	work_.request = request;
	return true;
}

bool Character::advance_work(float seconds, const Vec3 &self_position, WorkContext &env,
							 bool engaged) {
	if (work_.phase != WorkPhase::Queued && work_.phase != WorkPhase::Active) {
		return false;
	}
	// Zero seconds is the reconciliation frame: it may interrupt (everything below the
	// checks) but it never starts, ticks or completes anything.
	const bool ticking = seconds > 0.0f;
	if (!alive()) {
		interrupt_work(WorkFailure::Dead);
		return true;
	}
	// The bench the job is for has to still be an observed place. Checked before anything
	// else, both phases, and before the engagement reading below: a vanished bench is a
	// fact of the frame, not something the job can wait out.
	const WorkbenchObservation *bench = env.bench(work_.request.bench);
	if (bench == nullptr) {
		interrupt_work(WorkFailure::BenchLost);
		return true;
	}
	// Combat beginning interrupts: queued work clears free, active work stays paid. This
	// runs before settlement and before progress, which is what makes a fight arriving on
	// the fifteenth second win that second outright.
	if (engaged) {
		interrupt_work(WorkFailure::Combat);
		return true;
	}
	if (work_.phase == WorkPhase::Queued) {
		if (!ticking || !in_bench_reach(self_position, bench->position)) {
			return false;  // still walking; a queued job owns no clock, and a zero frame starts nothing
		}
		// Arrival re-validates against the sheet and the purse as they are now. Unmet at the
		// bench is a failure, and it is free: nothing had been paid to be refused. The copy
		// is load-bearing: begin_work replaces the runtime, and this request is that
		// runtime's own field.
		const WorkRequest pending = work_.request;
		if (!begin_work(pending, self_position, env)) {
			interrupt_work(WorkFailure::RequirementsUnmet);
			return true;
		}
		return true;  // started here, at zero; the walking minutes were never credited
	}

	if (is_stunned()) {
		// A stun freezes the bench the way it freezes every other schedule on this sheet:
		// the job neither progresses nor ends, and resumes when the legs do.
		return false;
	}
	if (work_.request.target.has_value() && !instance(*work_.request.target).has_value()) {
		interrupt_work(WorkFailure::TargetLost);
		return true;
	}
	work_.elapsed += static_cast<double>(seconds);
	if (work_.elapsed < kWorkDuration) {
		return true;
	}
	if (work_settle(*this, work_.start, env)) {
		work_.phase = WorkPhase::Completed;
		work_.failure = WorkFailure::None;
		if (work_.start.reserved != 0) {
			work_.output = work_.start.reserved;
		}
	} else {
		// Fifteen seconds that were paid for and a delivery that could not land: spent,
		// terminal, and no do-over bought by the clock.
		work_.phase = WorkPhase::Failed;
		work_.failure = WorkFailure::RequirementsUnmet;
	}
	return true;
}

bool Character::interrupt_work(WorkFailure reason) {
	if (work_.phase != WorkPhase::Queued && work_.phase != WorkPhase::Active) {
		return false;  // idle is not interrupted, and a kept result stands until the next job
	}
	work_.phase = WorkPhase::Failed;
	work_.failure = reason;
	return true;
}

// True while this id is the item a paid job is working on: the lock that refuses to let a
// thing under the tool be worn elsewhere or taken away mid-repair.
bool Character::work_locks(ItemId id) const {
	return work_.phase == WorkPhase::Active && work_.request.target.has_value()
		   && *work_.request.target == id;
}

}  // namespace open
