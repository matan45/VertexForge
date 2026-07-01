#include "BehaviorTreeEditorWindow.hpp"
#include "BTValueWidgets.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "print/Log.hpp"
#include <imgui.h>
#include <filesystem>
#include <array>
#include <algorithm>
#include <cstring>
#include <string>

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
        revalidate();
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

            revalidate();
            if (validationReport.hasErrors())
            {
                vfLogError("Saved behavior tree '{}' with {} validation error(s) and {} warning(s)",
                           treePath, validationReport.errorCount(), validationReport.warningCount());
            }
            else if (validationReport.hasWarnings())
            {
                vfLogWarning("Saved behavior tree '{}' with {} validation warning(s)",
                             treePath, validationReport.warningCount());
            }
        }
    }

    void BehaviorTreeEditorWindow::onGraphChanged()
    {
        isDirty = true;
        // VK-1457 perf: defer the (heavy, allocation-y) full-graph revalidate. onGraphChanged fires every
        // frame a slider/text field is held; draw() flushes this once the widget is released.
        validationDirty = true;
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

        // VK-1457 perf: coalesce a continuous edit (slider drag / typing) into a single full-graph
        // revalidate when the active widget is released, rather than revalidating every frame.
        if (validationDirty && !ImGui::IsAnyItemActive())
        {
            revalidate();
            validationDirty = false;
        }

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

        drawValidationPanel();

        if (debugActive)
        {
            if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
            {
                drawDebugPanel();
            }
        }

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

        const char* severityLabel(validation::Severity severity)
        {
            switch (severity)
            {
            case validation::Severity::Error: return "Error";
            case validation::Severity::Warning: return "Warning";
            case validation::Severity::Info: return "Info";
            default: return "Info";
            }
        }

        ImVec4 severityColor(validation::Severity severity)
        {
            switch (severity)
            {
            case validation::Severity::Error: return ImVec4(0.95f, 0.25f, 0.25f, 1.0f);
            case validation::Severity::Warning: return ImVec4(1.0f, 0.70f, 0.20f, 1.0f);
            case validation::Severity::Info: return ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
            default: return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            }
        }

        std::string diagnosticLabel(const validation::Diagnostic& diagnostic)
        {
            std::string label = std::string("[") + severityLabel(diagnostic.severity) + "] ";
            if (diagnostic.nodeId != 0)
            {
                label += "Node " + std::to_string(diagnostic.nodeId) + ": ";
            }
            label += diagnostic.message;
            return label;
        }

        const char* btEventTypeLabel(behaviortree::BTEventType type)
        {
            switch (type)
            {
            case behaviortree::BTEventType::Enter: return "Enter";
            case behaviortree::BTEventType::Exit: return "Exit";
            case behaviortree::BTEventType::Abort: return "Abort";
            case behaviortree::BTEventType::ServiceFire: return "Service";
            default: return "?";
            }
        }

        const char* btStatusLabel(behaviortree::BTNodeStatus status)
        {
            switch (status)
            {
            case behaviortree::BTNodeStatus::Success: return "Success";
            case behaviortree::BTNodeStatus::Failure: return "Failure";
            case behaviortree::BTNodeStatus::Running: return "Running";
            default: return "?";
            }
        }

        // Resolve a node id to a readable "Name (id)" using the authored graph.
        std::string nodeLabel(const behaviortree::BTGraph& graph, uint32_t nodeId)
        {
            const behaviortree::BTNode* node = graph.findNodeById(nodeId);
            std::string name = node ? (node->name.empty() ? behaviortree::nodeTypeToString(node->type) : node->name)
                                    : std::string("<gone>");
            return name + " (" + std::to_string(nodeId) + ")";
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
            const bool valid = debugSnapshot.valid;

            if (valid)
            {
                // VK-1457: static SubTree nodes are spliced out of the runtime's expanded graph, so the
                // snapshot reports their bodies under re-mapped ids. Mirror each SubTree node's entry-node
                // status/active state back onto the authored id, and re-push breakpoints translated to
                // entry ids once the map arrives (it's empty for trees with no static SubTree nodes).
                subtreeEntryMap = debugSnapshot.subtreeEntryMap;
                buildAugmentedDebugViews();
                if (!subtreeEntryMap.empty() && !subtreeMapApplied)
                {
                    pushBreakpointsToRuntime();
                    subtreeMapApplied = true;
                }
            }

            graphEditor.setLiveStatus(valid ? &augmentedNodeStatuses : nullptr);
            graphEditor.setActivePath(valid ? &augmentedActivePath : nullptr);
            graphEditor.setBreakpoints(&breakpoints);
        }
        else
        {
            graphEditor.setLiveStatus(nullptr);
            graphEditor.setActivePath(nullptr);
            graphEditor.setBreakpoints(nullptr);
        }
    }

    void BehaviorTreeEditorWindow::buildAugmentedDebugViews()
    {
        // Start from the raw snapshot; add a mirrored entry for each authored SubTree node so it lights up
        // and joins the active spine when its inlined body is running. No-op when there are no SubTrees.
        augmentedNodeStatuses = debugSnapshot.nodeStatuses;
        augmentedActivePath = debugSnapshot.activePath;

        for (const auto& [subTreeId, entryId] : subtreeEntryMap)
        {
            auto statusIt = debugSnapshot.nodeStatuses.find(entryId);
            if (statusIt != debugSnapshot.nodeStatuses.end())
                augmentedNodeStatuses[subTreeId] = statusIt->second;

            if (std::find(debugSnapshot.activePath.begin(), debugSnapshot.activePath.end(), entryId)
                != debugSnapshot.activePath.end())
                augmentedActivePath.push_back(subTreeId);
        }
    }

    void BehaviorTreeEditorWindow::pushBreakpointsToRuntime()
    {
        events::ai::SetTreeBreakpointsCommand cmd;
        cmd.entity = debugTarget;
        // VK-1457: a breakpoint on a static SubTree node must be sent under the expanded entry id it was
        // spliced into (the authored id doesn't exist in the runtime's expanded graph). Non-SubTree ids
        // pass through unchanged. The map arrives with the first snapshot, so updateDebugState re-pushes.
        cmd.nodeIds.reserve(breakpoints.size());
        for (uint32_t id : breakpoints)
        {
            auto it = subtreeEntryMap.find(id);
            cmd.nodeIds.push_back(it != subtreeEntryMap.end() ? it->second : id);
        }
        events::EventDispatcher::instance().execute(cmd);
    }

    void BehaviorTreeEditorWindow::startDebugging(services::EntityHandle entity)
    {
        debugActive = true;
        debugTarget = entity;
        debugSnapshot = {};
        // Re-push breakpoints (translated) once the SubTree entry map arrives with the first snapshot.
        subtreeMapApplied = false;

        events::ai::SetTreeDebugTargetCommand cmd;
        cmd.entity = entity;
        events::EventDispatcher::instance().execute(cmd);

        // The runtime cleared its breakpoint set when the target changed; re-apply the editor's set.
        pushBreakpointsToRuntime();
    }

    void BehaviorTreeEditorWindow::stopDebugging()
    {
        debugActive = false;
        debugTarget = services::EntityHandle::invalid();
        debugSnapshot = {};
        subtreeEntryMap.clear();
        subtreeMapApplied = false;
        graphEditor.setLiveStatus(nullptr);
        graphEditor.setActivePath(nullptr);
        graphEditor.setBreakpoints(nullptr);

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

    void BehaviorTreeEditorWindow::revalidate()
    {
        validationReport = {};
        validationSeverities.clear();

        if (!treeData)
        {
            graphEditor.setValidationSeverities(nullptr);
            return;
        }

        validation::ValidationContext context;
        context.subtreeExists = [](std::string_view path)
        {
            return BehaviorTreeAsset::exists(path);
        };

        validationReport = validation::validateBehaviorTree(*treeData, context);
        validationSeverities = validationReport.worstByNode();
        graphEditor.setValidationSeverities(&validationSeverities);
    }

    void BehaviorTreeEditorWindow::drawValidationPanel()
    {
        if (!treeData) return;

        const int errors = validationReport.errorCount();
        const int warnings = validationReport.warningCount();
        std::string summary = "Validation: Valid";
        validation::Severity summarySeverity = validation::Severity::Info;

        if (errors > 0)
        {
            summary = "Validation: " + std::to_string(errors) + " error(s), " +
                      std::to_string(warnings) + " warning(s)";
            summarySeverity = validation::Severity::Error;
        }
        else if (warnings > 0)
        {
            summary = "Validation: " + std::to_string(warnings) + " warning(s)";
            summarySeverity = validation::Severity::Warning;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, severityColor(summarySeverity));
        ImGuiTreeNodeFlags flags = (errors > 0 || warnings > 0) ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        bool open = ImGui::CollapsingHeader(summary.c_str(), flags);
        ImGui::PopStyleColor();

        if (!open) return;

        const float childHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.25f, 80.0f, 180.0f);
        ImGui::BeginChild("BTValidationDiagnostics", ImVec2(0, childHeight), true);

        if (validationReport.diagnostics.empty())
        {
            ImGui::TextDisabled("No diagnostics");
        }
        else
        {
            for (int i = 0; i < static_cast<int>(validationReport.diagnostics.size()); ++i)
            {
                const auto& diagnostic = validationReport.diagnostics[static_cast<size_t>(i)];
                const std::string label = diagnosticLabel(diagnostic);
                ImGui::PushID(i);
                ImGui::PushStyleColor(ImGuiCol_Text, severityColor(diagnostic.severity));
                if (diagnostic.nodeId != 0)
                {
                    if (ImGui::Selectable(label.c_str(), graphEditor.getSelectedNodeId() == diagnostic.nodeId))
                    {
                        graphEditor.selectNode(diagnostic.nodeId);
                    }
                }
                else
                {
                    ImGui::TextWrapped("%s", label.c_str());
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }

        ImGui::EndChild();
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

            behaviortree::BlackboardValue newValue = value;
            if (bt::drawBlackboardValueWidget(key.c_str(), bt::typeOfValue(value), newValue))
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

    void BehaviorTreeEditorWindow::drawDebugPanel()
    {
        if (!treeData) return;

        auto& dispatcher = events::EventDispatcher::instance();

        // --- Transport: pause / resume / step ---
        const bool paused = debugSnapshot.paused;
        if (paused)
        {
            if (ImGui::Button("Resume"))
            {
                events::ai::SetTreeDebugPausedCommand cmd;
                cmd.paused = false;
                dispatcher.execute(cmd);
            }
        }
        else
        {
            if (ImGui::Button("Pause"))
            {
                events::ai::SetTreeDebugPausedCommand cmd;
                cmd.paused = true;
                dispatcher.execute(cmd);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Step"))
        {
            dispatcher.execute(events::ai::StepTreeDebugCommand{});
        }
        ImGui::SameLine();
        ImGui::TextColored(paused ? ImVec4(1.0f, 0.6f, 0.2f, 1.0f) : ImVec4(0.3f, 0.85f, 0.4f, 1.0f),
                           paused ? "PAUSED" : "RUNNING");

        // --- Breakpoint toggle for the selected node ---
        const uint32_t selectedId = graphEditor.getSelectedNodeId();
        if (selectedId != 0)
        {
            const bool isBp = breakpoints.count(selectedId) != 0;
            std::string btnLabel = (isBp ? "Remove breakpoint on " : "Add breakpoint on ") +
                                   nodeLabel(treeData->graph, selectedId);
            if (ImGui::Button(btnLabel.c_str()))
            {
                if (isBp) breakpoints.erase(selectedId);
                else breakpoints.insert(selectedId);
                pushBreakpointsToRuntime();
            }
        }
        else
        {
            ImGui::TextDisabled("Select a node to toggle a breakpoint");
        }
        if (!breakpoints.empty())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear BPs"))
            {
                breakpoints.clear();
                pushBreakpointsToRuntime();
            }
        }

        if (!debugSnapshot.activeDynamicSubtreePath.empty())
        {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Dynamic subtree: %s",
                               debugSnapshot.activeDynamicSubtreePath.c_str());
        }

        // --- Aborts / interruptions ---
        if (!debugSnapshot.abortRecords.empty() &&
            ImGui::CollapsingHeader("Aborts / Interruptions"))
        {
            ImGui::BeginChild("BTAborts", ImVec2(0, 90.0f), true);
            for (const auto& record : debugSnapshot.abortRecords)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.35f, 1.0f));
                ImGui::TextWrapped("t%llu  %s  %s", static_cast<unsigned long long>(record.tickIndex),
                                   nodeLabel(treeData->graph, record.nodeId).c_str(), record.reason.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
        }

        // --- Execution history (most recent last) ---
        if (ImGui::CollapsingHeader("Execution History", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginChild("BTHistory", ImVec2(0, 140.0f), true);
            if (debugSnapshot.executionEvents.empty())
            {
                ImGui::TextDisabled("(no events)");
            }
            for (const auto& ev : debugSnapshot.executionEvents)
            {
                ImGui::Text("t%llu  %-8s %-8s %s", static_cast<unsigned long long>(ev.tickIndex),
                            btEventTypeLabel(ev.type), btStatusLabel(ev.status),
                            nodeLabel(treeData->graph, ev.nodeId).c_str());
            }
            // Keep the newest events in view.
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
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
                key.defaultValue = bt::defaultValueForType(key.type);
                onGraphChanged();
            }

            ImGui::SameLine();

            if (bt::drawBlackboardValueWidget("##default", key.type, key.defaultValue))
            {
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
