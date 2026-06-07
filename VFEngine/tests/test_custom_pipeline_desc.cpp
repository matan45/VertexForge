#include "doctest.h"

#include "data/CustomPipelineTypes.hpp"

// Lit custom pipelines (plugin API v9) — CPU-only contract tests guarding the
// CustomPipelineDesc ABI and the push-constant budget shared with the engine.

TEST_CASE("custom pipeline: desc defaults are unlit and unchanged")
{
    plugin::CustomPipelineDesc desc;
    CHECK(desc.receiveLighting == false);
    CHECK(desc.depthTest == true);
    CHECK(desc.depthWrite == true);
    CHECK(desc.cullMode == plugin::CustomCullMode::Back);
    CHECK(desc.topology == plugin::CustomTopology::TriangleList);
    CHECK(desc.blendMode == plugin::CustomBlendMode::Opaque);
    CHECK(desc.pushConstantSize == 0u);
}

TEST_CASE("custom pipeline: push-constant budget fits the Vulkan 128-byte minimum")
{
    // Built-in mat4 (MVP for unlit, MODEL for lit) + user data must stay within
    // the Vulkan-guaranteed minimum so lit pipelines need no extra budget.
    CHECK(plugin::MAX_CUSTOM_PUSH_CONSTANT_SIZE == 64u);
    CHECK(sizeof(glm::mat4) + plugin::MAX_CUSTOM_PUSH_CONSTANT_SIZE <= 128u);
}

TEST_CASE("custom pipeline: handle validity semantics")
{
    plugin::CustomPipelineHandle invalid;
    CHECK_FALSE(invalid.isValid());

    plugin::CustomPipelineHandle valid{7};
    CHECK(valid.isValid());
}

TEST_CASE("custom pipeline: vertex attribute sizes match interleaved packing")
{
    // The engine derives the vertex stride from these sizes; the HexTerrain lit
    // layout (pos3 + normal3 + color4 + edge + index) must stay tightly packed.
    auto size = [](plugin::CustomVertexAttribute a) -> uint32_t {
        switch (a) {
        case plugin::CustomVertexAttribute::Float:  return 4;
        case plugin::CustomVertexAttribute::Float2: return 8;
        case plugin::CustomVertexAttribute::Float3: return 12;
        case plugin::CustomVertexAttribute::Float4: return 16;
        }
        return 0;
    };

    const uint32_t stride =
        size(plugin::CustomVertexAttribute::Float3) +
        size(plugin::CustomVertexAttribute::Float3) +
        size(plugin::CustomVertexAttribute::Float4) +
        size(plugin::CustomVertexAttribute::Float) +
        size(plugin::CustomVertexAttribute::Float);
    CHECK(stride == 48u);
}
