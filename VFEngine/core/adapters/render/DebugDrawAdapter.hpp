#pragma once

#include "../../services/providers/render/IDebugDrawProvider.hpp"
#include "../../graphics/render/tools/ImmediateDebugTypes.hpp"
#include <glm/glm.hpp>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace core
{
    class DebugDrawAdapter : public services::IDebugDrawProvider
    {
    private:
        // VK-1660: lines arrive from several frame tasks at once (Navmesh and Volumetric workers,
        // the main-pinned WorldSector task, script natives), so every access holds this mutex.
        mutable std::mutex mutex;
        render::mesh::ImmediateDebugDrawList drawList;
        std::atomic<bool> enabled{true};
        bool budgetWarned = false;

        static constexpr size_t MAX_LINE_VERTICES = 128 * 1024; // 64K line segments = 128K vertices

        // Caller holds `mutex`.
        void addLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color);

    public:
        DebugDrawAdapter() = default;
        ~DebugDrawAdapter() override = default;

        void drawLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color) override;
        void drawRay(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color) override;
        void drawBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color) override;
        void drawSphere(const glm::vec3& center, float radius, const glm::vec4& color) override;

        void setEnabled(bool value) override { enabled = value; }
        bool isEnabled() const override { return enabled; }

        render::mesh::ImmediateDebugDrawList consumeDrawList() override;
    };
}
