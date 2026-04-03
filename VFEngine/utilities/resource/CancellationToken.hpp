#pragma once
#include <atomic>
#include <memory>

namespace resource {

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
