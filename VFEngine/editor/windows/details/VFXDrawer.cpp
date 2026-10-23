#include "print/Log.hpp"
#include "VFXDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "vfx/VFXBlendMode.hpp"
#include "vfx/VFXParameterRegistry.hpp"
#include <imgui.h>
#include <fstream>
#include <string>
#include <variant>

namespace windows::details
{
    namespace
    {
        // VK-1657: one read-only override row - the registry label, then the value (blend mode by
        // name, colours as a swatch plus the numbers).
        void drawOverrideValue(const vfx::VFXParamOverride& overrideValue)
        {
            const vfx::VFXExposedParameter* parameter = vfx::findExposedParameter(overrideValue.name);
            const std::string label = (parameter && !parameter->label.empty())
                ? std::string(parameter->label)
                : overrideValue.name;
            ImGui::Text("%s:", label.c_str());
            ImGui::SameLine();

            const vfx::VFXPropertyValue& value = overrideValue.value;
            if (const auto* number = std::get_if<float>(&value))
            {
                ImGui::Text("%.3f", *number);
            }
            else if (const auto* integer = std::get_if<int32_t>(&value))
            {
                const bool isBlendMode = parameter && parameter->target == vfx::VFXOverrideTarget::BlendMode;
                if (isBlendMode && *integer >= 0 && *integer < vfx::kBlendModeCount)
                {
                    ImGui::TextUnformatted(vfx::blendModeToString(static_cast<vfx::VFXBlendMode>(*integer)));
                }
                else
                {
                    ImGui::Text("%d", static_cast<int>(*integer));
                }
            }
            else if (const auto* flag = std::get_if<bool>(&value))
            {
                ImGui::TextUnformatted(*flag ? "true" : "false");
            }
            else if (const auto* vector = std::get_if<glm::vec3>(&value))
            {
                ImGui::Text("%.3f, %.3f, %.3f", vector->x, vector->y, vector->z);
            }
            else if (const auto* color = std::get_if<glm::vec4>(&value))
            {
                const float swatchSize = ImGui::GetTextLineHeight();
                ImGui::ColorButton("##Swatch", ImVec4(color->x, color->y, color->z, color->w),
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(swatchSize, swatchSize));
                ImGui::SameLine();
                ImGui::Text("%.2f, %.2f, %.2f, %.2f", color->x, color->y, color->z, color->w);
            }
            else
            {
                ImGui::TextDisabled("(unsupported value)");
            }
        }
    }

    bool VFXDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasVFXComponentQuery hasVFXQuery;
        hasVFXQuery.entity = handle;
        bool hasVFX = dispatcher.query(hasVFXQuery);

        if (!hasVFX)
        {
            return false;
        }

        events::scene::GetVFXDataQuery vfxQuery;
        vfxQuery.entity = handle;
        auto vfxOpt = dispatcher.query(vfxQuery);

        if (!vfxOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("VFXComponent");

        bool removeVFX = false;
        bool isOpen = drawHeader(removeVFX);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::VFXData vfxData = *vfxOpt;
            bool changed = false;

            ImGui::TextDisabled("Visual effects particle system");
            ImGui::Spacing();

            changed |= drawVFXFilePath(vfxData);
            ImGui::Spacing();
            changed |= drawSettings(vfxData);
            ImGui::Spacing();
            changed |= drawOverrides(vfxData);

            if (changed)
            {
                events::scene::SetVFXDataCommand cmd;
                cmd.entity = handle;
                cmd.vfxData = vfxData;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeVFX)
        {
            events::scene::RemoveVFXComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool VFXDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##VFXHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("VFX");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveVFX", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool VFXDrawer::drawVFXFilePath(services::VFXData& vfxData)
    {
        bool changed = false;

        if (vfxData.vfxRef.isValid())
        {
            std::string filename = vfxData.vfxRef.resolve();
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::Text("File: %s", filename.c_str());
        }
        else
        {
            ImGui::TextDisabled("No VFX file selected");
        }

        if (ImGui::Button("Select VFX File##VFX"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF VFX Files (*.vfVFX)", L"*.vfVFX"}});
            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    vfxData.vfxRef = asset::AssetRef::fromPath(path);
                    changed = true;
                }
                else
                {
                    vfLogError("Selected VFX file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool wasEmpty = !vfxData.vfxRef.isValid();
        if (wasEmpty) ImGui::BeginDisabled();
        if (ImGui::Button("Clear##VFX"))
        {
            vfxData.vfxRef = asset::AssetRef::invalid();
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool VFXDrawer::drawSettings(services::VFXData& vfxData)
    {
        bool changed = false;

        if (ImGui::Checkbox("Auto Play##VFX", &vfxData.autoPlay))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Automatically play when entering play mode");
        }

        if (ImGui::Checkbox("Loop##VFX", &vfxData.loop))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Loop the VFX effect continuously");
        }

        return changed;
    }

    bool VFXDrawer::drawOverrides(services::VFXData& vfxData)
    {
        // VK-1657: per-entity parameter overrides, set by the MCP vfx_set_params tool. The inspector
        // only shows and resets them (not undoable, like the rest of this drawer).
        if (vfxData.overrides.empty())
        {
            ImGui::TextDisabled("No parameter overrides (set with vfx_set_params)");
            return false;
        }

        bool changed = false;

        ImGui::Text("Parameter Overrides:");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Per-entity values applied over the effect's authored parameters.\n"
                              "Saved with the scene; the .vfVFX file is never changed.");
        }
        ImGui::Indent(10.0f);

        int overrideToReset = -1;
        for (int i = 0; i < static_cast<int>(vfxData.overrides.size()); ++i)
        {
            ImGui::PushID(i);

            drawOverrideValue(vfxData.overrides[i]);

            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##Override"))
            {
                overrideToReset = i;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Remove this override; the effect's authored value applies again");
            }

            ImGui::PopID();
        }

        if (overrideToReset >= 0)
        {
            vfxData.overrides.erase(vfxData.overrides.begin() + overrideToReset);
            changed = true;
        }

        if (ImGui::Button("Reset All##VFXOverrides"))
        {
            vfxData.overrides.clear();
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Remove every parameter override");
        }

        ImGui::Unindent(10.0f);

        return changed;
    }
}
