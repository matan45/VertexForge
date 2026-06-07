#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace plugin {

    struct CustomPipelineHandle {
        uint64_t id = 0;
        bool isValid() const { return id != 0; }
    };

    struct CustomMeshHandle {
        uint64_t id = 0;
        bool isValid() const { return id != 0; }
    };

    // Vertex attributes are interleaved and tightly packed, assigned to
    // sequential shader locations (0, 1, 2, ...) in declaration order.
    enum class CustomVertexAttribute : uint32_t {
        Float  = 0,   //  4 bytes -> float
        Float2 = 1,   //  8 bytes -> vec2
        Float3 = 2,   // 12 bytes -> vec3
        Float4 = 3    // 16 bytes -> vec4
    };

    enum class CustomCullMode : uint32_t { None = 0, Back = 1, Front = 2 };
    enum class CustomTopology : uint32_t { TriangleList = 0, LineList = 1 };
    enum class CustomBlendMode : uint32_t { Opaque = 0, AlphaBlend = 1 };

    // Maximum user push-constant bytes. The engine reserves the first 64 bytes
    // of the push-constant block for the built-in MVP matrix; 64 + 64 stays
    // within the Vulkan-guaranteed 128-byte minimum.
    inline constexpr uint32_t MAX_CUSTOM_PUSH_CONSTANT_SIZE = 64;

    // Description of a plugin-owned graphics pipeline. The engine compiles the
    // GLSL and creates the pipeline against the scene color/depth formats; the
    // plugin only ever sees the returned handle — no Vulkan objects cross the
    // DLL boundary.
    //
    // glslSource uses the engine multi-stage format:
    //   #type VERTEX
    //   ...
    //   #type FRAGMENT
    //   ...
    // The engine pushes a mat4 at push-constant offset 0. User push-constant
    // data (pushConstantSize bytes, max MAX_CUSTOM_PUSH_CONSTANT_SIZE) follows at
    // offset 64 and is visible to both stages.
    //
    // Unlit (receiveLighting == false, default): the built-in mat4 is the MVP
    // (projection * view * model). The shader declares:
    //   layout(push_constant) uniform PC { mat4 mvp; <user fields> };
    //
    // Lit (receiveLighting == true): the built-in mat4 is the MODEL matrix and
    // the engine binds its scene lighting descriptor sets to the pipeline:
    //   set 0: CameraUBO (b0) + IBL irradiance/prefilter/brdfLUT (b1-b3)
    //   set 6: directional/point/spot light SSBOs (b0-b2) + LightCounts UBO (b3)
    //   set 7: cluster grid params UBO (b0)
    //   set 8: cluster light grid (b0) + light index list (b1)
    //   set 9: shadow data (b0) + VSM page table (b1)
    //   set 10: VSM physical pool shadow/depth samplers (b0-b1)
    // Sets 1-5 are empty placeholders. Declarations can be copied verbatim from
    // resources/shaders/gpudriven/mesh_terrain.glsl; the shared chunks under
    // resources/shaders/common/ are #include-able from plugin GLSL. The shader
    // computes clip position itself:
    //   gl_Position = camera.projection * camera.view * pc.model * vec4(pos, 1);
    // The RT shadow mask (set 13) is NOT available to custom pipelines; VSM only.
    // Note: normals transformed with mat3(pc.model) are only correct for
    // uniform scale. See plugins/HexTerrain for a full lit reference shader.
    struct CustomPipelineDesc {
        std::string glslSource;
        std::vector<CustomVertexAttribute> vertexLayout;
        bool depthTest = true;
        bool depthWrite = true;
        CustomCullMode cullMode = CustomCullMode::Back;
        CustomTopology topology = CustomTopology::TriangleList;
        CustomBlendMode blendMode = CustomBlendMode::Opaque;
        uint32_t pushConstantSize = 0;   // user bytes after the built-in mat4
        bool receiveLighting = false;    // bind scene lighting sets; mat4 becomes MODEL
    };

    // Geometry uploaded once and stored device-local. indices may be empty for
    // non-indexed draws (vertexCount vertices are drawn instead).
    struct CustomMeshData {
        std::vector<std::byte> vertexData;
        std::vector<uint32_t> indices;
        uint32_t vertexCount = 0;
    };

    // One draw enqueued for the current frame. pushConstants must match the
    // pipeline's pushConstantSize.
    struct CustomDrawItem {
        CustomPipelineHandle pipeline;
        CustomMeshHandle mesh;
        glm::mat4 model{1.0f};
        std::vector<std::byte> pushConstants;
    };

}
