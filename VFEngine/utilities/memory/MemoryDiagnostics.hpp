#pragma once

#include <string>
#include <mutex>
#include <functional>

namespace memory {

	class MemoryDiagnostics {
	public:
		static MemoryDiagnostics& instance();

		void logSummary() const;

		// Optional: hook for GPU stats from VulkanMemoryManager
		using GpuStatsCallback = std::function<std::string()>;
		void setGpuStatsCallback(GpuStatsCallback callback);

	private:
		MemoryDiagnostics() = default;

		mutable std::mutex diagMutex;
		GpuStatsCallback gpuStatsCallback;
	};

}
