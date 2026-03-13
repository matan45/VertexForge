#include "BehaviorTreeEditorWindow.hpp"
#include <imgui.h>
#include <filesystem>
#include <array>

using namespace behaviortree;

namespace editor::windows
{
    BehaviorTreeEditorWindow::BehaviorTreeEditorWindow(const std::string& path)
        : treePath(path)
    {
        std::filesystem::path p(path);
        windowTitle = p.stem().string() + " - Behavior Tree###BT_" + path;
    }

    BehaviorTreeEditorWindow::~BehaviorTreeEditorWindow()
    {
        graphEditor.cleanUp();
    }

    void BehaviorTreeEditorWindow::initEditor()
    {
        graphEditor.init();

        graphEditor.setOnGraphChanged([this]() { onGraphChanged(); });
        propertyPanel.setOnPropertyChanged([this]() { onGraphChanged(); });

        loadTree();
    }

    void BehaviorTreeEditorWindow::loadTree()
    {
        if (treePath.empty() || !std::filesystem::exists(treePath))
        {
            treeData = std::make_unique<BehaviorTreeData>(BehaviorTreeAsset::createDefault());
        }
        else
        {
            auto loaded = BehaviorTreeAsset::load(treePath);
            if (loaded.has_value())
            {
                treeData = std::make_unique<BehaviorTreeData>(std::move(loaded.value()));
            }
            else
            {
                treeData = std::make_unique<BehaviorTreeData>(BehaviorTreeAsset::createDefault());
            }
        }

        graphEditor.setGraph(&treeData->graph);
        graphEditor.navigateToContent();
        isDirty = false;
    }

    void BehaviorTreeEditorWindow::saveTree()
    {
        if (treePath.empty()) return;

        if (BehaviorTreeAsset::save(treePath, *treeData))
        {
            isDirty = false;
        }
    }

    void BehaviorTreeEditorWindow::onGraphChanged()
    {
        isDirty = true;
    }

    void BehaviorTreeEditorWindow::draw()
    {
        if (needsInit)
        {
            initEditor();
            needsInit = false;
        }

        ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);

        std::string title = windowTitle;
        if (isDirty) title = "* " + title;

        if (!ImGui::Begin(title.c_str(), &isOpen, ImGuiWindowFlags_MenuBar))
        {
            ImGui::End();
            return;
        }

        drawToolbar();

        // Main layout: graph (left), panels (right)
        float rightPanelWidth = 300.0f;
        ImVec2 contentRegion = ImGui::GetContentRegionAvail();

        // Graph panel (left side)
        ImGui::BeginChild("BTGraphPanel", ImVec2(contentRegion.x - rightPanelWidth - 4.0f, 0), ImGuiChildFlags_None);
        drawGraphPanel();
        ImGui::EndChild();

        ImGui::SameLine();

        // Right side panels
        ImGui::BeginChild("BTRightPanel", ImVec2(rightPanelWidth, 0), ImGuiChildFlags_None);

        // Property panel (top right)
        float halfHeight = ImGui::GetContentRegionAvail().y * 0.5f;
        ImGui::BeginChild("BTPropertyPanel", ImVec2(0, halfHeight), ImGuiChildFlags_Borders);
        ImGui::Text("Properties");
        ImGui::Separator();
        drawPropertyPanel();
        ImGui::EndChild();

        // Blackboard panel (bottom right)
        ImGui::BeginChild("BTBlackboardPanel", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::Text("Blackboard");
        ImGui::Separator();
        drawBlackboardPanel();
        ImGui::EndChild();

        ImGui::EndChild();

        ImGui::End();
    }

    void BehaviorTreeEditorWindow::drawToolbar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    saveTree();
                }
                if (ImGui::MenuItem("Reload"))
                {
                    loadTree();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Navigate to Content"))
                {
                    graphEditor.navigateToContent();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // Keyboard shortcut
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
        {
            saveTree();
        }
    }

    void BehaviorTreeEditorWindow::drawGraphPanel()
    {
        graphEditor.draw();
    }

    void BehaviorTreeEditorWindow::drawPropertyPanel()
    {
        if (!treeData) return;

        uint32_t selectedId = graphEditor.getSelectedNodeId();
        BTNode* selectedNode = selectedId ? treeData->graph.findNodeById(selectedId) : nullptr;

        propertyPanel.draw(selectedNode, &treeData->graph);
    }

    void BehaviorTreeEditorWindow::drawBlackboardPanel()
    {
        if (!treeData) return;

        auto& keys = treeData->graph.blackboardKeys;

        // Add key button
        if (ImGui::Button("+ Add Key"))
        {
            BlackboardKeyDef newKey;
            newKey.name = "newKey";
            newKey.type = BlackboardValueType::Float;
            newKey.defaultValue = 0.0f;
            keys.push_back(newKey);
            onGraphChanged();
        }

        ImGui::Separator();

        int removeIdx = -1;

        for (int i = 0; i < static_cast<int>(keys.size()); ++i)
        {
            ImGui::PushID(i);

            auto& key = keys[i];

            // Name
            char nameBuf[64];
            strncpy(nameBuf, key.name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';

            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
            {
                key.name = nameBuf;
                onGraphChanged();
            }

            ImGui::SameLine();

            // Type dropdown
            static const std::array<const char*, 6> typeNames = {"Float", "Int", "Bool", "String", "Vec3", "Entity"};
            int currentType = static_cast<int>(key.type);
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::Combo("##type", &currentType, typeNames.data(), static_cast<int>(typeNames.size())))
            {
                key.type = static_cast<BlackboardValueType>(currentType);
                // Reset default value to match new type
                switch (key.type)
                {
                case BlackboardValueType::Float: key.defaultValue = 0.0f; break;
                case BlackboardValueType::Int: key.defaultValue = 0; break;
                case BlackboardValueType::Bool: key.defaultValue = false; break;
                case BlackboardValueType::String: key.defaultValue = std::string{}; break;
                case BlackboardValueType::Vec3: key.defaultValue = glm::vec3{0.0f}; break;
                case BlackboardValueType::Entity: key.defaultValue = services::EntityHandle::invalid(); break;
                }
                onGraphChanged();
            }

            ImGui::SameLine();

            // Remove button
            if (ImGui::Button("X##remove"))
            {
                removeIdx = i;
            }

            ImGui::PopID();
        }

        if (removeIdx >= 0)
        {
            keys.erase(keys.begin() + removeIdx);
            onGraphChanged();
        }
    }
}
