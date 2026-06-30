#pragma once

// Gameplay Ability System (VK-816) — .vfGameplayEffect authoring window.

#include "imguiHandler/ImguiWindow.hpp"
#include "../core/GASAssets.hpp"
#include "GASEditorWidgets.hpp"

#include <imgui.h>
#include <string>

namespace gas
{
    class EffectEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        void open(const std::string& assetPath)
        {
            path = assetPath;
            auto loadedSpec = EffectAsset::load(assetPath);
            spec = loadedSpec ? *loadedSpec : EffectAsset::createDefault("New Effect");
            status = loadedSpec ? std::string() : std::string("(new — using default)");
            loaded = true;
        }

        void draw() override
        {
            if (!ImGui::Begin("GAS Effect Editor"))
            {
                ImGui::End();
                return;
            }
            if (!loaded)
            {
                ImGui::TextDisabled("Double-click a .vfGameplayEffect in the Content Browser to edit.");
                ImGui::End();
                return;
            }

            ImGui::TextDisabled("%s", path.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Save"))
                status = EffectAsset::save(path, spec) ? "Saved" : "Save FAILED";
            if (!status.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", status.c_str());
            }
            ImGui::Separator();

            using namespace editorui;
            if (ImGui::BeginTabBar("gas_effect_tabs"))
            {
                if (ImGui::BeginTabItem("Duration"))
                {
                    inputText("Id", spec.id);
                    inputText("Display Name", spec.displayName);
                    std::string dp = durationPolicyToString(spec.durationPolicy);
                    if (combo("Duration Policy", dp, {"Instant", "Duration", "Infinite"}))
                        spec.durationPolicy = durationPolicyFromString(dp);
                    floatField("Duration", spec.duration);
                    floatField("Period", spec.period);
                    ImGui::TextDisabled("Period > 0 fires the modifiers against base each period.");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Modifiers"))
                {
                    int removeIdx = -1;
                    for (int i = 0; i < static_cast<int>(spec.modifiers.size()); ++i)
                    {
                        ImGui::PushID(i);
                        AttributeModifier& m = spec.modifiers[i];
                        inputText("Attribute", m.attribute);
                        std::string op = modifierOpToString(m.op);
                        if (combo("Op", op, {"Add", "Multiply", "Override"}))
                            m.op = modifierOpFromString(op);
                        floatField("Magnitude", m.magnitude);
                        if (ImGui::SmallButton("Remove")) removeIdx = i;
                        ImGui::Separator();
                        ImGui::PopID();
                    }
                    if (removeIdx >= 0) spec.modifiers.erase(spec.modifiers.begin() + removeIdx);
                    if (ImGui::SmallButton("Add Modifier")) spec.modifiers.emplace_back();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Tags"))
                {
                    ImGui::SeparatorText("Granted Tags");
                    stringList("granted", spec.grantedTags);
                    ImGui::SeparatorText("Ongoing Required");
                    stringList("ongoing", spec.ongoingRequiredTags);
                    ImGui::SeparatorText("Removal Tags");
                    stringList("removal", spec.removalTags);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Stacking"))
                {
                    std::string sp = stackingPolicyToString(spec.stacking.policy);
                    if (combo("Policy", sp, {"None", "BySource", "ByTarget"}))
                        spec.stacking.policy = stackingPolicyFromString(sp);
                    ImGui::InputInt("Limit", &spec.stacking.limit);
                    ImGui::Checkbox("Duration Refresh", &spec.stacking.durationRefresh);
                    std::string ex = stackExpirationToString(spec.stacking.expiration);
                    if (combo("Expiration", ex, {"ClearStack", "RemoveSingle"}))
                        spec.stacking.expiration = stackExpirationFromString(ex);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Cues"))
                {
                    stringList("cueIds", spec.cueIds, "Add Cue");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }

    private:
        std::string path;
        std::string status;
        GameplayEffectSpec spec;
        bool loaded = false;
    };
}
