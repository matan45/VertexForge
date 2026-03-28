#pragma once

#include <mutex>

namespace memory {

	class MemoryDiagnostics {
	public:
		static MemoryDiagnostics& instance();

		void logSummary() const;

	private:
		MemoryDiagnostics() = default;
		mutable std::mutex diagMutex;
	};

}
