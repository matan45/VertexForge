#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/FixedSizeFreeList.h>

namespace core::physics
{
	// Jolt JobSystem implementation that dispatches Jolt's internal physics jobs onto
	// the engine's enkiTS pool (threading::JobSystem) instead of spinning up a second
	// JPH::JobSystemThreadPool. This removes the ~2x core oversubscription that occurs
	// when Jolt's own (hw-1) thread pool runs alongside enkiTS (hw-2) during the step.
	//
	// Barrier handling is inherited from JobSystemWithBarrier; we only provide job
	// allocation (CreateJob/FreeJob), concurrency, and queueing (QueueJob/QueueJobs).
	// Each queued Jolt job becomes one pooled enkiTS task: Job::Execute() is internally
	// guarded so it runs exactly once even though the barrier's Wait() on the calling
	// thread may also try to execute it.
	class JoltEnkiJobSystem final : public JPH::JobSystemWithBarrier
	{
	public:
		JoltEnkiJobSystem(JPH::uint maxJobs, JPH::uint maxBarriers);
		~JoltEnkiJobSystem() override = default;

		int GetMaxConcurrency() const override;
		JPH::JobHandle CreateJob(const char* name, JPH::ColorArg color,
			const JobFunction& jobFunction, JPH::uint32 numDependencies = 0) override;

	protected:
		void QueueJob(Job* job) override;
		void QueueJobs(Job** jobs, JPH::uint numJobs) override;
		void FreeJob(Job* job) override;

	private:
		void dispatch(Job* job);

		JPH::FixedSizeFreeList<Job> jobs;
		int maxConcurrency = 1;
	};
}
