#include "../print/Log.hpp"
#include "JobSystem.hpp"

#include <TaskScheduler.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace threading {

	namespace {
		// Number of recyclable task slots. Sized far above the realistic count of
		// simultaneously in-flight fire-and-forget jobs so the pool effectively
		// never saturates; if it ever does, submitTask degrades to inline execution.
		constexpr size_t kPoolSize = 2048;
	}

	// Recyclable, self-contained task. enkiTS accesses the object AFTER ExecuteRange
	// returns (it decrements m_RunningCount and runs TaskComplete), so the object must
	// NOT free itself inside ExecuteRange. Instead the pool reuses a slot only once
	// GetIsComplete() is true — at which point, for a task with no dependents, the
	// scheduler is guaranteed to be done touching it (see TaskScheduler.cpp:634-637).
	class PooledJobTask final : public enki::ITaskSet {
	public:
		void ExecuteRange(enki::TaskSetPartition, uint32_t) override
		{
			if (fn) {
				fn();
				fn = nullptr; // release captured promise/resources promptly
			}
		}

		std::function<void()> fn;
		// Short-lived guard covering the setup window (claim -> AddTaskSetToPipe).
		// Prevents two acquirers from grabbing the same just-completed slot.
		std::atomic<bool> claimed{false};
	};

	// Continuation control block backing a JobHandle. Holds the completion promise
	// and any continuations registered before the job finished.
	struct JobControl {
		std::promise<void> promise;
		std::shared_future<void> future;
		std::mutex mutex;
		bool done = false;
		std::vector<std::function<void()>> continuations;

		JobControl() : future(promise.get_future().share()) {}

		bool isDone()
		{
			std::lock_guard<std::mutex> lock(mutex);
			return done;
		}

		void complete()
		{
			std::vector<std::function<void()>> pending;
			{
				std::lock_guard<std::mutex> lock(mutex);
				done = true;
				pending.swap(continuations);
			}
			promise.set_value();
			for (auto& cont : pending) {
				cont();
			}
		}

		// Register a continuation; runs it immediately if the job is already done.
		void addContinuation(std::function<void()> cont)
		{
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (!done) {
					continuations.push_back(std::move(cont));
					return;
				}
			}
			cont();
		}
	};

	struct JobSystem::Impl {
		enki::TaskScheduler scheduler;
		std::atomic<bool> initialized{false};
		uint32_t workerThreadCount = 1; // created task threads + 1 (calling thread)

		// Fixed pool of recyclable tasks (allocated once at init).
		std::vector<std::unique_ptr<PooledJobTask>> pool;
		std::atomic<uint64_t> cursor{0};

		// Acquire a free slot. Returns nullptr if the pool is fully in flight.
		PooledJobTask* acquireSlot()
		{
			const size_t size = pool.size();
			for (size_t probe = 0; probe < size; ++probe) {
				const size_t index = static_cast<size_t>(cursor.fetch_add(1, std::memory_order_relaxed)) % size;
				PooledJobTask* task = pool[index].get();

				if (!task->GetIsComplete()) {
					continue;
				}

				bool expected = false;
				if (!task->claimed.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
					continue; // another acquirer owns the setup window
				}

				// Re-check under the claim: a racing acquirer may have re-scheduled this
				// slot between our GetIsComplete() read and winning the claim.
				if (task->GetIsComplete()) {
					return task;
				}
				task->claimed.store(false, std::memory_order_release);
			}
			return nullptr;
		}
	};

	JobSystem::JobSystem() : pImpl(std::make_unique<Impl>()) {}
	JobSystem::~JobSystem() = default;

	JobSystem& JobSystem::instance()
	{
		static JobSystem inst;
		return inst;
	}

	void JobSystem::init(uint32_t threadCount, uint32_t maxExternalThreads)
	{
		if (threadCount == 0) {
			uint32_t hw = std::thread::hardware_concurrency();
			threadCount = (hw > 3) ? (hw - 2) : 2;
		}

		enki::TaskSchedulerConfig config;
		config.numTaskThreadsToCreate = threadCount;
		config.numExternalTaskThreads = maxExternalThreads;
		pImpl->scheduler.Initialize(config);
		pImpl->workerThreadCount = threadCount + 1;

		pImpl->pool.clear();
		pImpl->pool.reserve(kPoolSize);
		for (size_t i = 0; i < kPoolSize; ++i) {
			pImpl->pool.push_back(std::make_unique<PooledJobTask>());
		}

		pImpl->initialized.store(true, std::memory_order_release);
		vfLogInfo("JobSystem initialized with {} threads, {} external slots, {} task pool slots",
			pImpl->scheduler.GetNumTaskThreads(), maxExternalThreads, kPoolSize);
	}

	uint32_t JobSystem::getThreadCount() const
	{
		return pImpl->scheduler.GetNumTaskThreads();
	}

	uint32_t JobSystem::getWorkerThreadCount() const
	{
		return pImpl->workerThreadCount;
	}

	enki::TaskScheduler* JobSystem::getScheduler()
	{
		return &pImpl->scheduler;
	}

	void JobSystem::shutdown()
	{
		pImpl->scheduler.WaitforAllAndShutdown();
		pImpl->initialized.store(false, std::memory_order_release);
		pImpl->pool.clear();

		vfLogInfo("JobSystem shutdown complete");
	}

	void JobSystem::parallelFor(uint32_t count,
		const std::function<void(uint32_t, uint32_t)>& body,
		uint32_t minBatchSize, JobPriority priority)
	{
		if (count == 0) return;

		if (count <= minBatchSize)
		{
			body(0, count);
			return;
		}

		enki::TaskSet task(count,
			[&body](enki::TaskSetPartition range, uint32_t) {
				body(range.start, range.end);
			}
		);
		task.m_MinRange = minBatchSize;
		task.m_Priority = static_cast<enki::TaskPriority>(static_cast<uint32_t>(priority));
		pImpl->scheduler.AddTaskSetToPipe(&task);
		pImpl->scheduler.WaitforTask(&task);
	}

	void JobSystem::parallelFor(uint32_t count,
		const std::function<void(uint32_t, uint32_t, uint32_t)>& body,
		uint32_t minBatchSize, JobPriority priority)
	{
		if (count == 0) return;

		if (count <= minBatchSize)
		{
			body(0, count, 0);
			return;
		}

		enki::TaskSet task(count,
			[&body](enki::TaskSetPartition range, uint32_t threadNum) {
				body(range.start, range.end, threadNum);
			}
		);
		task.m_MinRange = minBatchSize;
		task.m_Priority = static_cast<enki::TaskPriority>(static_cast<uint32_t>(priority));
		pImpl->scheduler.AddTaskSetToPipe(&task);
		pImpl->scheduler.WaitforTask(&task);
	}

	JobHandle JobSystem::parallelForAsync(uint32_t count,
		std::function<void(uint32_t, uint32_t)> body,
		uint32_t minBatchSize, JobPriority priority)
	{
		// Run the (blocking) parallelFor on a worker job and hand back its handle. The worker
		// participates in its own WaitforTask, so this never deadlocks even under load. A zero
		// count is handled inside parallelFor (returns immediately); the handle still completes.
		return submitJob(
			[this, count, body = std::move(body), minBatchSize, priority]() {
				parallelFor(count, body, minBatchSize, priority);
			},
			priority);
	}

	void JobSystem::submitTask(std::function<void()> func, JobPriority priority)
	{
		if (!pImpl->initialized.load(std::memory_order_acquire)) {
			func();
			return;
		}

		if (pImpl->scheduler.GetThreadNum() == enki::NO_THREAD_NUM) {
			if (!pImpl->scheduler.RegisterExternalTaskThread()) {
				vfLogError("[JobSystem] Failed to register external thread - all {} external slots exhausted. "
					"Increase maxExternalThreads in JobSystem::init()", pImpl->scheduler.GetNumRegisteredExternalTaskThreads());
				func();
				return;
			}
			thread_local struct Guard {
				enki::TaskScheduler* scheduler = nullptr;
				~Guard() { if (scheduler) scheduler->DeRegisterExternalTaskThread(); }
			} guard;
			guard.scheduler = &pImpl->scheduler;
		}

		PooledJobTask* task = pImpl->acquireSlot();
		if (!task) {
			// Pool saturated (extremely unlikely): run inline rather than allocate unbounded.
			func();
			return;
		}

		task->fn = std::move(func);
		task->m_Priority = static_cast<enki::TaskPriority>(static_cast<uint32_t>(priority));

		// Schedule first (marks the slot not-complete), then release the setup guard.
		// Releasing only after AddTaskSetToPipe keeps any racing acquirer out of the
		// window where the slot still reports complete but fn is already overwritten.
		pImpl->scheduler.AddTaskSetToPipe(task);
		task->claimed.store(false, std::memory_order_release);
	}

	void JobSystem::enqueue(std::function<void()> fn, JobPriority priority)
	{
		submitTask(std::move(fn), priority);
	}

	JobHandle JobSystem::submitJob(std::function<void()> fn, JobPriority priority)
	{
		auto control = std::make_shared<JobControl>();
		JobHandle handle;
		handle.control = control;

		submitTask(
			[control, fn = std::move(fn)]() {
				fn();
				control->complete();
			},
			priority);

		return handle;
	}

	JobHandle JobSystem::submitJob(std::function<void()> fn, CancellationToken::Ptr token, JobPriority priority)
	{
		if (!token) {
			return submitJob(std::move(fn), priority);
		}

		auto control = std::make_shared<JobControl>();
		JobHandle handle;
		handle.control = control;

		submitTask(
			[control, fn = std::move(fn), token = std::move(token)]() {
				if (!token->isCancelled()) {
					fn();
				}
				control->complete();
			},
			priority);

		return handle;
	}

	JobHandle JobSystem::then(const JobHandle& dep, std::function<void()> fn, JobPriority priority)
	{
		if (!dep.control) {
			return submitJob(std::move(fn), priority);
		}

		auto control = std::make_shared<JobControl>();
		JobHandle handle;
		handle.control = control;

		auto fnShared = std::make_shared<std::function<void()>>(std::move(fn));
		dep.control->addContinuation(
			[this, control, fnShared, priority]() {
				submitTask(
					[control, fnShared]() {
						(*fnShared)();
						control->complete();
					},
					priority);
			});

		return handle;
	}

	JobHandle JobSystem::whenAll(std::span<const JobHandle> deps, std::function<void()> fn, JobPriority priority)
	{
		auto control = std::make_shared<JobControl>();
		JobHandle handle;
		handle.control = control;

		auto fnShared = std::make_shared<std::function<void()>>(std::move(fn));
		auto fire = [this, control, fnShared, priority]() {
			submitTask(
				[control, fnShared]() {
					(*fnShared)();
					control->complete();
				},
				priority);
		};

		// Count only valid (still-referenced) dependencies.
		uint32_t validCount = 0;
		for (const JobHandle& dep : deps) {
			if (dep.control) ++validCount;
		}

		if (validCount == 0) {
			fire();
			return handle;
		}

		auto remaining = std::make_shared<std::atomic<uint32_t>>(validCount);
		for (const JobHandle& dep : deps) {
			if (!dep.control) continue;
			dep.control->addContinuation(
				[remaining, fire]() {
					if (remaining->fetch_sub(1, std::memory_order_acq_rel) == 1) {
						fire();
					}
				});
		}

		return handle;
	}

	void JobSystem::wait(const JobHandle& handle)
	{
		if (handle.control) {
			handle.control->future.wait();
		}
	}

	// --- JobHandle out-of-line members (need the complete JobControl type) ---

	bool JobHandle::isComplete() const
	{
		return !control || control->isDone();
	}

	void JobHandle::wait() const
	{
		if (control) {
			control->future.wait();
		}
	}

}
