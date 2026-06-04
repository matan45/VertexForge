#pragma once

#include "PoolAllocator.hpp"
#include <algorithm>

namespace memory {

	template<typename T>
	class TypedPoolAllocator {
	public:
		explicit TypedPoolAllocator(uint32_t count, const std::string& name = "TypedPoolAllocator")
			: pool(std::max(sizeof(T), alignof(T)), count, name)
		{
		}

		TypedPoolAllocator(const TypedPoolAllocator&) = delete;
		TypedPoolAllocator& operator=(const TypedPoolAllocator&) = delete;

		T* allocate()
		{
			auto handle = pool.allocate(sizeof(T), alignof(T));
			if (!handle.isValid()) {
				return nullptr;
			}
			lastHandle = handle;
			T* ptr = static_cast<T*>(pool.getPointer(handle));
			new (ptr) T();
			return ptr;
		}

		void free(T* ptr)
		{
			if (!ptr) {
				return;
			}
			ptr->~T();

			// Compute handle from pointer offset
			auto basePtr = static_cast<uint8_t*>(pool.getPointer(AllocationHandle{0, pool.getBlockSize(), 0}));
			auto objPtr = reinterpret_cast<uint8_t*>(ptr);
			uint64_t offset = static_cast<uint64_t>(objPtr - basePtr);

			AllocationHandle handle;
			handle.offset = offset;
			handle.size = pool.getBlockSize();
			pool.free(handle);
		}

		void reset()
		{
			pool.reset();
		}

		AllocatorStats getStats() const
		{
			return pool.getStats();
		}

		uint32_t getBlockCount() const { return pool.getBlockCount(); }

	private:
		PoolAllocator pool;
		AllocationHandle lastHandle;
	};

}
