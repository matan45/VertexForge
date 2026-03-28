#include "FrameAllocator.hpp"

namespace memory {

	FrameAllocator::FrameAllocator(uint64_t frameBudget, const std::string& name)
		: allocators{
			LinearAllocator(frameBudget, name + "_Frame0"),
			LinearAllocator(frameBudget, name + "_Frame1")
		}
	{
	}

	void FrameAllocator::setFrame(uint32_t frameIndex)
	{
		currentFrame = frameIndex % FRAME_COUNT;
		allocators[currentFrame].reset();
	}

	AllocationHandle FrameAllocator::allocate(uint64_t size, uint64_t alignment)
	{
		return allocators[currentFrame].allocate(size, alignment);
	}

	void* FrameAllocator::getPointer(const AllocationHandle& handle) const
	{
		return allocators[currentFrame].getPointer(handle);
	}

	AllocatorStats FrameAllocator::getStats() const
	{
		return allocators[currentFrame].getStats();
	}

}
