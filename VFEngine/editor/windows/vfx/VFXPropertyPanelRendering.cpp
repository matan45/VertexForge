#include "VFXPropertyPanel.hpp"
#include "imgui.h"
#include "../../dragdrop/AssetDropTarget.hpp"
#include <nfd/FileDialog.hpp>
#include <vfx/VFXBurstTypes.hpp>
#include <vfx/VFXEventTypes.hpp>
#include <vfx/VFXEmitterSections.hpp>
#include <vfx/VFXKillVolume.hpp>
#include <vfx/VFXShapeProperties.hpp>
#include <vfx/VFXShapeTypes.hpp>
#include <vfx/VFXOrientationMode.hpp>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <unordered_set>

namespace editor::vfxeditor
{
    bool VFXPropertyPanel::drawScalarProperty(const char* label, vfx::VFXProperty& prop, float inputWidth, float step)
    {
        constexpr float labelWidth = 120.0f;

        switch (prop.type)
        {
        case vfx::VFXPropertyType::Float:
            if (auto* val = std::get_if<float>(&prop.value))
            {
                ImGui::Text("%s", label);
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(inputWidth);
                return ImGui::DragFloat("##v", val, step, prop.min, prop.max, "%.2f");
            }
            break;

        case vfx::VFXPropertyType::Int:
            if (auto* val = std::get_if<int32_t>(&prop.value))
            {
                ImGui::Text("%s", label);
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(inputWidth);
                return ImGui::DragInt("##v", val, 1,
                                      static_cast<int>(prop.min), static_cast<int>(prop.max));
            }
            break;

        case vfx::VFXPropertyType::Bool:
            if (auto* val = std::get_if<bool>(&prop.value))
            {
                ImGui::Text("%s", label);
                ImGui::SameLine(labelWidth);
                return ImGui::Checkbox("##v", val);
            }
            break;

        case vfx::VFXPropertyType::Vec3:
            if (auto* val = std::get_if<glm::vec3>(&prop.value))
            {
                ImGui::Text("%s", label);
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(inputWidth * 2.4f);
                return ImGui::DragFloat3("##v", &val->x, step, prop.min, prop.max, "%.2f");
            }
            break;

        default:
            break;
        }

        return false;
    }

    void VFXPropertyPanel::drawCoreProperties(vfx::VFXNode& node)
    {
        constexpr float inputWidth = 80.0f;

        struct CoreEntry { const char* key; const char* label; float step; };
        static constexpr CoreEntry entries[] = {
            {"spawnRate",     "Spawn Rate", 0.1f},
            {"lifetime",      "Lifetime",   0.1f},
            {"startSize",     "Start Size", 0.1f},
            {"startVelocity", "Velocity",   0.1f},
            {"looping",       "Looping",    0.1f},
        };

        for (const auto& entry : entries)
        {
            auto it = node.properties.find(entry.key);
            if (it == node.properties.end()) continue;

            ImGui::PushID(entry.key);
            if (drawScalarProperty(entry.label, it->second, inputWidth, entry.step))
                notifyChanged();
            ImGui::PopID();
        }

        if (auto colorIt = node.properties.find("startColor"); colorIt != node.properties.end())
        {
            if (auto* val = std::get_if<glm::vec4>(&colorIt->second.value))
            {
                ImGui::Text("Start Color");
                ImGui::SameLine(120.0f);
                ImGui::SetNextItemWidth(inputWidth * 2.4f);
                if (ImGui::ColorEdit4("##startColor", &val->x))
                    notifyChanged();
            }
        }

        auto textureIt = node.properties.find("texture");
        if (textureIt == node.properties.end()) return;

        auto* textureVal = std::get_if<std::string>(&textureIt->second.value);
        if (!textureVal) return;

        ImGui::Text("Texture");
        ImGui::SameLine(120.0f);
        std::string display = textureVal->empty() ? "(none)" : std::filesystem::path(*textureVal).filename().string();
        char buf[256];
        std::strncpy(buf, display.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::SetNextItemWidth(inputWidth * 1.8f);
        ImGui::InputText("##coreTexture", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
        if (auto dropped = windows::acceptAssetDropOnLastItem("VFXTextureDrop", {".vfimage"}))
        {
            *textureVal = *dropped;
            notifyChanged();
        }
        ImGui::SameLine();
        if (ImGui::Button("...##textureBrowse"))
        {
            nfd::FileDialog dialog;
            std::string path = dialog.openFileDialog({
                {L"VF Image", L"*.vfImage"}
            });
            if (!path.empty())
            {
                *textureVal = path;
                notifyChanged();
            }
        }
        if (!textureVal->empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("X##textureClear"))
            {
                textureVal->clear();
                notifyChanged();
            }
        }
    }

    void VFXPropertyPanel::drawSpawnVarianceProperties(vfx::VFXNode& node)
    {
        constexpr float inputWidth = 80.0f;

        struct VarianceEntry { const char* key; const char* label; float step; };
        static constexpr VarianceEntry entries[] = {
            {"sizeVariance",            "Size +/-",       0.01f},
            {"lifetimeVariance",        "Lifetime +/-",   0.01f},
            {"speedVariance",           "Speed +/-",      0.01f},
            {"rotationVariance",        "Rotation +/-",   1.0f},
            {"angularVelocityVariance", "Ang Vel +/-",    1.0f},
            {"colorValueVariance",      "Color Val +/-",  0.01f},
            {"alphaVariance",           "Alpha +/-",      0.01f},
        };

        for (const auto& entry : entries)
        {
            auto it = node.properties.find(entry.key);
            if (it == node.properties.end()) continue;

            ImGui::PushID(entry.key);
            if (drawScalarProperty(entry.label, it->second, inputWidth, entry.step))
                notifyChanged();
            ImGui::PopID();
        }
    }

    void VFXPropertyPanel::drawForceProperties(vfx::VFXNode& node)
    {
        struct ForceEntry { const char* key; const char* label; float step; };

        auto drawEntries = [&](const ForceEntry* entries, size_t count)
        {
            constexpr float inputWidth = 80.0f;
            for (size_t i = 0; i < count; ++i)
            {
                const auto& entry = entries[i];
                auto it = node.properties.find(entry.key);
                if (it == node.properties.end()) continue;

                ImGui::PushID(entry.key);
                if (drawScalarProperty(entry.label, it->second, inputWidth, entry.step))
                    notifyChanged();
                ImGui::PopID();
            }
        };

        switch (node.type)
        {
        case vfx::VFXNodeType::ForceGravity: {
            ImGui::Text("Gravity");
            ImGui::Separator();
            static constexpr ForceEntry entries[] = {
                {"direction",  "Direction",   0.1f},
                {"strength",   "Strength",    0.1f},
                {"localSpace", "Local Space", 0.1f},
            };
            drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::VFXNodeType::ForceWind: {
            ImGui::Text("Wind");
            ImGui::Separator();
            static constexpr ForceEntry entries[] = {
                {"direction",      "Direction",       0.1f},
                {"strength",       "Strength",        0.1f},
                {"noiseStrength",  "Noise Strength",  0.1f},
                {"noiseFrequency", "Noise Frequency", 0.1f},
                {"localSpace",     "Local Space",     0.1f},
            };
            drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::VFXNodeType::ForceTurbulence: {
            ImGui::Text("Turbulence");
            ImGui::Separator();
            static constexpr ForceEntry entries[] = {
                {"strength",    "Strength",     0.1f},
                {"frequency",   "Frequency",    0.1f},
                {"scrollSpeed", "Scroll Speed", 0.1f},
                {"octaves",     "Octaves",      0.1f},
                {"localSpace",  "Local Space",  0.1f},
            };
            drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::VFXNodeType::ForceVortex: {
            ImGui::Text("Vortex");
            ImGui::Separator();
            static constexpr ForceEntry entries[] = {
                {"axis",       "Axis",        0.1f},
                {"center",     "Center",      0.1f},
                {"strength",   "Strength",    0.1f},
                {"radialPull", "Radial Pull", 0.1f},
                {"localSpace", "Local Space", 0.1f},
            };
            drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::VFXNodeType::ForceDrag: {
            ImGui::Text("Drag");
            ImGui::Separator();
            static constexpr ForceEntry entries[] = {
                {"linearCoeff",    "Linear",      0.05f},
                {"quadraticCoeff", "Quadratic",   0.05f},
                {"localSpace",     "Local Space", 0.1f},
            };
            drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::VFXNodeType::ForcePointAttractor: {
            ImGui::Text("Point Attractor");
            ImGui::Separator();
            static constexpr ForceEntry entries[] = {
                {"position",     "Position",       0.1f},
                {"strength",     "Strength",       0.1f},
                {"radius",       "Radius",         0.1f},
                {"falloff",      "Falloff",        0.05f},
                {"killAtCenter", "Kill At Center", 0.1f},
                {"localSpace",   "Local Space",    0.1f},
            };
            drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::VFXNodeType::ForceCurlNoise: {
            ImGui::Text("Curl Noise");
            ImGui::Separator();
            static constexpr ForceEntry entries[] = {
                {"strength",    "Strength",     0.1f},
                {"frequency",   "Frequency",    0.1f},
                {"scrollSpeed", "Scroll Speed", 0.1f},
                {"octaves",     "Octaves",      0.1f},
                {"localSpace",  "Local Space",  0.1f},
            };
            drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::VFXNodeType::ForceKillVolume: {
            constexpr float inputWidth = 80.0f;
            ImGui::Text("Kill Volume");
            ImGui::Separator();

            vfx::KillVolumeShape activeShape = vfx::KillVolumeShape::Plane;
            auto shapeIt = node.properties.find("shape");
            if (shapeIt != node.properties.end())
            {
                if (auto* val = std::get_if<std::string>(&shapeIt->second.value))
                    activeShape = vfx::stringToKillVolumeShape(*val);
            }

            const char* shapeItems[] = {"Plane", "Sphere", "Box"};
            int currentShape = static_cast<int>(activeShape);
            ImGui::Text("Shape");
            ImGui::SameLine(120.0f);
            ImGui::SetNextItemWidth(inputWidth * 1.8f);
            if (ImGui::Combo("##killVolumeShape", &currentShape, shapeItems, 3))
            {
                activeShape = static_cast<vfx::KillVolumeShape>(currentShape);
                node.properties["shape"] = vfx::VFXProperty{
                    "shape", vfx::VFXPropertyType::String,
                    std::string(vfx::killVolumeShapeToString(activeShape)), 0.0f, 1.0f
                };
                notifyChanged();
            }

            static constexpr ForceEntry commonEntries[] = {
                {"center", "Center", 0.1f},
            };
            drawEntries(commonEntries, sizeof(commonEntries) / sizeof(commonEntries[0]));

            switch (activeShape)
            {
            case vfx::KillVolumeShape::Plane: {
                static constexpr ForceEntry entries[] = {
                    {"normal", "Normal", 0.1f},
                };
                drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
                break;
            }
            case vfx::KillVolumeShape::Sphere: {
                static constexpr ForceEntry entries[] = {
                    {"radius", "Radius", 0.1f},
                };
                drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
                break;
            }
            case vfx::KillVolumeShape::Box: {
                static constexpr ForceEntry entries[] = {
                    {"halfExtents", "Half Extents", 0.1f},
                };
                drawEntries(entries, sizeof(entries) / sizeof(entries[0]));
                break;
            }
            default:
                break;
            }

            static constexpr ForceEntry toggles[] = {
                {"invert",     "Invert",      0.1f},
                {"localSpace", "Local Space", 0.1f},
            };
            drawEntries(toggles, sizeof(toggles) / sizeof(toggles[0]));
            break;
        }
        default:
            break;
        }
    }

    void VFXPropertyPanel::drawShapeProperties(vfx::VFXNode& node)
    {
        constexpr float inputWidth = 80.0f;

        ImGui::Text("Shape");
        ImGui::Separator();

        vfx::ShapeType activeType = vfx::ShapeType::Point;
        auto shapeIt = node.properties.find("shapeType");
        if (shapeIt != node.properties.end())
        {
            if (auto* val = std::get_if<std::string>(&shapeIt->second.value))
                activeType = vfx::stringToShapeType(*val);
        }

        const char* shapeItems[] = {"Point", "Sphere", "Cone", "Box", "Torus"};
        int currentShape = static_cast<int>(activeType);
        ImGui::Text("Shape Type");
        ImGui::SameLine(120.0f);
        ImGui::SetNextItemWidth(inputWidth * 1.8f);
        if (ImGui::Combo("##shapeType", &currentShape, shapeItems, 5))
        {
            activeType = static_cast<vfx::ShapeType>(currentShape);
            vfx::applyShapeTypeProperties(node, activeType);
            notifyChanged();
        }

        auto emitIt = node.properties.find("emitFrom");
        if (emitIt != node.properties.end())
        {
            if (auto* val = std::get_if<std::string>(&emitIt->second.value))
            {
                const char* emitItems[] = {"Volume", "Surface"};
                int currentEmit = static_cast<int>(vfx::stringToEmitFrom(*val));
                ImGui::Text("Emit From");
                ImGui::SameLine(120.0f);
                ImGui::SetNextItemWidth(inputWidth * 1.8f);
                if (ImGui::Combo("##emitFrom", &currentEmit, emitItems, 2))
                {
                    *val = vfx::emitFromToString(static_cast<vfx::EmitFrom>(currentEmit));
                    notifyChanged();
                }
            }
        }

        auto randomIt = node.properties.find("randomDirection");
        if (randomIt != node.properties.end())
        {
            ImGui::PushID("randomDirection");
            if (drawScalarProperty("Random Direction", randomIt->second, inputWidth))
                notifyChanged();
            ImGui::PopID();
        }

        struct ShapeEntry { const char* key; const char* label; float step; };
        auto drawDimensionEntries = [&](const ShapeEntry* entries, size_t count)
        {
            for (size_t i = 0; i < count; ++i)
            {
                const auto& entry = entries[i];
                auto it = node.properties.find(entry.key);
                if (it == node.properties.end()) continue;

                ImGui::PushID(entry.key);
                if (drawScalarProperty(entry.label, it->second, inputWidth, entry.step))
                    notifyChanged();
                ImGui::PopID();
            }
        };

        switch (activeType)
        {
        case vfx::ShapeType::Sphere: {
            static constexpr ShapeEntry entries[] = {
                {"radius", "Radius", 0.1f},
            };
            drawDimensionEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::ShapeType::Cone: {
            static constexpr ShapeEntry entries[] = {
                {"radius", "Radius", 0.1f},
                {"height", "Height", 0.1f},
                {"angle",  "Angle",  0.01f},
            };
            drawDimensionEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::ShapeType::Box: {
            static constexpr ShapeEntry entries[] = {
                {"halfExtents", "Half Extents", 0.1f},
            };
            drawDimensionEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::ShapeType::Torus: {
            static constexpr ShapeEntry entries[] = {
                {"majorRadius", "Major Radius", 0.1f},
                {"minorRadius", "Minor Radius", 0.1f},
            };
            drawDimensionEntries(entries, sizeof(entries) / sizeof(entries[0]));
            break;
        }
        case vfx::ShapeType::Point:
        default:
            break;
        }
    }

    void VFXPropertyPanel::drawFlipbookProperties(vfx::VFXNode& node)
    {
        struct FlipbookEntry { const char* key; const char* label; };
        static constexpr FlipbookEntry entries[] = {
            {"flipbookColumns",     "Columns"},
            {"flipbookRows",        "Rows"},
            {"flipbookFrameRate",   "Frame Rate"},
            {"flipbookRandomStart", "Random Start"},
            {"flipbookFrameBlend",  "Frame Blending"},
        };

        float inputWidth = 80.0f;

        for (const auto& entry : entries)
        {
            auto it = node.properties.find(entry.key);
            if (it == node.properties.end()) continue;

            auto& prop = it->second;
            ImGui::PushID(entry.key);

            if (prop.type == vfx::VFXPropertyType::Int)
            {
                auto* val = std::get_if<int32_t>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth);
                    int v = *val;
                    if (ImGui::DragInt("##v", &v,
                                       1.0f, static_cast<int>(prop.min), static_cast<int>(prop.max)))
                    {
                        *val = v;
                        notifyChanged();
                    }
                }
            }
            else if (prop.type == vfx::VFXPropertyType::Float)
            {
                auto* val = std::get_if<float>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth);
                    if (ImGui::DragFloat("##v", val, 0.1f, prop.min, prop.max, "%.2f"))
                    {
                        notifyChanged();
                    }
                }
            }
            else if (prop.type == vfx::VFXPropertyType::Bool)
            {
                auto* val = std::get_if<bool>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine();
                    if (ImGui::Checkbox("##v", val))
                    {
                        notifyChanged();
                    }
                }
            }

            ImGui::PopID();
        }
    }

    void VFXPropertyPanel::drawMeshPathSelector(vfx::VFXNode& node, float inputWidth)
    {
        auto meshIt = node.properties.find("meshPath");
        if (meshIt == node.properties.end()) return;

        auto* meshVal = std::get_if<std::string>(&meshIt->second.value);
        if (!meshVal) return;

        ImGui::Text("Mesh");
        ImGui::SameLine(100.0f);
        std::string display = meshVal->empty() ? "(none)" : std::filesystem::path(*meshVal).filename().string();
        char buf[256];
        std::strncpy(buf, display.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::SetNextItemWidth(inputWidth * 1.5f);
        ImGui::InputText("##panel_meshPath", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button("...##meshBrowse"))
        {
            nfd::FileDialog dialog;
            std::string path = dialog.openFileDialog({
                {L"VF Mesh", L"*.vfMesh"}
            });
            if (!path.empty())
            {
                *meshVal = path;
                notifyChanged();
            }
        }
        if (!meshVal->empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("X##meshClear"))
            {
                meshVal->clear();
                notifyChanged();
            }
        }
    }

    void VFXPropertyPanel::drawRibbonProperties(vfx::VFXNode& node, float inputWidth)
    {
        struct RibbonEntry { const char* key; const char* label; float step; const char* fmt; };
        static constexpr RibbonEntry ribbonEntries[] = {
            {"ribbonWidth",       "Width",        0.01f, "%.2f"},
            {"ribbonMinDistance",  "Min Distance", 0.01f, "%.2f"},
        };

        auto tpIt = node.properties.find("maxTrailPoints");
        if (tpIt != node.properties.end())
        {
            auto* val = std::get_if<int32_t>(&tpIt->second.value);
            if (val)
            {
                ImGui::Text("Trail Points");
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                if (ImGui::SliderInt("##panel_maxTrailPoints", val, 2, 256))
                {
                    notifyChanged();
                }
            }
        }

        for (const auto& re : ribbonEntries)
        {
            auto it = node.properties.find(re.key);
            if (it == node.properties.end()) continue;
            auto& prop = it->second;
            auto* val = std::get_if<float>(&prop.value);
            if (val)
            {
                ImGui::PushID(re.key);
                ImGui::Text("%s", re.label);
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                if (ImGui::DragFloat("##v", val, re.step, prop.min, prop.max, re.fmt))
                {
                    notifyChanged();
                }
                ImGui::PopID();
            }
        }

        // VK-1474: over-trail width curve + tail gradient. Reuse the shared curve/gradient
        // editors; their re-sync keys are now independent (VFXPropertyPanel.hpp) so both can
        // be shown together without thrashing. Width curve multiplies the flat Width above;
        // tail gradient tints the ribbon color. Both are sampled head(0) -> tail(1).
        if (auto wcIt = node.properties.find("ribbonWidthCurve"); wcIt != node.properties.end())
        {
            if (auto* curve = std::get_if<vfx::VFXCurve>(&wcIt->second.value))
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Width Over Trail (head -> tail)");
                drawCurveEditor(*curve, wcIt->second);
            }
        }
        if (auto tgIt = node.properties.find("ribbonTailGradient"); tgIt != node.properties.end())
        {
            if (auto* gradient = std::get_if<vfx::VFXGradient>(&tgIt->second.value))
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Tint Over Trail (head -> tail)");
                drawGradientEditor(*gradient, "ribbonTailGradient");
            }
        }
    }

    void VFXPropertyPanel::drawUVScrollProperties(vfx::VFXNode& node, float inputWidth)
    {
        struct UVScrollEntry { const char* key; const char* label; };
        static constexpr UVScrollEntry uvScrollEntries[] = {
            {"uvScrollSpeedU", "UV Scroll U"},
            {"uvScrollSpeedV", "UV Scroll V"},
        };

        for (const auto& entry : uvScrollEntries)
        {
            auto it = node.properties.find(entry.key);
            if (it == node.properties.end()) continue;
            auto& prop = it->second;
            auto* val = std::get_if<float>(&prop.value);
            if (val)
            {
                ImGui::PushID(entry.key);
                ImGui::Text("%s", entry.label);
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                if (ImGui::DragFloat("##v", val, 0.01f, prop.min, prop.max, "%.2f"))
                {
                    notifyChanged();
                }
                ImGui::PopID();
            }
        }
    }

    void VFXPropertyPanel::drawRenderingProperties(vfx::VFXNode& node)
    {
        float inputWidth = 80.0f;

        int currentRenderMode = 0;
        {
            auto rmIt = node.properties.find("renderMode");
            if (rmIt != node.properties.end())
            {
                auto& prop = rmIt->second;
                if (auto* val = std::get_if<int32_t>(&prop.value))
                {
                    currentRenderMode = std::clamp(*val, 0, 4);
                    ImGui::Text("Render Mode");
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth * 1.5f);
                    const char* modes[] = {"Billboard", "Stretched", "Horizontal", "Mesh Particle", "Ribbon"};
                    int current = currentRenderMode;
                    if (ImGui::Combo("##panel_renderMode", &current, modes, 5))
                    {
                        *val = current;
                        currentRenderMode = current;
                        notifyChanged();
                    }
                }
            }
        }

        if (currentRenderMode == 3)
            drawMeshPathSelector(node, inputWidth);

        // VK-1476: mesh-particle orientation (only for the Mesh Particle render mode).
        if (currentRenderMode == 3)
        {
            // Ensure the props exist so effects authored before VK-1476 can be edited.
            if (node.properties.find("meshOrientationMode") == node.properties.end())
                node.properties["meshOrientationMode"] = vfx::VFXProperty{
                    "meshOrientationMode", vfx::VFXPropertyType::String,
                    std::string(vfx::orientationModeToString(vfx::VFXOrientationMode::VelocityForward)), 0.0f, 0.0f};
            if (node.properties.find("meshOrientationAxis") == node.properties.end())
                node.properties["meshOrientationAxis"] = vfx::VFXProperty{
                    "meshOrientationAxis", vfx::VFXPropertyType::Vec3, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f};
            if (node.properties.find("meshOrientationSpinRate") == node.properties.end())
                node.properties["meshOrientationSpinRate"] = vfx::VFXProperty{
                    "meshOrientationSpinRate", vfx::VFXPropertyType::Float, 1.0f, 0.0f, 50.0f};

            vfx::VFXOrientationMode currentOrient = vfx::VFXOrientationMode::VelocityForward;
            if (auto* s = std::get_if<std::string>(&node.properties["meshOrientationMode"].value))
                currentOrient = vfx::stringToOrientationMode(*s);

            ImGui::Text("Orientation");
            ImGui::SameLine(100.0f);
            ImGui::SetNextItemWidth(inputWidth * 1.5f);
            const char* orientModes[] = {"Velocity Forward", "Tumble", "Axis Lock", "Camera Facing"};
            int current = static_cast<int>(currentOrient);
            if (ImGui::Combo("##panel_meshOrientationMode", &current, orientModes, 4))
            {
                vfx::VFXOrientationMode chosen = static_cast<vfx::VFXOrientationMode>(std::clamp(current, 0, 3));
                node.properties["meshOrientationMode"] = vfx::VFXProperty{
                    "meshOrientationMode", vfx::VFXPropertyType::String,
                    std::string(vfx::orientationModeToString(chosen)), 0.0f, 0.0f};
                currentOrient = chosen;
                notifyChanged();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Tumble / Axis Lock spin at the Spin Rate below.\n"
                                  "Tumble uses a per-particle random axis; Axis Lock uses the fixed Axis.");

            // Spin Rate (Tumble + Axis Lock).
            if (currentOrient == vfx::VFXOrientationMode::Tumble || currentOrient == vfx::VFXOrientationMode::AxisLock)
            {
                if (auto* val = std::get_if<float>(&node.properties["meshOrientationSpinRate"].value))
                {
                    ImGui::Text("Spin Rate");
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth * 1.5f);
                    if (ImGui::DragFloat("##panel_meshOrientationSpinRate", val, 0.05f, 0.0f, 50.0f, "%.2f"))
                        notifyChanged();
                }
            }

            // Axis (Axis Lock only).
            if (currentOrient == vfx::VFXOrientationMode::AxisLock)
            {
                if (auto* v = std::get_if<glm::vec3>(&node.properties["meshOrientationAxis"].value))
                {
                    ImGui::Text("Axis");
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth * 2.5f);
                    if (ImGui::DragFloat3("##panel_meshOrientationAxis", &(*v)[0], 0.01f, -1.0f, 1.0f, "%.2f"))
                        notifyChanged();
                }
            }
        }

        // VK-1472: Blend Mode dropdown (replaces the legacy "Additive" checkbox).
        // Resolves from the blendMode string prop, else the legacy additiveBlend bool.
        {
            vfx::VFXBlendMode currentBlend = vfx::VFXBlendMode::Alpha;
            auto bmIt = node.properties.find("blendMode");
            if (bmIt != node.properties.end())
            {
                if (auto* s = std::get_if<std::string>(&bmIt->second.value))
                    currentBlend = vfx::stringToBlendMode(*s);
            }
            else
            {
                auto abIt = node.properties.find("additiveBlend");
                if (abIt != node.properties.end())
                    if (auto* b = std::get_if<bool>(&abIt->second.value))
                        currentBlend = vfx::blendModeFromLegacy(*b);
            }

            ImGui::Text("Blend Mode");
            ImGui::SameLine(100.0f);
            ImGui::SetNextItemWidth(inputWidth * 1.5f);
            const char* blendModes[] = {"Alpha", "Additive", "Premultiplied", "Multiply"};
            int current = static_cast<int>(currentBlend);
            if (ImGui::Combo("##panel_blendMode", &current, blendModes, 4))
            {
                vfx::VFXBlendMode chosen = static_cast<vfx::VFXBlendMode>(std::clamp(current, 0, 3));
                node.properties["blendMode"] = vfx::VFXProperty{
                    "blendMode", vfx::VFXPropertyType::String,
                    std::string(vfx::blendModeToString(chosen)), 0.0f, 0.0f
                };
                // Mirror the legacy bool so old runtimes/tools still read additive-vs-not.
                node.properties["additiveBlend"] = vfx::VFXProperty{
                    "additiveBlend", vfx::VFXPropertyType::Bool,
                    (chosen == vfx::VFXBlendMode::Additive), 0.0f, 1.0f
                };
                notifyChanged();
            }
        }

        struct RenderEntry { const char* key; const char* label; };
        static constexpr RenderEntry entries[] = {
            {"alphaClipThreshold",   "Alpha Clip"},
            {"sortOrder",            "Sort Order"},
            {"softParticleDistance",  "Soft Distance"},
            {"stretchMultiplier",    "Stretch"},
        };

        for (const auto& entry : entries)
        {
            auto it = node.properties.find(entry.key);
            if (it == node.properties.end()) continue;

            auto& prop = it->second;
            ImGui::PushID(entry.key);

            bool disableWidget = (strcmp(entry.key, "stretchMultiplier") == 0 && currentRenderMode != 1);
            if (disableWidget) ImGui::BeginDisabled();

            if (prop.type == vfx::VFXPropertyType::Float)
            {
                auto* val = std::get_if<float>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth);
                    if (ImGui::DragFloat("##v", val, 0.01f, prop.min, prop.max, "%.2f"))
                    {
                        notifyChanged();
                    }
                }
            }
            else if (prop.type == vfx::VFXPropertyType::Bool)
            {
                auto* val = std::get_if<bool>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine();
                    if (ImGui::Checkbox("##v", val))
                    {
                        notifyChanged();
                    }
                }
            }
            else if (prop.type == vfx::VFXPropertyType::Int)
            {
                auto* val = std::get_if<int32_t>(&prop.value);
                if (val)
                {
                    ImGui::Text("%s", entry.label);
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth);
                    if (ImGui::DragInt("##v", val, 1,
                                       static_cast<int>(prop.min), static_cast<int>(prop.max)))
                    {
                        notifyChanged();
                    }
                }
            }

            if (disableWidget) ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    void VFXPropertyPanel::drawLightingProperties(vfx::VFXNode& node)
    {
        float inputWidth = 80.0f;

        int currentRenderMode = 0;
        auto rmIt = node.properties.find("renderMode");
        if (rmIt != node.properties.end())
        {
            if (auto* val = std::get_if<int32_t>(&rmIt->second.value))
                currentRenderMode = std::clamp(*val, 0, 4);
        }

        struct LightEntry { const char* key; const char* label; };
        static constexpr LightEntry lightEntries[] = {
            {"emissiveIntensity", "Emissive"},
            {"lightingInfluence",  "Light Influence"},
            {"ambientAmount",      "Ambient"},
        };

        for (const auto& entry : lightEntries)
        {
            auto it = node.properties.find(entry.key);
            if (it == node.properties.end()) continue;

            auto& prop = it->second;
            ImGui::PushID(entry.key);

            if (auto* val = std::get_if<float>(&prop.value))
            {
                ImGui::Text("%s", entry.label);
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                if (ImGui::DragFloat("##v", val, 0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    notifyChanged();
                }
            }

            ImGui::PopID();
        }

        {
            auto nmIt = node.properties.find("normalMode");
            if (nmIt != node.properties.end())
            {
                if (auto* val = std::get_if<int32_t>(&nmIt->second.value))
                {
                    ImGui::Text("Normal Mode");
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth * 1.5f);
                    bool isMeshMode = (currentRenderMode == 3);
                    int maxModes = isMeshMode ? 3 : 2;
                    const char* normalModes[] = {"Sphere", "View-Aligned", "Mesh"};
                    int current = std::clamp(*val, 0, maxModes - 1);

                    if (current != *val)
                    {
                        *val = current;
                        notifyChanged();
                    }

                    if (ImGui::Combo("##panel_normalMode", &current, normalModes, maxModes))
                    {
                        *val = current;
                        notifyChanged();
                    }
                }
            }
        }
    }

    void VFXPropertyPanel::drawDistortionProperties(vfx::VFXNode& node)
    {
        float inputWidth = 80.0f;
        {
            auto enableIt = node.properties.find("distortionEnabled");
            if (enableIt != node.properties.end())
            {
                if (auto* val = std::get_if<bool>(&enableIt->second.value))
                {
                    ImGui::Text("Enable");
                    ImGui::SameLine();
                    if (ImGui::Checkbox("##distortionEnabled", val))
                    {
                        notifyChanged();
                    }
                }
            }
        }

        {
            auto strengthIt = node.properties.find("distortionStrength");
            if (strengthIt != node.properties.end())
            {
                if (auto* val = std::get_if<float>(&strengthIt->second.value))
                {
                    ImGui::Text("Strength");
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth);
                    if (ImGui::DragFloat("##distortionStrength", val, 0.01f, 0.0f, 2.0f, "%.2f"))
                    {
                        notifyChanged();
                    }
                }
            }
        }

        {
            auto texIt = node.properties.find("distortionTexture");
            if (texIt != node.properties.end())
            {
                if (auto* val = std::get_if<std::string>(&texIt->second.value))
                {
                    ImGui::Text("Texture");
                    ImGui::SameLine(100.0f);
                    std::string display = val->empty() ? "(none)"
                        : std::filesystem::path(*val).filename().string();
                    char buf[256];
                    std::strncpy(buf, display.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    ImGui::SetNextItemWidth(inputWidth * 1.5f);
                    ImGui::InputText("##distortionTexture", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
                    if (auto dropped = windows::acceptAssetDropOnLastItem("DistortionTexDrop", {".vfimage"}))
                    {
                        *val = *dropped;
                        notifyChanged();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("...##distortionTexBrowse"))
                    {
                        nfd::FileDialog dialog;
                        std::string path = dialog.openFileDialog({
                            {L"VF Image", L"*.vfImage"}
                        });
                        if (!path.empty())
                        {
                            *val = path;
                            notifyChanged();
                        }
                    }
                    if (!val->empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("X##distortionTexClear"))
                        {
                            val->clear();
                            notifyChanged();
                        }
                    }
                }
            }
        }
    }

    void VFXPropertyPanel::drawBurstProperties(vfx::VFXNode& node, float inputWidth)
    {
        std::vector<vfx::VFXBurst> bursts = vfx::loadBurstsFromNode(node);
        bool changed = false;
        int removeIndex = -1;

        for (int i = 0; i < static_cast<int>(bursts.size()); ++i)
        {
            auto& burst = bursts[static_cast<size_t>(i)];
            ImGui::PushID(i);

            ImGui::Text("Burst %d", i);
            ImGui::SameLine();
            if (ImGui::SmallButton("X##removeBurst"))
                removeIndex = i;

            ImGui::Text("Time");
            ImGui::SameLine(100.0f);
            ImGui::SetNextItemWidth(inputWidth);
            changed |= ImGui::DragFloat("##burstTime", &burst.time, 0.05f, 0.0f, 60.0f, "%.2f");

            ImGui::Text("Count");
            ImGui::SameLine(100.0f);
            ImGui::SetNextItemWidth(inputWidth);
            changed |= ImGui::DragInt("##burstCount", &burst.count, 1, 0, 10000);

            ImGui::Text("Cycles");
            ImGui::SameLine(100.0f);
            ImGui::SetNextItemWidth(inputWidth);
            changed |= ImGui::DragInt("##burstCycles", &burst.cycles, 1, 0, 100);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Number of repeats (0 = repeat forever)");

            if (burst.cycles != 1)
            {
                ImGui::Text("Interval");
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                changed |= ImGui::DragFloat("##burstInterval", &burst.interval, 0.05f, 0.01f, 60.0f, "%.2f");
            }

            ImGui::Text("Probability");
            ImGui::SameLine(100.0f);
            ImGui::SetNextItemWidth(inputWidth);
            changed |= ImGui::SliderFloat("##burstProbability", &burst.probability, 0.0f, 1.0f, "%.2f");

            ImGui::Spacing();
            ImGui::PopID();
        }

        if (removeIndex >= 0)
        {
            bursts.erase(bursts.begin() + removeIndex);
            changed = true;
        }

        if (bursts.size() < static_cast<size_t>(vfx::BurstDefaults::MAX_BURSTS))
        {
            if (ImGui::Button("+ Add Burst"))
            {
                bursts.push_back(vfx::VFXBurst{});
                changed = true;
            }
        }

        if (changed)
        {
            vfx::storeBurstsToNode(node, bursts);
            notifyChanged();
        }
    }

    void VFXPropertyPanel::drawEventsProperties(vfx::VFXNode& node, float inputWidth)
    {
        struct EventEntry
        {
            vfx::VFXEventType type;
            const char* label;
        };

        static constexpr EventEntry eventEntries[] = {
            {vfx::VFXEventType::OnSpawn,             "On Spawn"},
            {vfx::VFXEventType::OnDeath,             "On Death"},
            {vfx::VFXEventType::OnCollision,         "On Collision"},
            {vfx::VFXEventType::OnLifetimeThreshold, "On Threshold"},
        };

        vfx::VFXEventConfig config = vfx::loadEventConfigFromNode(node);
        bool changed = false;

        for (const auto& entry : eventEntries)
        {
            auto& eventConfig = config.types[vfx::eventTypeIndex(entry.type)];

            ImGui::PushID(vfx::eventTypeKeyPrefix(entry.type));
            ImGui::Text("%s", entry.label);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("##enable", &eventConfig.enabled);

            if (eventConfig.enabled)
            {
                ImGui::SameLine();
                std::string display = eventConfig.vfxPath.empty() ? "(none)"
                    : std::filesystem::path(eventConfig.vfxPath).filename().string();
                char buf[256];
                std::strncpy(buf, display.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                ImGui::SetNextItemWidth(inputWidth * 1.5f);
                ImGui::InputText("##vfxPath", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("...##browse"))
                {
                    nfd::FileDialog dialog;
                    std::string path = dialog.openFileDialog({
                        {L"VFX Asset (*.vfVFX)", L"*.vfVFX"}
                    });
                    if (!path.empty())
                    {
                        eventConfig.vfxPath = path;
                        changed = true;
                    }
                }
                if (!eventConfig.vfxPath.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("X##clear"))
                    {
                        eventConfig.vfxPath.clear();
                        changed = true;
                    }
                }

                ImGui::Text("Count");
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                int count = eventConfig.spawnCount;
                if (ImGui::SliderInt("##count", &count,
                                     vfx::EventDefaults::MIN_SPAWN_COUNT,
                                     vfx::EventDefaults::MAX_SPAWN_COUNT))
                {
                    eventConfig.spawnCount = count;
                    changed = true;
                }

                ImGui::Text("Probability");
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                changed |= ImGui::SliderFloat("##probability", &eventConfig.probability, 0.0f, 1.0f, "%.2f");

                ImGui::Text("Vel Inherit");
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                changed |= ImGui::DragFloat("##velInherit", &eventConfig.inheritVelocityScale,
                                            0.05f, 0.0f,
                                            vfx::EventDefaults::MAX_INHERIT_VELOCITY_SCALE,
                                            "%.2f");

                ImGui::Text("Inherit Color");
                ImGui::SameLine(100.0f);
                changed |= ImGui::Checkbox("##inheritColor", &eventConfig.inheritColor);

                ImGui::Text("Inherit Size");
                ImGui::SameLine(100.0f);
                changed |= ImGui::Checkbox("##inheritSize", &eventConfig.inheritSize);

                // VK-1501: GPU fast path (spark-on-impact). Only OnDeath/OnCollision qualify.
                if (entry.type == vfx::VFXEventType::OnDeath ||
                    entry.type == vfx::VFXEventType::OnCollision)
                {
                    ImGui::Text("GPU Fast Path");
                    ImGui::SameLine(100.0f);
                    changed |= ImGui::Checkbox("##gpuFastPath", &eventConfig.gpuFastPath);
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(
                            "Spawn the child GPU-side via the request ring (~1 frame later).\n"
                            "No CPU readback and no child instance slot; depth-1 only.\n"
                            "Falls back to the CPU path when Probability < 1, when the child\n"
                            "regions are exhausted, or the child isn't burst-compatible.\n"
                            "No gameplay event notification is published on the fast path.");
                    }
                }

                ImGui::Spacing();
            }
            ImGui::PopID();
        }

        if (config.types[vfx::eventTypeIndex(vfx::VFXEventType::OnLifetimeThreshold)].enabled)
        {
            ImGui::Text("Threshold");
            ImGui::SameLine(100.0f);
            ImGui::SetNextItemWidth(inputWidth);
            changed |= ImGui::SliderFloat("##threshold", &config.lifetimeThreshold, 0.0f, 1.0f, "%.2f");
        }

        if (changed)
        {
            vfx::storeEventConfigToNode(node, config);
            notifyChanged();
        }
    }

    void VFXPropertyPanel::drawCollisionProperties(vfx::VFXNode& node, float inputWidth)
    {
        auto enableIt = node.properties.find("collisionEnabled");
        if (enableIt == node.properties.end()) return;
        auto* enableVal = std::get_if<bool>(&enableIt->second.value);
        if (!enableVal) return;

        ImGui::Text("Enable");
        ImGui::SameLine();
        if (ImGui::Checkbox("##collisionEnable", enableVal))
            notifyChanged();

        if (*enableVal)
        {
            ImGui::TextDisabled("Collides with scene physics colliders (Box, Sphere, Capsule)");

            struct SliderEntry { const char* key; const char* label; };
            static constexpr SliderEntry sliders[] = {
                {"collisionBounce",       "Bounce"},
                {"collisionFriction",     "Friction"},
                {"collisionLifetimeLoss", "Life Loss"},
            };

            for (const auto& s : sliders)
            {
                auto it = node.properties.find(s.key);
                if (it == node.properties.end()) continue;
                auto* val = std::get_if<float>(&it->second.value);
                if (!val) continue;

                ImGui::Text("%s", s.label);
                ImGui::SameLine(100.0f);
                ImGui::SetNextItemWidth(inputWidth);
                ImGui::PushID(s.key);
                if (ImGui::SliderFloat("##slider", val, 0.0f, 1.0f, "%.2f"))
                    notifyChanged();
                ImGui::PopID();
            }
        }

        // VK-1502: depth-buffer collision (independent of the analytic collision above).
        auto depthEnableIt = node.properties.find("depthCollisionEnabled");
        if (depthEnableIt != node.properties.end())
        {
            if (auto* depthEnableVal = std::get_if<bool>(&depthEnableIt->second.value))
            {
                ImGui::Separator();
                ImGui::Text("Depth Collision");
                ImGui::SameLine();
                if (ImGui::Checkbox("##depthCollisionEnable", depthEnableVal))
                    notifyChanged();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Collides against the scene depth buffer (last frame) so particles\n"
                                      "bounce off on-screen geometry without analytic colliders.\n"
                                      "Reuses Bounce/Friction/Life Loss. On-screen only; not shown in preview.");

                if (*depthEnableVal)
                {
                    struct DepthSlider { const char* key; const char* label; float min; float max; };
                    static constexpr DepthSlider depthSliders[] = {
                        {"depthCollisionThickness",       "Thickness",  0.0f, 5.0f},
                        {"depthCollisionNormalInfluence", "Normal Inf", 0.0f, 1.0f},
                    };
                    for (const auto& s : depthSliders)
                    {
                        auto it = node.properties.find(s.key);
                        if (it == node.properties.end()) continue;
                        auto* val = std::get_if<float>(&it->second.value);
                        if (!val) continue;

                        ImGui::Text("%s", s.label);
                        ImGui::SameLine(100.0f);
                        ImGui::SetNextItemWidth(inputWidth);
                        ImGui::PushID(s.key);
                        if (ImGui::SliderFloat("##slider", val, s.min, s.max, "%.2f"))
                            notifyChanged();
                        ImGui::PopID();
                    }
                }
            }
        }
    }

    namespace
    {
        bool moduleMatchesFilter(const char* label, const char* filter)
        {
            if (!filter || filter[0] == '\0') return true;
            std::string hay(label);
            std::string needle(filter);
            auto lower = [](std::string& s) {
                std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            };
            lower(hay);
            lower(needle);
            return hay.find(needle) != std::string::npos;
        }
    }

    // Draws every enabled "module" section in canonical order. Each addable section
    // uses the CollapsingHeader(&visible) overload so its built-in "x" removes it.
    void VFXPropertyPanel::drawEmitterSections(vfx::VFXNode& node)
    {
        std::string toRemove;
        for (const auto& s : vfx::kEmitterSections)
        {
            if (!vfx::sectionEnabled(node, s.id))
                continue;

            bool visible = true;
            if (ImGui::CollapsingHeader(s.label, &visible))
                drawEmitterSection(node, s.id);
            if (!visible)
                toRemove = s.id;
        }

        if (!toRemove.empty())
        {
            auto& v = node.enabledSections;
            v.erase(std::remove(v.begin(), v.end(), toRemove), v.end());
            notifyChanged();
        }
    }

    void VFXPropertyPanel::drawEmitterSection(vfx::VFXNode& node, const std::string& sectionId)
    {
        if (sectionId == "spawnVariance") drawSpawnVarianceProperties(node);
        else if (sectionId == "flipbook") drawFlipbookProperties(node);
        else if (sectionId == "rendering") drawRenderingProperties(node);
        else if (sectionId == "ribbon") drawRibbonProperties(node, 80.0f);
        else if (sectionId == "uvScroll") drawUVScrollProperties(node, 80.0f);
        else if (sectionId == "bursts") drawBurstProperties(node, 80.0f);
        else if (sectionId == "events") drawEventsProperties(node, 80.0f);
        else if (sectionId == "lighting") drawLightingProperties(node);
        else if (sectionId == "collision") drawCollisionProperties(node, 80.0f);
        else if (sectionId == "distortion") drawDistortionProperties(node);
        else if (sectionId == "advanced") drawAdvancedProperties(node);
    }

    void VFXPropertyPanel::drawAddModulePopup(vfx::VFXNode& node)
    {
        ImGui::Spacing();
        if (ImGui::Button("+ Add Module", ImVec2(-1.0f, 0.0f)))
        {
            moduleSearch[0] = '\0';
            ImGui::OpenPopup("VFXAddModule");
        }

        if (ImGui::BeginPopup("VFXAddModule"))
        {
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##vfxModSearch", "Search...", moduleSearch, sizeof(moduleSearch));
            ImGui::Separator();

            const char* filter = moduleSearch[0] != '\0' ? moduleSearch : nullptr;
            bool anyShown = false;
            for (const auto& s : vfx::kEmitterSections)
            {
                if (vfx::sectionEnabled(node, s.id))   // already added -> hide from the list
                    continue;
                if (!moduleMatchesFilter(s.label, filter))
                    continue;

                anyShown = true;
                if (ImGui::Selectable(s.label))
                {
                    node.enabledSections.emplace_back(s.id);
                    notifyChanged();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!anyShown)
                ImGui::TextDisabled("No modules");

            ImGui::EndPopup();
        }
    }

    // Fallback section: any emitter property not owned by a dedicated section above
    // (e.g. inheritVelocityRatio, proxy-light emission). Moved verbatim out of draw().
    void VFXPropertyPanel::drawAdvancedProperties(vfx::VFXNode& node)
    {
        static const std::unordered_set<std::string> handledProperties = {
            "spawnRate", "lifetime", "startSize", "startVelocity",
            "startColor", "looping", "texture",
            "sizeVariance", "lifetimeVariance", "speedVariance",
            "rotationVariance", "angularVelocityVariance",
            "colorValueVariance", "alphaVariance",
            "shapeType", "flipbookColumns", "flipbookRows", "flipbookFrameRate",
            "flipbookRandomStart", "flipbookFrameBlend", "alphaClipThreshold", "additiveBlend", "blendMode", "meshPath",
            "meshOrientationMode", "meshOrientationAxis", "meshOrientationSpinRate",
            "sortOrder", "renderMode", "softParticleDistance", "stretchMultiplier",
            "maxTrailPoints", "ribbonWidth", "ribbonMinDistance",
            "uvScrollSpeedU", "uvScrollSpeedV",
            "emissiveIntensity", "lightingInfluence", "ambientAmount", "normalMode",
            "distortionEnabled", "distortionStrength", "distortionTexture"
        };

        float labelWidth = 160.0f;
        float inputWidth = 80.0f;

        for (auto& [propName, prop] : node.properties)
        {
            if (handledProperties.count(propName)) continue;
            if (propName.rfind("flipbook", 0) == 0) continue;
            if (propName.rfind("event", 0) == 0) continue;
            if (propName.rfind("collision", 0) == 0) continue;
            if (propName.rfind("burst", 0) == 0) continue;

            std::string widgetId = "##adv" + propName + std::to_string(node.id);

            switch (prop.type)
            {
            case vfx::VFXPropertyType::Float: {
                float* val = std::get_if<float>(&prop.value);
                if (val) {
                    ImGui::Text("%s", propName.c_str());
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(inputWidth);
                    if (ImGui::DragFloat(widgetId.c_str(), val, 0.01f, prop.min, prop.max, "%.2f"))
                        notifyChanged();
                    ImGui::PopItemWidth();
                }
                break;
            }
            case vfx::VFXPropertyType::Int: {
                int32_t* val = std::get_if<int32_t>(&prop.value);
                if (val) {
                    ImGui::Text("%s", propName.c_str());
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(inputWidth);
                    if (ImGui::DragInt(widgetId.c_str(), val, 1,
                            static_cast<int>(prop.min), static_cast<int>(prop.max)))
                        notifyChanged();
                    ImGui::PopItemWidth();
                }
                break;
            }
            case vfx::VFXPropertyType::Bool: {
                bool* val = std::get_if<bool>(&prop.value);
                if (val) {
                    ImGui::Text("%s", propName.c_str());
                    ImGui::SameLine(labelWidth);
                    if (ImGui::Checkbox(widgetId.c_str(), val))
                        notifyChanged();
                }
                break;
            }
            case vfx::VFXPropertyType::String: {
                std::string* val = std::get_if<std::string>(&prop.value);
                if (val) {
                    ImGui::Text("%s", propName.c_str());
                    ImGui::SameLine(labelWidth);
                    ImGui::TextDisabled("%s", val->empty() ? "(none)" : val->c_str());
                }
                break;
            }
            default: break;
            }
        }
    }
}
