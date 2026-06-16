#pragma once
#include <atomic>
#include <memory>

namespace threading {

	// Cooperative cancellation flag shared between a submitter and a job. enkiTS cannot
	// preempt a running task, so the contract is: a job cancelled before it starts is
	// skipped, and long-running jobs may poll isCancelled() to bail out early.
	class CancellationToken
	{
	public:
		using Ptr = std::shared_ptr<CancellationToken>;

		static Ptr create() { return std::make_shared<CancellationToken>(); }

		void cancel() { cancelled.store(true, std::memory_order_release); }
		bool isCancelled() const { return cancelled.load(std::memory_order_acquire); }

	private:
		std::atomic<bool> cancelled{ false };
	};

}
