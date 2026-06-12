#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <glm/glm.hpp>

namespace resource {

	enum class LoadImportance : uint8_t
	{
		Critical = 0,   // UI-blocking, shaders, player avatar
		High = 1,       // Visible on screen now
		Normal = 2,     // Nearby, likely visible soon
		Low = 3,        // Prefetch
		Background = 4  // Speculative
	};

	// Coarse lifecycle of a scheduled load. The scheduler owns the
	// Pending/Loading/Completed/Cancelled transitions; loader lambdas mark
	// Failed (their exceptions are swallowed into null results) and may
	// report GpuUploadPending / fraction / bytes at their own granularity.
	enum class LoadStage : uint8_t
	{
		Pending = 0,
		Loading = 1,
		GpuUploadPending = 2,
		Completed = 3,
		Failed = 4,
		Cancelled = 5
	};

	inline const char* loadStageName(LoadStage stage)
	{
		switch (stage)
		{
		case LoadStage::Pending: return "Pending";
		case LoadStage::Loading: return "Loading";
		case LoadStage::GpuUploadPending: return "GPU Upload";
		case LoadStage::Completed: return "Completed";
		case LoadStage::Failed: return "Failed";
		case LoadStage::Cancelled: return "Cancelled";
		}
		return "Unknown";
	}

	// Shared between the scheduler, the loader lambda and UI snapshots —
	// all-atomic so any side can read/write without the scheduler mutex.
	class LoadProgress
	{
	public:
		using Ptr = std::shared_ptr<LoadProgress>;
		static Ptr create() { return std::make_shared<LoadProgress>(); }

		void setStage(LoadStage s) { stageValue.store(static_cast<uint8_t>(s), std::memory_order_relaxed); }
		LoadStage stage() const { return static_cast<LoadStage>(stageValue.load(std::memory_order_relaxed)); }

		void setFraction(float f) { fractionValue.store(f, std::memory_order_relaxed); }
		float fraction() const { return fractionValue.load(std::memory_order_relaxed); }

		void setBytes(uint64_t b) { bytesValue.store(b, std::memory_order_relaxed); }
		uint64_t bytes() const { return bytesValue.load(std::memory_order_relaxed); }

		bool isTerminal() const
		{
			LoadStage s = stage();
			return s == LoadStage::Completed || s == LoadStage::Failed || s == LoadStage::Cancelled;
		}

	private:
		std::atomic<uint8_t> stageValue{static_cast<uint8_t>(LoadStage::Pending)};
		std::atomic<float> fractionValue{0.0f};
		std::atomic<uint64_t> bytesValue{0};
	};

	struct LoadHint
	{
		float priority = 0.5f;
		LoadImportance importance = LoadImportance::Normal;
		std::optional<glm::vec3> worldPosition;
		uint32_t sectorId = 0;
	};

	struct ResourceSchedulerConfig
	{
		uint32_t maxConcurrentLoads = 8;
		uint32_t maxQueueSize = 256;
	};

	struct SchedulerStats
	{
		uint32_t pendingCount = 0;
		uint32_t inFlightCount = 0;
		uint32_t completedThisFrame = 0;
		uint32_t cancelledThisFrame = 0;
	};

}
