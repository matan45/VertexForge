#pragma once

#include "BillboardTypes.hpp"
#include "../common/QuadBufferManager.hpp"
#include <array>
#include <vector>

namespace render::billboard
{
    inline constexpr std::array<BillboardVertex, 4> QUAD_VERTICES = {{
        {{-0.5f, -0.5f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f}, {0.0f, 1.0f}},
    }};

    inline constexpr std::array<uint16_t, 6> QUAD_INDICES = {0, 1, 2, 2, 3, 0};

    class BillboardBufferManager : public common::QuadBufferManager<BillboardVertex, BillboardInstanceData>
    {
    private:
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;
        bool externalCameraBuffer = false;

    public:
        explicit BillboardBufferManager(core::Device& device);
        ~BillboardBufferManager() = default;

        void init();
        void cleanUp();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos) const;

        void updateInstanceBuffer(const std::vector<BillboardRenderData>& billboards);

        vk::Buffer getCameraUBO() const { return cameraUBO; }

        void setExternalCameraBuffer(vk::Buffer buffer)
        {
            cameraUBO = buffer;
            externalCameraBuffer = true;
        }

    private:
        void createCameraUBO();
    };
}
