#pragma once
#include "JobSystem.hpp"
#include <vector>
#include <utility>
#include <cstdint>

namespace threading {

	// Parallel fold over the index range [0, count). Each worker thread folds the
	// sub-ranges it is handed into its own accumulator (one slot per enkiTS thread,
	// indexed by threadNum), so there is zero contention on the hot path. The
	// per-thread accumulators are then combined sequentially into the final result.
	//
	//   rangeFn(uint32_t begin, uint32_t end, T& accumulator)  // fold [begin,end) into accumulator
	//   combineFn(T lhs, T rhs) -> T                           // merge two partial results
	//
	// `identity` seeds every accumulator and the final reduction (must be the neutral
	// element for combineFn). Falls back to a single sequential fold for small counts.
	template<typename T, typename RangeFn, typename CombineFn>
	T parallelReduce(uint32_t count, const T& identity,
		RangeFn&& rangeFn, CombineFn&& combineFn, uint32_t minBatchSize = 64)
	{
		if (count == 0) {
			return identity;
		}

		if (count <= minBatchSize) {
			T accumulator = identity;
			rangeFn(0u, count, accumulator);
			return accumulator;
		}

		// One accumulator per enkiTS thread (+1 for the calling thread, threadNum 0).
		uint32_t threadCount = JobSystem::instance().getThreadCount() + 1;
		std::vector<T> partials(threadCount, identity);

		JobSystem::instance().parallelFor(count,
			[&rangeFn, &partials, threadCount](uint32_t begin, uint32_t end, uint32_t threadNum)
			{
				uint32_t slot = threadNum < threadCount ? threadNum : 0;
				rangeFn(begin, end, partials[slot]);
			}, minBatchSize);

		T result = identity;
		for (auto& partial : partials) {
			result = combineFn(std::move(result), std::move(partial));
		}
		return result;
	}

}
