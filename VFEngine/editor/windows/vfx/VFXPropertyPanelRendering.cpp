#include "VFXPropertyPanel.hpp"
#include "imgui.h"
#include "../../dragdrop/AssetDropTarget.hpp"
#include <nfd/FileDialog.hpp>
#include <vfx/VFXBurstTypes.hpp>
#include <vfx/VFXShapeProperties.hpp>
#include <vfx/VFXShapeTypes.hpp>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>

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

        struct RenderEntry { const char* key; const char* label; };
        static constexpr RenderEntry entries[] = {
            {"alphaClipThreshold",   "Alpha Clip"},
            {"additiveBlend",        "Additive"},
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
            const char* enableKey;
            const char* vfxKey;
            const char* label;
        };

        static constexpr EventEntry eventEntries[] = {
            {"eventOnSpawnEnabled",             "eventOnSpawnVFX",             "On Spawn"},
            {"eventOnDeathEnabled",             "eventOnDeathVFX",             "On Death"},
            {"eventOnCollisionEnabled",         "eventOnCollisionVFX",         "On Collision"},
            {"eventOnLifetimeThresholdEnabled", "eventOnLifetimeThresholdVFX", "On Threshold"},
        };

        for (const auto& entry : eventEntries)
        {
            auto enableIt = node.properties.find(entry.enableKey);
            if (enableIt == node.properties.end()) continue;

            auto* enableVal = std::get_if<bool>(&enableIt->second.value);
            if (!enableVal) continue;

            ImGui::PushID(entry.enableKey);
            ImGui::Text("%s", entry.label);
            ImGui::SameLine();
            if (ImGui::Checkbox("##enable", enableVal))
            {
                notifyChanged();
            }

            if (*enableVal)
            {
                auto vfxIt = node.properties.find(entry.vfxKey);
                if (vfxIt != node.properties.end())
                {
                    auto* vfxVal = std::get_if<std::string>(&vfxIt->second.value);
                    if (vfxVal)
                    {
                        ImGui::SameLine();
                        std::string display = vfxVal->empty() ? "(none)"
                            : std::filesystem::path(*vfxVal).filename().string();
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
                                *vfxVal = path;
                                notifyChanged();
                            }
                        }
                        if (!vfxVal->empty())
                        {
                            ImGui::SameLine();
                            if (ImGui::Button("X##clear"))
                            {
                                vfxVal->clear();
                                notifyChanged();
                            }
                        }
                    }
                }
            }
            ImGui::PopID();
        }

        auto enableIt = node.properties.find("eventOnLifetimeThresholdEnabled");
        bool threshEnabled = false;
        if (enableIt != node.properties.end())
        {
            auto* ev = std::get_if<bool>(&enableIt->second.value);
            if (ev) threshEnabled = *ev;
        }

        if (threshEnabled)
        {
            auto threshIt = node.properties.find("eventLifetimeThreshold");
            if (threshIt != node.properties.end())
            {
                auto* val = std::get_if<float>(&threshIt->second.value);
                if (val)
                {
                    ImGui::Text("Threshold");
                    ImGui::SameLine(100.0f);
                    ImGui::SetNextItemWidth(inputWidth);
                    if (ImGui::SliderFloat("##threshold", val, 0.0f, 1.0f, "%.2f"))
                    {
                        notifyChanged();
                    }
                }
            }
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
    }
}
