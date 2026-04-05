#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include "../../core/VulkanMemoryManager.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <array>

namespace render::ibl
{
    static constexpr uint32_t CUBE_MAP_SIZE = 512;
    // Irradiance maps are low-frequency and don't need high resolution
    static constexpr uint32_t IRRADIANCE_MAP_SIZE = 32;

    struct ImageData
    {
        vk::Image image;
        core::VulkanAllocation imageAllocation;
        vk::ImageView imageView;
        vk::Sampler sampler;
    };

    struct UniformBufferObject
    {
        alignas(16) glm::mat4 projection;
        alignas(16) glm::mat4 view;
    };

    struct CameraViewMatrix
    {
        // Vulkan cubemap face order: +X, -X, +Y, -Y, +Z, -Z
        // Standard OpenGL-style capture views - coordinate conversion happens in shaders
        inline static const std::array<glm::mat4, 6> captureViews = {
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)), // +X
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0)), // -X
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)), // +Y
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, -1)), // -Y
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0)), // +Z
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, -1, 0)) // -Z
        };
        inline static const glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    };

    struct SkyboxTargetParams
    {
        vk::ImageView colorImageView;
        uint32_t width;
        uint32_t height;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec4 clearColor;
    };

    struct OffScreenHelper
    {
        vk::Image image;
        vk::ImageView view;
        core::VulkanAllocation allocation;
    };

    struct QuadVertex
    {
        glm::vec3 position;
        glm::vec2 texture;
    };

    // Cube vertices for CUBEMAP GENERATION (rendering to faces from outside/origin)
    inline static const std::vector<glm::vec3> cubeVertices = {
        {-1.0f, 1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},

        {-1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f},

        {1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},

        {-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f},

        {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f},

        {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}
    };

    // Skybox cube vertices - wound for viewing from INSIDE the cube
    inline static const std::vector<glm::vec3> skyboxVertices = {
        // Front face (z = -1)
        {-1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
        {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},

        // Back face (z = +1)
        {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},

        // Left face (x = -1)
        {-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f},

        // Right face (x = +1)
        {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 1.0f},

        // Top face (y = +1)
        {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, -1.0f},
        {-1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},

        // Bottom face (y = -1)
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f}
    };

    inline static const std::vector<QuadVertex> quad = {
        {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},

        {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}
    };
}
