#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include "imguiHandler/ImguiWindow.hpp"
#include <imgui.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
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

    // Picking state (written by the plugin, read by the window)
    bool hasHovered = false;
    int hoveredQ = 0;
    int hoveredR = 0;
    bool hasSelected = false;
    int selectedQ = 0;
    int selectedR = 0;

    // Height-field collider demo
    bool colliderToggleRequested = false;
    bool hasCollider = false;
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

        ImGui::SeparatorText("Physics");
        if (ImGui::Button(settings->hasCollider ? "Destroy HeightField Collider" : "Create HeightField Collider"))
        {
            settings->colliderToggleRequested = true;
        }
        ImGui::SetItemTooltip("Static Jolt height-field body matching the hex grid surface\n(plugin physics API demo - raycast it with Physics::raycast)");

        ImGui::SeparatorText("Picking (camera ray -> hex math)");
        if (settings->hasHovered)
            ImGui::Text("Hovered:  q=%d  r=%d", settings->hoveredQ, settings->hoveredR);
        else
            ImGui::TextDisabled("Hovered:  (move cursor over terrain)");
        if (settings->hasSelected)
            ImGui::Text("Selected: q=%d  r=%d", settings->selectedQ, settings->selectedR);
        else
            ImGui::TextDisabled("Selected: (left-click a hex)");

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
        float edge;       // 0 at hex center, 1 at the rim — used for border shading
        float hexIndex;   // flat per-hex id, compared against the picked hex in the shader
    };

    plugin::PluginContext* ctx = nullptr;
    plugin::CustomPipelineHandle pipeline;
    plugin::CustomMeshHandle mesh;
    std::shared_ptr<HexSettings> settings = std::make_shared<HexSettings>();
    std::unordered_map<int64_t, int> axialToIndex;   // (q,r) -> hexIndex of the current grid
    entt::entity colliderEntity = entt::null;
    bool prevMouseDown = false;
    float time = 0.0f;

    static int64_t axialKey(int q, int r) { return (static_cast<int64_t>(q) << 32) ^ (r & 0xffffffffLL); }

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
            settings->hasSelected = false;
            if (buildHexGrid())
            {
                ctx->logInfo("[HexTerrain] Grid rebuilt (re-draped over terrain)");
            }
        }

        if (settings->colliderToggleRequested)
        {
            settings->colliderToggleRequested = false;
            toggleHeightFieldCollider();
        }

        updatePicking();

        if (settings->show && pipeline.isValid() && mesh.isValid())
        {
            const float hovered = settings->hasHovered
                ? static_cast<float>(axialToIndex[axialKey(settings->hoveredQ, settings->hoveredR)]) : -1.0f;
            const float selected = settings->hasSelected
                ? static_cast<float>(axialToIndex[axialKey(settings->selectedQ, settings->selectedR)]) : -1.0f;

            struct { float time; float opacity; float hovered; float selected; } user{
                time, settings->opacity, hovered, selected};
            std::vector<std::byte> pushConstants(sizeof(user));
            std::memcpy(pushConstants.data(), &user, sizeof(user));
            ctx->drawCustomMesh(pipeline, mesh, glm::mat4(1.0f), pushConstants);
        }
    }

    // "Collider-free" hex picking, the Civ way: cast the camera->cursor ray
    // (screenToWorldRay works in both edit and play mode) and march it against
    // the same height function the grid was draped with — pure math, no physics.
    void updatePicking()
    {
        settings->hasHovered = false;
        if (!settings->show) return;
        if (!ctx->hasCapability(std::string(plugin::capability::input))) return;

        glm::vec3 origin, direction;
        if (!ctx->screenToWorldRay(ctx->getViewportMousePosition(), origin, direction)) return;

        glm::vec3 hitPos;
        if (raymarchHeight(origin, direction, hitPos))
        {
            int q = 0, r = 0;
            worldToAxial(hitPos.x, hitPos.z, q, r);
            if (axialToIndex.contains(axialKey(q, r)))
            {
                settings->hasHovered = true;
                settings->hoveredQ = q;
                settings->hoveredR = r;
            }
        }

        if (ctx->hasCapability(std::string(plugin::capability::input)))
        {
            const bool mouseDown = ctx->isMouseButtonDown(0);
            if (mouseDown && !prevMouseDown && settings->hasHovered)
            {
                settings->hasSelected = true;
                settings->selectedQ = settings->hoveredQ;
                settings->selectedR = settings->hoveredR;
                ctx->logInfo("[HexTerrain] Selected hex q=" + std::to_string(settings->selectedQ)
                             + " r=" + std::to_string(settings->selectedR));
            }
            prevMouseDown = mouseDown;
        }
    }

    // Physics API demo: a static Jolt height-field body matching the hex grid
    // surface — sampled from the same height function the visual grid uses.
    void toggleHeightFieldCollider()
    {
        if (!ctx->hasCapability(std::string(plugin::capability::physics)))
        {
            ctx->logWarning("[HexTerrain] physics capability not available");
            return;
        }

        if (settings->hasCollider)
        {
            ctx->destroyHeightFieldBody(colliderEntity, 0, 0);
            ctx->getRegistry().destroy(colliderEntity);
            colliderEntity = entt::null;
            settings->hasCollider = false;
            ctx->logInfo("[HexTerrain] HeightField collider destroyed");
            return;
        }

        constexpr uint32_t SAMPLES = 64;
        const float extent = (settings->gridRadius + 1) * settings->hexSize * 1.8f;
        const float spacing = (2.0f * extent) / (SAMPLES - 1);

        std::vector<float> heights(static_cast<size_t>(SAMPLES) * SAMPLES);
        for (uint32_t zi = 0; zi < SAMPLES; ++zi)
        {
            for (uint32_t xi = 0; xi < SAMPLES; ++xi)
            {
                const float x = -extent + xi * spacing;
                const float z = -extent + zi * spacing;
                heights[static_cast<size_t>(zi) * SAMPLES + xi] = sampleHeight(x, z);
            }
        }

        colliderEntity = ctx->getRegistry().create();
        if (ctx->createHeightFieldBody(colliderEntity, 0, 0, std::move(heights), SAMPLES,
                                       glm::vec3(-extent, 0.0f, -extent), spacing))
        {
            settings->hasCollider = true;
            ctx->logInfo("[HexTerrain] HeightField collider created (" + std::to_string(SAMPLES) + "x"
                         + std::to_string(SAMPLES) + " samples, extent " + std::to_string(extent) + ")");
        }
        else
        {
            ctx->getRegistry().destroy(colliderEntity);
            colliderEntity = entt::null;
            ctx->logError("[HexTerrain] HeightField collider creation failed");
        }
    }

    // March the pick ray until it dips below the height function the grid uses,
    // then refine by bisection. Falls back to a flat plane when no terrain exists.
    bool raymarchHeight(const glm::vec3& origin, const glm::vec3& direction, glm::vec3& outHit) const
    {
        constexpr float MAX_DISTANCE = 2000.0f;
        constexpr int COARSE_STEPS = 256;
        const float stepSize = MAX_DISTANCE / COARSE_STEPS;

        float prevT = 0.0f;
        bool prevAbove = origin.y > sampleHeight(origin.x, origin.z);
        if (!prevAbove) return false;   // camera below the surface

        for (int i = 1; i <= COARSE_STEPS; ++i)
        {
            const float t = i * stepSize;
            const glm::vec3 p = origin + direction * t;
            if (p.y <= sampleHeight(p.x, p.z))
            {
                // Bisect [prevT, t] for a precise crossing
                float lo = prevT, hi = t;
                for (int j = 0; j < 16; ++j)
                {
                    const float mid = 0.5f * (lo + hi);
                    const glm::vec3 m = origin + direction * mid;
                    if (m.y > sampleHeight(m.x, m.z)) lo = mid; else hi = mid;
                }
                outHit = origin + direction * (0.5f * (lo + hi));
                return true;
            }
            prevT = t;
        }
        return false;
    }

    // Inverse of the pointy-top axial -> world mapping in buildHexGrid, with cube rounding.
    void worldToAxial(float x, float z, int& outQ, int& outR) const
    {
        const float size = settings->hexSize;
        const float qf = (std::sqrt(3.0f) / 3.0f * x - z / 3.0f) / size;
        const float rf = (2.0f / 3.0f * z) / size;

        // Cube rounding (q + r + s = 0)
        const float sf = -qf - rf;
        float rq = std::round(qf), rr = std::round(rf), rs = std::round(sf);
        const float dq = std::abs(rq - qf), dr = std::abs(rr - rf), ds = std::abs(rs - sf);
        if (dq > dr && dq > ds)      rq = -rr - rs;
        else if (dr > ds)            rr = -rq - rs;

        outQ = static_cast<int>(rq);
        outR = static_cast<int>(rr);
    }

    void onShutdown() override
    {
        // Handles (window, pipelines, height-field body) are also cleaned up
        // automatically on unload — but the collider's carrier entity is ours.
        if (colliderEntity != entt::null)
        {
            ctx->destroyHeightFieldBody(colliderEntity, 0, 0);
            ctx->getRegistry().destroy(colliderEntity);
            colliderEntity = entt::null;
        }
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
layout(location = 3) in float inHexIndex;
layout(push_constant) uniform PC { mat4 mvp; float time; float opacity; float hovered; float selected; } pc;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out float fragEdge;
layout(location = 2) flat out float fragHexIndex;
void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragEdge = inEdge;
    fragHexIndex = inHexIndex;
}

#type FRAGMENT
#version 450
layout(push_constant) uniform PC { mat4 mvp; float time; float opacity; float hovered; float selected; } pc;
layout(location = 0) in vec4 fragColor;
layout(location = 1) in float fragEdge;
layout(location = 2) flat in float fragHexIndex;
layout(location = 0) out vec4 outColor;
void main() {
    // Darkened rim per hex; subtle pulse keeps the overlay readable in motion
    float border = smoothstep(0.82, 0.97, fragEdge);
    float pulse = 0.9 + 0.1 * sin(pc.time * 2.0);
    vec3 tile = fragColor.rgb * (1.0 - 0.45 * border) * pulse;
    float alpha = fragColor.a * pc.opacity;

    bool isHovered  = pc.hovered  >= 0.0 && abs(fragHexIndex - pc.hovered)  < 0.5;
    bool isSelected = pc.selected >= 0.0 && abs(fragHexIndex - pc.selected) < 0.5;
    if (isSelected) {
        tile = mix(tile, vec3(1.0, 0.85, 0.25), 0.55);   // gold: selected
        alpha = min(alpha + 0.25, 1.0);
    } else if (isHovered) {
        tile = mix(tile, vec3(1.0), 0.30);               // white: hovered
        alpha = min(alpha + 0.15, 1.0);
    }

    outColor = vec4(tile, alpha);
}
)";
        desc.vertexLayout = {
            plugin::CustomVertexAttribute::Float3,  // position
            plugin::CustomVertexAttribute::Float4,  // color
            plugin::CustomVertexAttribute::Float,   // edge factor
            plugin::CustomVertexAttribute::Float    // hex index (flat)
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
        axialToIndex.clear();
        axialToIndex.reserve(hexCount);

        const float sqrt3 = std::sqrt(3.0f);
        int hexCounter = 0;

        for (int q = -gridRadius; q <= gridRadius; ++q)
        {
            for (int r = -gridRadius; r <= gridRadius; ++r)
            {
                if (std::abs(q + r) > gridRadius) continue;  // hexagonal disc

                // Pointy-top axial -> world
                const float cx = hexSize * sqrt3 * (q + r * 0.5f);
                const float cz = hexSize * 1.5f * r;
                const glm::vec4 color = tileColor(q, r);
                const float hexIndex = static_cast<float>(hexCounter);
                axialToIndex[axialKey(q, r)] = hexCounter++;

                const uint32_t centerIndex = static_cast<uint32_t>(vertices.size());
                vertices.push_back({{cx, sampleHeight(cx, cz), cz}, color, 0.0f, hexIndex});

                for (int corner = 0; corner < 6; ++corner)
                {
                    const float angle = glm::radians(60.0f * corner - 30.0f);
                    const float x = cx + hexSize * std::cos(angle);
                    const float z = cz + hexSize * std::sin(angle);
                    vertices.push_back({{x, sampleHeight(x, z), z}, color, 1.0f, hexIndex});
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
