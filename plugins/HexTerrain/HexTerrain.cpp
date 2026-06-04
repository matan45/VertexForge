#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include "imguiHandler/ImguiWindow.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Civilization-style hexagonal tile overlay rendered through the engine's
// custom render pipeline plugin API. Generates a pointy-top hex grid on the
// CPU (draped over the terrain when one exists), uploads it once, and enqueues
// one draw per frame. All Vulkan work happens engine-side.

// Settings shared between the plugin and its editor window. Held by shared_ptr
// on both sides so destruction order doesn't matter.
struct HexSettings
{
    bool show = true;
    int gridRadius = 12;       // hexes around the origin
    float hexSize = 4.0f;      // center-to-corner distance
    float opacity = 0.65f;
    bool rebuildRequested = false;
};

// Editor control panel — registered via PluginContext::registerEditorWindow.
// draw() runs inside the engine's ImGui frame; ImGui::SetCurrentContext was
// pointed at the engine context during plugin init.
class HexTerrainWindow : public controllers::imguiHandler::ImguiWindow
{
private:
    std::shared_ptr<HexSettings> settings;

public:
    explicit HexTerrainWindow(std::shared_ptr<HexSettings> settings)
        : settings(std::move(settings))
    {
    }

    void draw() override
    {
        if (!ImGui::Begin("Hex Terrain"))
        {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Show Grid (F7)", &settings->show);
        ImGui::SliderFloat("Opacity", &settings->opacity, 0.0f, 1.0f);

        ImGui::SeparatorText("Grid (rebuilds on release)");
        ImGui::SliderInt("Radius", &settings->gridRadius, 1, 40);
        if (ImGui::IsItemDeactivatedAfterEdit()) settings->rebuildRequested = true;
        ImGui::SliderFloat("Hex Size", &settings->hexSize, 1.0f, 16.0f);
        if (ImGui::IsItemDeactivatedAfterEdit()) settings->rebuildRequested = true;

        if (ImGui::Button("Rebuild / Re-drape (F6)"))
        {
            settings->rebuildRequested = true;
        }
        ImGui::SetItemTooltip("Re-samples terrain height — use after loading a scene with terrain");

        ImGui::End();
    }
};

class HexTerrain : public plugin::IPlugin
{
private:
    struct HexVertex
    {
        glm::vec3 position;
        glm::vec4 color;
        float edge;   // 0 at hex center, 1 at the rim — used for border shading
    };

    plugin::PluginContext* ctx = nullptr;
    plugin::CustomPipelineHandle pipeline;
    plugin::CustomMeshHandle mesh;
    std::shared_ptr<HexSettings> settings = std::make_shared<HexSettings>();
    float time = 0.0f;

    static constexpr float HEIGHT_OFFSET = 0.2f;  // lift above terrain to avoid z-fighting

public:
    plugin::PluginInfo getInfo() const override
    {
        return {"HexTerrain", "VertexForge", "Hex tile terrain overlay via custom render pipeline", 1, 0, 0};
    }

    bool onInitialize(plugin::PluginContext* context) override
    {
        ctx = context;

        if (!ctx->hasCapability(std::string(plugin::capability::graphics)))
        {
            ctx->logError("[HexTerrain] graphics capability not available");
            return false;
        }

        if (!createPipeline()) return false;
        if (!buildHexGrid()) return false;

        if (ctx->hasCapability(std::string(plugin::capability::editor)))
        {
            ImGui::SetCurrentContext(ctx->getImGuiContext());
            ctx->registerEditorWindow(std::make_shared<HexTerrainWindow>(settings), "Hex Terrain");
        }

        ctx->logInfo("[HexTerrain] Hex grid ready - F7 toggle, F6 re-drape, Plugins > Hex Terrain for settings");
        return true;
    }

    void onUpdate(float deltaTime) override
    {
        time += deltaTime;

        if (ctx->hasCapability(std::string(plugin::capability::input)))
        {
            if (ctx->isKeyPressed(296)) // F7
            {
                settings->show = !settings->show;
                ctx->logInfo(std::string("[HexTerrain] Grid ") + (settings->show ? "shown" : "hidden"));
            }

            // F6: rebuild the grid — re-drapes over the terrain of the currently
            // loaded scene (plugin init runs before any scene exists)
            if (ctx->isKeyPressed(295))
            {
                settings->rebuildRequested = true;
            }
        }

        if (settings->rebuildRequested)
        {
            settings->rebuildRequested = false;
            ctx->destroyCustomMesh(mesh);
            mesh = {};
            if (buildHexGrid())
            {
                ctx->logInfo("[HexTerrain] Grid rebuilt (re-draped over terrain)");
            }
        }

        if (settings->show && pipeline.isValid() && mesh.isValid())
        {
            struct { float time; float opacity; float pad[2]; } user{time, settings->opacity, {0, 0}};
            std::vector<std::byte> pushConstants(sizeof(user));
            std::memcpy(pushConstants.data(), &user, sizeof(user));
            ctx->drawCustomMesh(pipeline, mesh, glm::mat4(1.0f), pushConstants);
        }
    }

    void onShutdown() override
    {
        // Handles (and the editor window) are also cleaned up automatically on unload
        ctx->destroyCustomMesh(mesh);
        ctx->destroyCustomPipeline(pipeline);
        ctx->logInfo("[HexTerrain] shutdown");
    }

private:
    bool createPipeline()
    {
        plugin::CustomPipelineDesc desc;
        desc.glslSource = R"(#type VERTEX
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inEdge;
layout(push_constant) uniform PC { mat4 mvp; float time; float opacity; } pc;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out float fragEdge;
void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragEdge = inEdge;
}

#type FRAGMENT
#version 450
layout(push_constant) uniform PC { mat4 mvp; float time; float opacity; } pc;
layout(location = 0) in vec4 fragColor;
layout(location = 1) in float fragEdge;
layout(location = 0) out vec4 outColor;
void main() {
    // Darkened rim per hex; subtle pulse keeps the overlay readable in motion
    float border = smoothstep(0.82, 0.97, fragEdge);
    float pulse = 0.9 + 0.1 * sin(pc.time * 2.0);
    vec3 tile = fragColor.rgb * (1.0 - 0.45 * border) * pulse;
    outColor = vec4(tile, fragColor.a * pc.opacity);
}
)";
        desc.vertexLayout = {
            plugin::CustomVertexAttribute::Float3,  // position
            plugin::CustomVertexAttribute::Float4,  // color
            plugin::CustomVertexAttribute::Float    // edge factor
        };
        desc.cullMode = plugin::CustomCullMode::None;
        desc.blendMode = plugin::CustomBlendMode::AlphaBlend;
        desc.depthWrite = false;   // overlay: test against scene depth, don't occlude it
        desc.pushConstantSize = 16;

        pipeline = ctx->createCustomPipeline(desc);
        if (!pipeline.isValid())
        {
            ctx->logError("[HexTerrain] pipeline creation failed");
            return false;
        }
        return true;
    }

    float sampleHeight(float worldX, float worldZ) const
    {
        if (ctx->hasCapability(std::string(plugin::capability::terrain)))
        {
            auto result = ctx->getTerrainHeightAt(worldX, worldZ);
            if (result.valid) return result.height + HEIGHT_OFFSET;
        }
        return HEIGHT_OFFSET;
    }

    // Deterministic tile color from axial coordinates — stands in for real
    // gameplay terrain types (grass/forest/desert/water/mountain).
    static glm::vec4 tileColor(int q, int r)
    {
        uint32_t h = static_cast<uint32_t>(q * 374761393 + r * 668265263);
        h = (h ^ (h >> 13)) * 1274126177u;
        switch ((h ^ (h >> 16)) % 5)
        {
        case 0:  return {0.30f, 0.62f, 0.25f, 1.0f};  // grass
        case 1:  return {0.13f, 0.38f, 0.16f, 1.0f};  // forest
        case 2:  return {0.78f, 0.68f, 0.38f, 1.0f};  // desert
        case 3:  return {0.18f, 0.42f, 0.72f, 1.0f};  // water
        default: return {0.52f, 0.50f, 0.52f, 1.0f};  // mountain
        }
    }

    bool buildHexGrid()
    {
        const int gridRadius = settings->gridRadius;
        const float hexSize = settings->hexSize;

        std::vector<HexVertex> vertices;
        std::vector<uint32_t> indices;
        const int hexCount = 3 * gridRadius * (gridRadius + 1) + 1;
        vertices.reserve(hexCount * 7);
        indices.reserve(hexCount * 18);

        const float sqrt3 = std::sqrt(3.0f);

        for (int q = -gridRadius; q <= gridRadius; ++q)
        {
            for (int r = -gridRadius; r <= gridRadius; ++r)
            {
                if (std::abs(q + r) > gridRadius) continue;  // hexagonal disc

                // Pointy-top axial -> world
                const float cx = hexSize * sqrt3 * (q + r * 0.5f);
                const float cz = hexSize * 1.5f * r;
                const glm::vec4 color = tileColor(q, r);

                const uint32_t centerIndex = static_cast<uint32_t>(vertices.size());
                vertices.push_back({{cx, sampleHeight(cx, cz), cz}, color, 0.0f});

                for (int corner = 0; corner < 6; ++corner)
                {
                    const float angle = glm::radians(60.0f * corner - 30.0f);
                    const float x = cx + hexSize * std::cos(angle);
                    const float z = cz + hexSize * std::sin(angle);
                    vertices.push_back({{x, sampleHeight(x, z), z}, color, 1.0f});
                }

                for (uint32_t corner = 0; corner < 6; ++corner)
                {
                    indices.push_back(centerIndex);
                    indices.push_back(centerIndex + 1 + corner);
                    indices.push_back(centerIndex + 1 + (corner + 1) % 6);
                }
            }
        }

        plugin::CustomMeshData meshData;
        meshData.vertexData.resize(vertices.size() * sizeof(HexVertex));
        std::memcpy(meshData.vertexData.data(), vertices.data(), meshData.vertexData.size());
        meshData.indices = std::move(indices);
        meshData.vertexCount = static_cast<uint32_t>(vertices.size());

        mesh = ctx->uploadCustomMesh(std::move(meshData));
        if (!mesh.isValid())
        {
            ctx->logError("[HexTerrain] mesh upload failed");
            return false;
        }
        return true;
    }
};

VF_IMPLEMENT_PLUGIN(HexTerrain)
