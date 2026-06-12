#include "ThemeEditorWindow.hpp"
#include "ui/UIThemeSerialization.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIThemeEvents.hpp"
#include "print/Log.hpp"
#include <imgui.h>
#include <cstring>

namespace windows
{
    namespace
    {
        // Well-known property names recognized by UIThemeApplier
        const char* colorProperties[] = {
            "labelColor", "imageTint",
            "buttonNormalColor", "buttonHoveredColor", "buttonPressedColor", "buttonDisabledColor",
            "progressTrackColor", "progressFillColor"
        };
        const char* floatProperties[] = { "fontSize" };
        const char* assetProperties[] = {
            "font", "imageTexture",
            "buttonNormalTexture", "buttonHoveredTexture", "buttonPressedTexture", "buttonDisabledTexture"
        };
    }

    void ThemeEditorWindow::show()
    {
        visible = true;
    }

    void ThemeEditorWindow::draw()
    {
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
        std::string title = "Theme Editor";
        if (!themePath.empty())
        {
            std::string filename = themePath;
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
                filename = filename.substr(lastSlash + 1);
            title += " - " + filename;
            if (dirty) title += " *";
        }
        title += "###ThemeEditor";

        if (ImGui::Begin(title.c_str(), &visible))
        {
            drawToolbar();
            ImGui::Separator();

            if (themePath.empty())
            {
                ImGui::TextDisabled("Open or create a .vfTheme to start editing");
            }
            else
            {
                if (ImGui::BeginChild("StyleList", ImVec2(180, 0), ImGuiChildFlags_Borders))
                {
                    drawStyleList();
                }
                ImGui::EndChild();
                ImGui::SameLine();
                if (ImGui::BeginChild("StyleProps", ImVec2(0, 0)))
                {
                    drawStyleProperties();
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void ThemeEditorWindow::drawToolbar()
    {
        if (ImGui::Button("Open##Theme"))
        {
            std::string path = fileDialog.openFileDialog(
                {{L"VF Theme Files (*.vfTheme)", L"*.vfTheme"}});
            if (!path.empty())
            {
                openTheme(path);
            }
        }
        ImGui::SameLine();
        bool noTheme = themePath.empty();
        if (noTheme) ImGui::BeginDisabled();
        if (ImGui::Button("Save##Theme"))
        {
            saveTheme();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to Scene##Theme"))
        {
            saveTheme();
            applyToScene();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Saves, then re-applies every themed canvas in the scene");
        if (noTheme) ImGui::EndDisabled();
    }

    void ThemeEditorWindow::drawStyleList()
    {
        for (const auto& [key, style] : theme.styles)
        {
            if (ImGui::Selectable(key.c_str(), selectedStyle == key))
            {
                selectedStyle = key;
            }
        }

        ImGui::Separator();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##NewStyleName", "New style...", newStyleName, sizeof(newStyleName));
        if (ImGui::Button("Add Style", ImVec2(-1, 0)) && newStyleName[0] != '\0')
        {
            std::string key = newStyleName;
            if (theme.styles.find(key) == theme.styles.end())
            {
                theme.styles[key] = {};
                selectedStyle = key;
                dirty = true;
            }
            newStyleName[0] = '\0';
        }

        if (!selectedStyle.empty())
        {
            if (ImGui::Button("Remove Style", ImVec2(-1, 0)))
            {
                theme.styles.erase(selectedStyle);
                selectedStyle.clear();
                dirty = true;
            }
        }
    }

    void ThemeEditorWindow::drawStyleProperties()
    {
        auto it = theme.styles.find(selectedStyle);
        if (it == theme.styles.end())
        {
            ImGui::TextDisabled("Select a style to edit its properties");
            return;
        }
        auto& style = it->second;

        ImGui::Text("Style: %s", selectedStyle.c_str());
        ImGui::Spacing();

        // Colors
        ImGui::SeparatorText("Colors");
        std::string removeColor;
        for (auto& [name, color] : style.colors)
        {
            ImGui::PushID(name.c_str());
            if (ImGui::ColorEdit4(name.c_str(), &color.x))
            {
                dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
            {
                removeColor = name;
            }
            ImGui::PopID();
        }
        if (!removeColor.empty())
        {
            style.colors.erase(removeColor);
            dirty = true;
        }

        // Floats
        ImGui::SeparatorText("Floats");
        std::string removeFloat;
        for (auto& [name, value] : style.floats)
        {
            ImGui::PushID(name.c_str());
            if (ImGui::DragFloat(name.c_str(), &value, 0.5f, 0.0f, 512.0f, "%.1f"))
            {
                dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
            {
                removeFloat = name;
            }
            ImGui::PopID();
        }
        if (!removeFloat.empty())
        {
            style.floats.erase(removeFloat);
            dirty = true;
        }

        // Assets
        ImGui::SeparatorText("Assets");
        std::string removeAsset;
        for (auto& [name, ref] : style.assets)
        {
            ImGui::PushID(name.c_str());
            std::string filename = ref.isValid() ? ref.resolve() : "";
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
                filename = filename.substr(lastSlash + 1);
            ImGui::Text("%s: %s", name.c_str(), filename.empty() ? "(missing)" : filename.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Select"))
            {
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Assets (*.vfImage;*.vfFont)", L"*.vfImage;*.vfFont"}});
                if (!path.empty())
                {
                    asset::AssetRef newRef = asset::AssetRef::fromPath(path);
                    if (newRef.isValid())
                    {
                        ref = newRef;
                        dirty = true;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
            {
                removeAsset = name;
            }
            ImGui::PopID();
        }
        if (!removeAsset.empty())
        {
            style.assets.erase(removeAsset);
            dirty = true;
        }

        // Add property
        ImGui::SeparatorText("Add Property");
        const char* kinds[] = {"Color", "Float", "Asset"};
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("##PropKind", &newPropertyKind, kinds, 3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::InputTextWithHint("##PropName", "property name", newPropertyName, sizeof(newPropertyName));
        ImGui::SameLine();
        if (ImGui::Button("Add##Prop") && newPropertyName[0] != '\0')
        {
            std::string name = newPropertyName;
            if (newPropertyKind == 0)      style.colors.emplace(name, glm::vec4{1.0f});
            else if (newPropertyKind == 1) style.floats.emplace(name, 0.0f);
            else                           style.assets.emplace(name, asset::AssetRef::invalid());
            newPropertyName[0] = '\0';
            dirty = true;
        }

        // Quick-add buttons for the well-known property names
        if (ImGui::TreeNode("Known Properties"))
        {
            ImGui::TextDisabled("Click to add (recognized by the theme applier)");
            for (const char* name : colorProperties)
            {
                if (style.colors.find(name) != style.colors.end()) continue;
                if (ImGui::SmallButton(name))
                {
                    style.colors.emplace(name, glm::vec4{1.0f});
                    dirty = true;
                }
            }
            for (const char* name : floatProperties)
            {
                if (style.floats.find(name) != style.floats.end()) continue;
                if (ImGui::SmallButton(name))
                {
                    style.floats.emplace(name, 16.0f);
                    dirty = true;
                }
            }
            for (const char* name : assetProperties)
            {
                if (style.assets.find(name) != style.assets.end()) continue;
                if (ImGui::SmallButton(name))
                {
                    style.assets.emplace(name, asset::AssetRef::invalid());
                    dirty = true;
                }
            }
            ImGui::TreePop();
        }
    }

    void ThemeEditorWindow::openTheme(const std::string& path)
    {
        auto loaded = utilities::ui::UIThemeSerialization::loadFromFile(path);
        if (!loaded.has_value())
        {
            vfLogError("Theme editor: failed to load {}", path);
            return;
        }
        theme = std::move(*loaded);
        themePath = path;
        selectedStyle.clear();
        dirty = false;
    }

    void ThemeEditorWindow::saveTheme()
    {
        if (themePath.empty()) return;
        if (utilities::ui::UIThemeSerialization::saveToFile(theme, themePath))
        {
            dirty = false;
        }
    }

    void ThemeEditorWindow::applyToScene()
    {
        events::ui::ReapplyUIThemeCommand cmd;
        events::EventDispatcher::instance().execute(cmd);
    }
}
