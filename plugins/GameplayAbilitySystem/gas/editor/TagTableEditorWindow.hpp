#pragma once

// Gameplay Ability System (VK-816) — .vfGameplayTags table editor. Flat,
// dot-hierarchy-aware list with add / rename-cascade / delete and duplicate
// validation.

#include "imguiHandler/ImguiWindow.hpp"
#include "../core/GASAssets.hpp"
#include "../core/TagTableValidation.hpp"
#include "GASEditorWidgets.hpp"

#include <algorithm>
#include <imgui.h>
#include <string>

namespace gas
{
    class TagTableEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        void open(const std::string& assetPath)
        {
            path = assetPath;
            auto loadedTable = TagsAsset::load(assetPath);
            table = loadedTable ? *loadedTable : TagsAsset::createDefault("Project Tags");
            status = loadedTable ? std::string() : std::string("(new — using default)");
            loaded = true;
        }

        void draw() override
        {
            if (!ImGui::Begin("GAS Tag Table"))
            {
                ImGui::End();
                return;
            }
            if (!loaded)
            {
                ImGui::TextDisabled("Double-click a .vfGameplayTags in the Content Browser to edit.");
                ImGui::End();
                return;
            }

            ImGui::TextDisabled("%s", path.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Save"))
                status = TagsAsset::save(path, table) ? "Saved" : "Save FAILED";
            if (!status.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", status.c_str());
            }

            const auto dups = findDuplicateTags(table);
            if (!dups.empty())
            {
                ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.3f, 1.0f), "Duplicate tags: %d (first wins at load)",
                                   static_cast<int>(dups.size()));
            }
            ImGui::Separator();

            // Sort view alphabetically so the dotted hierarchy reads naturally.
            std::sort(table.definitions().begin(), table.definitions().end(),
                      [](const TagDefinition& a, const TagDefinition& b) { return a.tag < b.tag; });

            int removeIdx = -1;
            for (int i = 0; i < static_cast<int>(table.definitions().size()); ++i)
            {
                ImGui::PushID(i);
                TagDefinition& def = table.definitions()[i];

                // Rename with hierarchical cascade: edit a buffer, apply on enter.
                char tagBuf[256];
                std::snprintf(tagBuf, sizeof(tagBuf), "%s", def.tag.c_str());
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::InputText("##tag", tagBuf, sizeof(tagBuf), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    const std::string newTag = tagBuf;
                    if (!newTag.empty() && newTag != def.tag)
                        renameTagCascade(table, def.tag, newTag);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(260.0f);
                editorui::inputText("##comment", def.comment);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) removeIdx = i;
                ImGui::PopID();
            }
            if (removeIdx >= 0)
                table.definitions().erase(table.definitions().begin() + removeIdx);

            ImGui::Separator();
            ImGui::SetNextItemWidth(220.0f);
            editorui::inputText("New tag (dotted)", newTag);
            ImGui::SameLine();
            if (ImGui::Button("Declare") && !newTag.empty())
            {
                table.declare(newTag);
                newTag.clear();
            }
            ImGui::TextDisabled("%d tags. Editing a tag renames it and all its descendants.",
                                static_cast<int>(table.definitions().size()));
            ImGui::End();
        }

    private:
        std::string path;
        std::string status;
        std::string newTag;
        TagTable table;
        bool loaded = false;
    };
}
