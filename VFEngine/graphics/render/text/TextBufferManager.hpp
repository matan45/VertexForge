#pragma once

#include "TextTypes.hpp"
#include "../common/QuadBufferManager.hpp"
#include <array>
#include <vector>

namespace render::text
{
    inline constexpr std::array<TextVertex, 4> QUAD_VERTICES = {{
        {{-0.5f, -0.5f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f}, {0.0f, 1.0f}},
    }};

    inline constexpr std::array<uint16_t, 6> QUAD_INDICES = {0, 1, 2, 2, 3, 0};

    class TextBufferManager : public common::QuadBufferManager<TextVertex, TextCharInstance>
    {
    private:
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

    public:
        explicit TextBufferManager(core::Device& device);
        ~TextBufferManager() = default;

        void init();
        void cleanUp();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos) const;

        void updateInstanceBuffer(const std::vector<TextCharInstance>& instances);

        vk::Buffer getCameraUBO() const { return cameraUBO; }

    private:
        void createCameraUBO();
    };
}
