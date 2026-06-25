#include "BehaviorTreeEditorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include <imgui.h>
#include <filesystem>
#include <array>
#include <algorithm>

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
        if (debugActive)
        {
            stopDebugging();
        }
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
            events::resource::AssetSavedNotification assetNotif;
            assetNotif.filePath = treePath;
            events::EventDispatcher::instance().publish(assetNotif);

            // Hot reload: rebind any live runtimes using this asset (no-op outside play)
            events::ai::ReloadBehaviorTreeAssetCommand reloadCmd;
            reloadCmd.treePath = treePath;
            events::EventDispatcher::instance().execute(reloadCmd);
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

        if (initialSize.x <= 0.0f)
        {
            initialSize = ::editor::preview::initialWindowSize("BehaviorTreeEditor", ImVec2(1200, 700));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        std::string title = windowTitle;
        if (isDirty) title = "* " + title;

        if (!ImGui::Begin(title.c_str(), &isOpen, ImGuiWindowFlags_MenuBar | maximizer.windowFlags()))
        {
            ImGui::End();
            return;
        }

        drawToolbar();
        updateDebugState();

        static float rightPanelWidth = 300.0f;
        const float splitterThickness = 5.0f;
        ImVec2 contentRegion = ImGui::GetContentRegionAvail();
        rightPanelWidth = std::clamp(rightPanelWidth, 220.0f,
                                     std::max(220.0f, contentRegion.x - 200.0f - splitterThickness));
        float graphWidth = contentRegion.x - rightPanelWidth - splitterThickness;

        ImGui::BeginChild("BTGraphPanel", ImVec2(graphWidth, 0), ImGuiChildFlags_None);
        drawGraphPanel();
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 0.0f);
        ::editor::preview::splitterV("##btSplit", splitterThickness, &graphWidth,
                                     &rightPanelWidth, 200.0f, 220.0f, contentRegion.y);
        ImGui::SameLine(0.0f, 0.0f);

        ImGui::BeginChild("BTRightPanel", ImVec2(rightPanelWidth, 0), ImGuiChildFlags_None);

        float halfHeight = ImGui::GetContentRegionAvail().y * 0.5f;
        ImGui::BeginChild("BTPropertyPanel", ImVec2(0, halfHeight), ImGuiChildFlags_Borders);
        ImGui::Text("Properties");
        ImGui::Separator();
        drawPropertyPanel();
        ImGui::EndChild();

        ImGui::BeginChild("BTBlackboardPanel", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::Text("Blackboard");
        ImGui::Separator();
        drawBlackboardPanel();
        ImGui::EndChild();

        ImGui::EndChild();

        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            ::editor::preview::rememberWindowSize("BehaviorTreeEditor", maximizer.effectiveSize());
            sizeSaved = true;
        }
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

            drawDebugMenu();

            maximizer.drawButton();

            ImGui::EndMenuBar();
        }

        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
        {
            saveTree();
        }
    }

    namespace
    {
        std::string normalizePath(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
        }
    }

    void BehaviorTreeEditorWindow::drawDebugMenu()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool isPlayMode = dispatcher.query(events::editor::IsPlayModeQuery{});

        if (ImGui::BeginMenu("Debug"))
        {
            if (!isPlayMode)
            {
                ImGui::TextDisabled("Enter play mode to debug");
            }
            else
            {
                if (ImGui::MenuItem("Off", nullptr, !debugActive))
                {
                    stopDebugging();
                }

                auto targets = dispatcher.query(events::ai::GetAttachedBehaviorTreesQuery{});
                std::string thisPath = normalizePath(treePath);
                bool anyMatch = false;

                for (const auto& target : targets)
                {
                    if (normalizePath(target.treePath) != thisPath) continue;
                    anyMatch = true;

                    std::string label = (target.name.empty() ? "Entity" : target.name) +
                                        " (" + std::to_string(target.entity.id) + ")";
                    bool selected = debugActive && debugTarget.id == target.entity.id;
                    if (ImGui::MenuItem(label.c_str(), nullptr, selected))
                    {
                        startDebugging(target.entity);
                    }
                }

                if (!anyMatch)
                {
                    ImGui::TextDisabled("No entities running this tree");
                }
            }
            ImGui::EndMenu();
        }

        if (debugActive)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "DEBUGGING");
        }
    }

    void BehaviorTreeEditorWindow::updateDebugState()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (debugActive && !dispatcher.query(events::editor::IsPlayModeQuery{}))
        {
            stopDebugging();
        }

        if (debugActive)
        {
            events::ai::GetTreeRuntimeSnapshotQuery snapshotQuery;
            snapshotQuery.entity = debugTarget;
            debugSnapshot = dispatcher.query(snapshotQuery);
            graphEditor.setLiveStatus(debugSnapshot.valid ? &debugSnapshot.nodeStatuses : nullptr);
        }
        else
        {
            graphEditor.setLiveStatus(nullptr);
        }
    }

    void BehaviorTreeEditorWindow::startDebugging(services::EntityHandle entity)
    {
        debugActive = true;
        debugTarget = entity;
        debugSnapshot = {};

        events::ai::SetTreeDebugTargetCommand cmd;
        cmd.entity = entity;
        events::EventDispatcher::instance().execute(cmd);
    }

    void BehaviorTreeEditorWindow::stopDebugging()
    {
        debugActive = false;
        debugTarget = services::EntityHandle::invalid();
        debugSnapshot = {};
        graphEditor.setLiveStatus(nullptr);

        events::ai::SetTreeDebugTargetCommand cmd;
        cmd.entity = services::EntityHandle::invalid();
        events::EventDispatcher::instance().execute(cmd);
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

    void BehaviorTreeEditorWindow::drawLiveBlackboardPanel()
    {
        ImGui::TextDisabled("Live values - tick %llu",
                            static_cast<unsigned long long>(debugSnapshot.tickIndex));
        ImGui::Separator();

        auto& dispatcher = events::EventDispatcher::instance();

        for (auto& [key, value] : debugSnapshot.blackboard)
        {
            ImGui::PushID(key.c_str());

            bool changed = false;
            behaviortree::BlackboardValue newValue = value;

            if (std::holds_alternative<float>(value))
            {
                float v = std::get<float>(value);
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::DragFloat(key.c_str(), &v, 0.1f)) { newValue = v; changed = true; }
            }
            else if (std::holds_alternative<int32_t>(value))
            {
                int v = std::get<int32_t>(value);
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::DragInt(key.c_str(), &v)) { newValue = static_cast<int32_t>(v); changed = true; }
            }
            else if (std::holds_alternative<bool>(value))
            {
                bool v = std::get<bool>(value);
                if (ImGui::Checkbox(key.c_str(), &v)) { newValue = v; changed = true; }
            }
            else if (std::holds_alternative<std::string>(value))
            {
                char buf[128];
                strncpy(buf, std::get<std::string>(value).c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText(key.c_str(), buf, sizeof(buf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    newValue = std::string(buf);
                    changed = true;
                }
            }
            else if (std::holds_alternative<glm::vec3>(value))
            {
                glm::vec3 v = std::get<glm::vec3>(value);
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::DragFloat3(key.c_str(), &v.x, 0.1f)) { newValue = v; changed = true; }
            }
            else if (std::holds_alternative<services::EntityHandle>(value))
            {
                auto handle = std::get<services::EntityHandle>(value);
                ImGui::Text("%s: entity %llu", key.c_str(),
                            static_cast<unsigned long long>(handle.id));
            }

            if (changed)
            {
                events::ai::SetBlackboardValueCommand cmd;
                cmd.entity = debugTarget;
                cmd.key = key;
                cmd.value = newValue;
                dispatcher.execute(cmd);
            }

            ImGui::PopID();
        }

        if (debugSnapshot.blackboard.empty())
        {
            ImGui::TextDisabled("(blackboard is empty)");
        }
    }

    void BehaviorTreeEditorWindow::drawBlackboardPanel()
    {
        if (!treeData) return;

        if (debugActive && debugSnapshot.valid)
        {
            drawLiveBlackboardPanel();
            return;
        }

        auto& keys = treeData->graph.blackboardKeys;

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

            static const std::array<const char*, 6> typeNames = {"Float", "Int", "Bool", "String", "Vec3", "Entity"};
            int currentType = static_cast<int>(key.type);
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::Combo("##type", &currentType, typeNames.data(), static_cast<int>(typeNames.size())))
            {
                key.type = static_cast<BlackboardValueType>(currentType);
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
