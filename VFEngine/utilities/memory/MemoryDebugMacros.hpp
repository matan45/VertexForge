#pragma once

#include "MemoryTypes.hpp"

namespace memory {
	class IAllocator;
}

#ifdef DEBUG

#define VF_ALLOC(allocator, size, alignment) \
	memory::debugAllocate(allocator, size, alignment, __FILE__, __LINE__)

#define VF_FREE(allocator, handle) \
	memory::debugFree(allocator, handle, __FILE__, __LINE__)

namespace memory {
	AllocationHandle debugAllocate(IAllocator& allocator, uint64_t size, uint64_t alignment,
		const char* file, int line);
	void debugFree(IAllocator& allocator, const AllocationHandle& handle,
		const char* file, int line);
}

#else

#define VF_ALLOC(allocator, size, alignment) \
	(allocator).allocate(size, alignment)

#define VF_FREE(allocator, handle) \
	(allocator).free(handle)

#endif
