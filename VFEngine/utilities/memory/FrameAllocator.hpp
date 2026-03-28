#pragma once

#include "LinearAllocator.hpp"
#include <array>
#include <cstdint>

namespace memory {

	class FrameAllocator {
	public:
		static constexpr uint32_t FRAME_COUNT = 2;

		FrameAllocator(uint64_t frameBudget, const std::string& name = "FrameAllocator");

		FrameAllocator(const FrameAllocator&) = delete;
		FrameAllocator& operator=(const FrameAllocator&) = delete;

		void setFrame(uint32_t frameIndex);

		AllocationHandle allocate(uint64_t size, uint64_t alignment = 1);
		void* getPointer(const AllocationHandle& handle) const;

		AllocatorStats getStats() const;
		uint32_t getCurrentFrame() const { return currentFrame; }

	private:
		std::array<LinearAllocator, FRAME_COUNT> allocators;
		uint32_t currentFrame = 0;
	};

}
