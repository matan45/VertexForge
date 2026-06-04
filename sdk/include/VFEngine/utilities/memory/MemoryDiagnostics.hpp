#pragma once

namespace memory {

	class MemoryDiagnostics {
	public:
		static MemoryDiagnostics& instance();

		void logSummary() const;

	private:
		MemoryDiagnostics() = default;
	};

}
