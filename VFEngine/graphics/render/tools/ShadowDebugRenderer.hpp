#pragma once

#include "../shadow/ShadowTypes.hpp"
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <array>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::mesh
{
    /**
     * Shadow frustum types for debug visualization.
     */
    enum class ShadowFrustumType : uint8_t
    {
        DirectionalCascade,  // Orthographic frustum box (CSM cascade)
        SpotFrustum,         // Perspective frustum cone
        PointSphere          // Sphere outline showing light radius
    };

    /**
     * Data for rendering a single shadow debug visualization.
     */
    struct ShadowFrustumRenderData
    {
        ShadowFrustumType type = ShadowFrustumType::DirectionalCascade;
        uint32_t cascadeIndex = 0;           // For CSM: cascade level (0-3)
        glm::mat4 inverseViewProjection{1.0f}; // For frustum reconstruction
        glm::vec3 lightPosition{0.0f};       // For point lights (sphere center)
        float radius = 0.0f;                 // For point lights (farPlane = sphere radius)
    };

    /**
     * Push constants for shadow debug rendering.
     */
    struct ShadowDebugPushConstants
    {
        glm::mat4 viewProj;          // Editor's view-projection matrix
        glm::mat4 inverseViewProj;   // Shadow's inverse view-projection (for frustums)
        glm::vec4 color;
    };

    /**
     * ShadowDebugRenderer - Renders shadow frustums and volumes for debugging.
     *
     * Visualization types:
     * - Directional (CSM): Orthographic frustum boxes, color-coded by cascade
     *   - Cascade 0: Red
     *   - Cascade 1: Orange
     *   - Cascade 2: Yellow
     *   - Cascade 3: Green
     * - Spot lights: Perspective frustum cones (cyan)
     * - Point lights: Sphere outlines showing light radius (magenta)
     */
    class ShadowDebugRenderer
    {
    private:
        // Static NDC corners for frustum (Vulkan: z = 0 near, z = 1 far)
        inline static constexpr std::array<glm::vec3, 8> ndcCorners = {{
            // Near plane (z = 0 in Vulkan)
            {-1.0f, -1.0f, 0.0f},  // bottom-left
            { 1.0f, -1.0f, 0.0f},  // bottom-right
            { 1.0f,  1.0f, 0.0f},  // top-right
            {-1.0f,  1.0f, 0.0f},  // top-left
            // Far plane (z = 1 in Vulkan)
            {-1.0f, -1.0f, 1.0f},  // bottom-left
            { 1.0f, -1.0f, 1.0f},  // bottom-right
            { 1.0f,  1.0f, 1.0f},  // top-right
            {-1.0f,  1.0f, 1.0f},  // top-left
        }};

        // Line indices for 12 edges of the frustum
        inline static constexpr std::array<uint32_t, 24> frustumIndices = {{
            // Near plane edges
            0, 1,  1, 2,  2, 3,  3, 0,
            // Far plane edges
            4, 5,  5, 6,  6, 7,  7, 4,
            // Connecting edges (near to far)
            0, 4,  1, 5,  2, 6,  3, 7
        }};

        // Color scheme for cascade levels
        inline static constexpr std::array<glm::vec4, 4> cascadeColors = {{
            {1.0f, 0.2f, 0.2f, 0.8f},  // Cascade 0: Red
            {1.0f, 0.6f, 0.2f, 0.8f},  // Cascade 1: Orange
            {1.0f, 1.0f, 0.2f, 0.8f},  // Cascade 2: Yellow
            {0.2f, 1.0f, 0.2f, 0.8f},  // Cascade 3: Green
        }};

        // Fixed colors for spot and point lights
        inline static constexpr glm::vec4 spotLightColor = {0.2f, 0.8f, 1.0f, 0.8f};   // Cyan
        inline static constexpr glm::vec4 pointLightColor = {1.0f, 0.2f, 1.0f, 0.8f};  // Magenta

        // Sphere wireframe segments
        static constexpr int SPHERE_SEGMENTS = 32;

        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> frustumShader;
        std::shared_ptr<core::Shader> sphereShader;

        vk::Pipeline frustumPipeline;
        vk::PipelineLayout frustumPipelineLayout;
        vk::Pipeline spherePipeline;
        vk::PipelineLayout spherePipelineLayout;

        // Frustum buffers (for cascade boxes and spot frustums)
        vk::Buffer frustumVertexBuffer;
        vk::DeviceMemory frustumVertexBufferMemory;
        vk::Buffer frustumIndexBuffer;
        vk::DeviceMemory frustumIndexBufferMemory;

        // Sphere buffers (for point light radius)
        vk::Buffer sphereVertexBuffer;
        vk::DeviceMemory sphereVertexBufferMemory;
        vk::Buffer sphereIndexBuffer;
        vk::DeviceMemory sphereIndexBufferMemory;
        uint32_t sphereIndexCount = 0;

        bool initialized = false;

    public:
        explicit ShadowDebugRenderer(core::Device& device, core::SwapChain& swapChain);
        ~ShadowDebugRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<ShadowFrustumRenderData>& shadowDrawList,
                    const glm::mat4& editorView,
                    const glm::mat4& editorProjection) const;

        bool isInitialized() const { return initialized; }

        // Get color for a shadow type/cascade
        static glm::vec4 getColorForShadow(ShadowFrustumType type, uint32_t cascadeIndex);

    private:
        void loadShaders();
        void createPipelines(vk::RenderPass renderPass);
        void createFrustumBuffers();
        void createSphereBuffers();

        void renderFrustum(const vk::CommandBuffer& commandBuffer,
                           const ShadowFrustumRenderData& shadow,
                           const glm::mat4& editorViewProj) const;

        void renderSphere(const vk::CommandBuffer& commandBuffer,
                          const ShadowFrustumRenderData& shadow,
                          const glm::mat4& editorViewProj) const;
    };
}
