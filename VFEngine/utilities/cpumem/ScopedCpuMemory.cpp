#include "ScopedCpuMemory.hpp"
#include "CpuMemoryManager.hpp"

namespace memory
{
    ScopedCpuMemory::ScopedCpuMemory(CategoryId id, uint64_t bytes)
        : id_(id), bytes_(bytes)
    {
        if (bytes_ != 0)
            CpuMemoryManager::instance().addUsage(id_, bytes_);
    }

    ScopedCpuMemory::~ScopedCpuMemory()
    {
        if (bytes_ != 0)
            CpuMemoryManager::instance().subUsage(id_, bytes_);
    }

    ScopedCpuMemory::ScopedCpuMemory(ScopedCpuMemory&& other) noexcept
        : id_(other.id_), bytes_(other.bytes_)
    {
        other.bytes_ = 0; // moved-from guard releases nothing
    }
}
