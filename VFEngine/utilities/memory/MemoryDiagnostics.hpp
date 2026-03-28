#pragma once

#include "MemoryTypes.hpp"
#include <vector>
#include <string>
#include <mutex>
#include <functional>

namespace memory {

	class IAllocator;

	struct RegisteredAllocator {
		IAllocator* allocator = nullptr;
		std::string name;
		AllocatorStrategy strategy;
	};

	class MemoryDiagnostics {
	public:
		static MemoryDiagnostics& instance();

		void registerAllocator(IAllocator* allocator);
		void unregisterAllocator(IAllocator* allocator);

		AllocatorStats getGlobalStats() const;
		AllocatorStats getAllocatorStats(const std::string& name) const;
		std::vector<std::pair<std::string, AllocatorStats>> getAllStats() const;

		void logSummary() const;

		// Optional: hook for GPU stats from VulkanMemoryManager
		using GpuStatsCallback = std::function<std::string()>;
		void setGpuStatsCallback(GpuStatsCallback callback);

	private:
		MemoryDiagnostics() = default;

		mutable std::mutex diagMutex;
		std::vector<RegisteredAllocator> allocators;
		GpuStatsCallback gpuStatsCallback;
	};

}
