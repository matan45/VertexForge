#include "MemoryDiagnostics.hpp"
#include "print/Log.hpp"

namespace memory {

	MemoryDiagnostics& MemoryDiagnostics::instance()
	{
		static MemoryDiagnostics diag;
		return diag;
	}

	void MemoryDiagnostics::logSummary() const
	{
		std::lock_guard lock(diagMutex);

		vfLogInfo("=== Memory Diagnostics Summary ===");

		// GPU stats if callback is set
		if (gpuStatsCallback) {
			auto gpuInfo = gpuStatsCallback();
			if (!gpuInfo.empty()) {
				vfLogInfo("{}", gpuInfo);
			}
		}

		vfLogInfo("=================================");
	}

	void MemoryDiagnostics::setGpuStatsCallback(GpuStatsCallback callback)
	{
		std::lock_guard lock(diagMutex);
		gpuStatsCallback = std::move(callback);
	}

}
