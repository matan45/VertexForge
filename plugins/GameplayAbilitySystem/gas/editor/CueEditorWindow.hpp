#pragma once

// Gameplay Ability System (VK-816) — .vfGameplayCue authoring window. A cue is
// cosmetic feedback (VFX + audio at a world position) the runtime dispatches
// when an effect/ability fires.

#include "imguiHandler/ImguiWindow.hpp"
#include "../core/GASAssets.hpp"
#include "GASEditorWidgets.hpp"

#include <imgui.h>
#include <string>

namespace gas
{
    class CueEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        void open(const std::string& assetPath)
        {
            path = assetPath;
            auto loadedSpec = CueAsset::load(assetPath);
            spec = loadedSpec ? *loadedSpec : CueAsset::createDefault("New Cue");
            status = loadedSpec ? std::string() : std::string("(new — using default)");
            loaded = true;
        }

        void draw() override
        {
            if (!ImGui::Begin("GAS Cue Editor"))
            {
                ImGui::End();
                return;
            }
            if (!loaded)
            {
                ImGui::TextDisabled("Double-click a .vfGameplayCue in the Content Browser to edit.");
                ImGui::End();
                return;
            }

            ImGui::TextDisabled("%s", path.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Save"))
                status = CueAsset::save(path, spec) ? "Saved" : "Save FAILED";
            if (!status.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", status.c_str());
            }
            ImGui::Separator();

            using namespace editorui;
            inputText("Id", spec.id);
            std::string tr = cueTriggerToString(spec.trigger);
            if (combo("Trigger", tr, {"OnApply", "OnRemove", "OnActive", "OnExecute"}))
                spec.trigger = cueTriggerFromString(tr);
            inputText("VFX Path (.vfVFX)", spec.vfxPath);
            inputText("Attach Socket", spec.attachSocket);
            inputText("Audio Path (.vfAudio)", spec.audioPath);

            ImGui::SeparatorText("Params (float)");
            std::string keyToRemove;
            for (auto& [key, value] : spec.params)
            {
                ImGui::PushID(key.c_str());
                float v = value;
                if (ImGui::InputFloat(key.c_str(), &v, 0.1f, 1.0f, "%.3f"))
                    value = v;
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) keyToRemove = key;
                ImGui::PopID();
            }
            if (!keyToRemove.empty())
                spec.params.erase(keyToRemove);

            ImGui::SetNextItemWidth(180.0f);
            inputText("##newparam", newParamKey);
            ImGui::SameLine();
            if (ImGui::Button("Add Param") && !newParamKey.empty())
            {
                spec.params[newParamKey] = 0.0f;
                newParamKey.clear();
            }
            ImGui::End();
        }

    private:
        std::string path;
        std::string status;
        std::string newParamKey;
        GameplayCueSpec spec;
        bool loaded = false;
    };
}
