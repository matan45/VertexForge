#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <mutex>

namespace vegetation
{
    struct ColliderDebugEntry
    {
        glm::vec3 position{0.0f};
        float rotation = 0.0f;
        float radius = 0.3f;
        float height = 5.0f;
    };

    // Shared debug data for vegetation colliders, written by GPU renderer, read by debug renderer.
    class VegetationColliderDebugData
    {
    public:
        static VegetationColliderDebugData& instance()
        {
            static VegetationColliderDebugData inst;
            return inst;
        }

        void set(std::vector<ColliderDebugEntry> entries)
        {
            std::lock_guard lock(mutex);
            data = std::move(entries);
        }

        std::vector<ColliderDebugEntry> get() const
        {
            std::lock_guard lock(mutex);
            return data;
        }

        void clear()
        {
            std::lock_guard lock(mutex);
            data.clear();
        }

    private:
        VegetationColliderDebugData() = default;
        mutable std::mutex mutex;
        std::vector<ColliderDebugEntry> data;
    };
}
