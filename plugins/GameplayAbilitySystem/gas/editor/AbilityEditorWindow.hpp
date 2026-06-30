#pragma once

// Gameplay Ability System (VK-816) — .vfAbility authoring window (tabbed form).
// Registered via PluginContext::registerEditorWindow; opened by the GAS plugin
// when a .vfAbility double-click publishes "vf.asset.open".

#include "imguiHandler/ImguiWindow.hpp"
#include "../core/GASAssets.hpp"
#include "GASEditorWidgets.hpp"

#include <imgui.h>
#include <string>

namespace gas
{
    class AbilityEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        void open(const std::string& assetPath)
        {
            path = assetPath;
            auto loadedSpec = AbilityAsset::load(assetPath);
            spec = loadedSpec ? *loadedSpec : AbilityAsset::createDefault("New Ability");
            status = loadedSpec ? std::string() : std::string("(new — using default)");
            loaded = true;
        }

        void draw() override
        {
            if (!ImGui::Begin("GAS Ability Editor"))
            {
                ImGui::End();
                return;
            }
            if (!loaded)
            {
                ImGui::TextDisabled("Double-click a .vfAbility in the Content Browser to edit.");
                ImGui::End();
                return;
            }

            ImGui::TextDisabled("%s", path.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Save"))
                status = AbilityAsset::save(path, spec) ? "Saved" : "Save FAILED";
            if (!status.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", status.c_str());
            }
            ImGui::Separator();

            using namespace editorui;
            if (ImGui::BeginTabBar("gas_ability_tabs"))
            {
                if (ImGui::BeginTabItem("Activation"))
                {
                    inputText("Id", spec.id);
                    inputText("Display Name", spec.displayName);
                    inputText("Input Action", spec.activation.inputAction);
                    std::string pol = activationPolicyToString(spec.activation.policy);
                    if (combo("Policy", pol, {"OnPressed", "OnHeld", "OnGranted", "Manual"}))
                        spec.activation.policy = activationPolicyFromString(pol);
                    std::string tt = targetingTypeToString(spec.targeting.type);
                    if (combo("Targeting", tt, {"Self", "Single", "AOE"}))
                        spec.targeting.type = targetingTypeFromString(tt);
                    floatField("Range", spec.targeting.range);
                    floatField("Radius", spec.targeting.radius);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Requirements"))
                {
                    ImGui::SeparatorText("Ability Tags");
                    stringList("abilityTags", spec.abilityTags);
                    ImGui::SeparatorText("Activation Required");
                    stringList("reqTags", spec.activationRequiredTags);
                    ImGui::SeparatorText("Activation Blocked");
                    stringList("blkTags", spec.activationBlockedTags);
                    ImGui::SeparatorText("Owned While Active");
                    stringList("ownTags", spec.activationOwnedTags);
                    ImGui::SeparatorText("Target Required");
                    stringList("tgtTags", spec.targetRequiredTags);
                    ImGui::SeparatorText("Cancel Abilities With Tag");
                    stringList("cancelTags", spec.cancelAbilitiesWithTag);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Tasks"))
                {
                    ImGui::TextDisabled("Linear task sequence (run on activation).");
                    int removeIdx = -1;
                    for (int i = 0; i < static_cast<int>(spec.tasks.size()); ++i)
                    {
                        ImGui::PushID(i);
                        AbilityTask& t = spec.tasks[i];
                        std::string ty = taskTypeToString(t.type);
                        if (combo("Type", ty, {"WaitDelay", "PlayMontage", "ApplyEffect", "SpawnCue"}))
                            t.type = taskTypeFromString(ty);
                        inputText("Param", t.param);
                        floatField("Delay", t.delay);
                        if (ImGui::SmallButton("Remove Task")) removeIdx = i;
                        ImGui::Separator();
                        ImGui::PopID();
                    }
                    if (removeIdx >= 0) spec.tasks.erase(spec.tasks.begin() + removeIdx);
                    if (ImGui::SmallButton("Add Task")) spec.tasks.emplace_back();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Effects"))
                {
                    inputText("Cost Effect", spec.cost.effectId);
                    ImGui::SeparatorText("Cooldown");
                    inputText("Cooldown Effect", spec.cooldown.effectId);
                    floatField("Cooldown Duration", spec.cooldown.duration);
                    stringList("cdTags", spec.cooldown.cooldownTags, "Add Cooldown Tag");
                    ImGui::SeparatorText("Effects On Activate");
                    stringList("onActivate", spec.effectsOnActivate, "Add Effect");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Cues"))
                {
                    stringList("cueIds", spec.cueIds, "Add Cue");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Preview"))
                {
                    ImGui::Text("Id: %s", spec.id.c_str());
                    ImGui::Text("Tasks: %d", static_cast<int>(spec.tasks.size()));
                    ImGui::Text("Effects on activate: %d", static_cast<int>(spec.effectsOnActivate.size()));
                    ImGui::Text("Cooldown: %.2fs", spec.cooldown.duration);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }

    private:
        std::string path;
        std::string status;
        AbilitySpec spec;
        bool loaded = false;
    };
}
