#include "JoltEnkiJobSystem.hpp"

#include <threading/JobSystem.hpp>

#include <chrono>
#include <thread>

namespace core::physics
{
	JoltEnkiJobSystem::JoltEnkiJobSystem(JPH::uint maxJobs, JPH::uint maxBarriers)
	{
		JobSystemWithBarrier::Init(maxBarriers);
		jobs.Init(maxJobs, maxJobs);

		// Created task threads + 1 for the thread that calls WaitForJobs (which runs
		// barrier jobs itself). External submit slots are excluded on purpose.
		maxConcurrency = static_cast<int>(threading::JobSystem::instance().getWorkerThreadCount());
		if (maxConcurrency < 1) {
			maxConcurrency = 1;
		}
	}

	int JoltEnkiJobSystem::GetMaxConcurrency() const
	{
		return maxConcurrency;
	}

	JPH::JobHandle JoltEnkiJobSystem::CreateJob(const char* name, JPH::ColorArg color,
		const JobFunction& jobFunction, JPH::uint32 numDependencies)
	{
		// Acquire a job slot (mirrors JobSystemThreadPool: spin briefly if the pool is
		// momentarily exhausted - sized by cMaxPhysicsJobs so this is effectively never).
		JPH::uint32 index;
		for (;;) {
			index = jobs.ConstructObject(name, color, this, jobFunction, numDependencies);
			if (index != JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex) {
				break;
			}
			JPH_ASSERT(false, "No physics jobs available!");
			std::this_thread::sleep_for(std::chrono::microseconds(100));
		}
		Job* job = &jobs.Get(index);

		// Handle keeps a reference; the job may complete immediately once queued.
		JPH::JobHandle handle(job);

		if (numDependencies == 0) {
			QueueJob(job);
		}

		return handle;
	}

	void JoltEnkiJobSystem::dispatch(Job* job)
	{
		// The enkiTS task owns a reference for the duration of execution.
		job->AddRef();
		threading::JobSystem::instance().enqueue(
			[job]() {
				job->Execute();
				job->Release();
			},
			threading::JobPriority::HIGH);
	}

	void JoltEnkiJobSystem::QueueJob(Job* job)
	{
		dispatch(job);
	}

	void JoltEnkiJobSystem::QueueJobs(Job** jobsToQueue, JPH::uint numJobs)
	{
		for (JPH::uint i = 0; i < numJobs; ++i) {
			dispatch(jobsToQueue[i]);
		}
	}

	void JoltEnkiJobSystem::FreeJob(Job* job)
	{
		jobs.DestructObject(job);
	}
}
