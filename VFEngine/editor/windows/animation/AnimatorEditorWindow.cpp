#include "print/Log.hpp"
#include "AnimatorEditorWindow.hpp"
#include "animator/AnimatorAsset.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "imgui.h"
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <filesystem>
#include <cctype>
#include <system_error>

namespace fs = std::filesystem;

namespace windows
{
    AnimatorEditorWindow::AnimatorEditorWindow(const std::string& path)
        : animatorPath(path)
          , instanceId(services::PreviewInstanceId(this))
    {
        fs::path filePath(path);
        windowTitle = "Animator Editor - " + filePath.stem().string();

        // Pick up saves of this file made elsewhere (VK-1656). Compared normalised, because each
        // publisher reports the path as it built it. The handler captures NO `this` (see the
        // header): the flags by shared_ptr, the path by value.
        assetSavedToken = events::EventDispatcher::instance()
            .subscribe<events::resource::AssetSavedNotification>(
                [changed = externalChange, ownSave = publishingOwnSave, watchedPath = animatorPath](
                    const events::resource::AssetSavedNotification& notification)
                {
                    if (ownSave->load())
                        return;

                    std::error_code ec;
                    const fs::path mine = fs::weakly_canonical(watchedPath, ec);
                    if (ec)
                        return;

                    std::error_code theirsEc;
                    const fs::path theirs = fs::weakly_canonical(notification.filePath, theirsEc);
                    if (!theirsEc && theirs == mine)
                        changed->store(true);
                });
    }

    AnimatorEditorWindow::~AnimatorEditorWindow()
    {
        if (assetSavedToken.isValid())
            events::EventDispatcher::instance().unsubscribe(assetSavedToken);

        nodeGraph.cleanUp();
    }

    void AnimatorEditorWindow::initEditor()
    {
        nodeGraph.init();
        loadAnimator();
        needsPositionInit = true;
        needsNavigateToContent = true;
        needsInit = false;
    }

    void AnimatorEditorWindow::loadAnimator()
    {
        auto result = animator::AnimatorAsset::load(animatorPath);
        if (result)
        {
            animatorData = std::make_unique<animator::AnimatorData>(std::move(*result));
            isDirty = false;
            vfLogInfo("Loaded animator: {}", animatorPath);
        }
        else
        {
            vfLogError("Failed to load animator: {}", animatorPath);
            animatorData = std::make_unique<animator::AnimatorData>(
                animator::AnimatorAsset::createDefault("New Animator"));
        }
    }

    void AnimatorEditorWindow::saveAnimator()
    {
        if (!animatorData)
            return;

        if (animator::AnimatorAsset::save(animatorPath, *animatorData))
        {
            isDirty = false;
            // These edits now overwrite whatever changed on disk.
            showExternalChangeBanner = false;
            vfLogInfo("Saved animator: {}", animatorPath);
            events::resource::AssetSavedNotification assetNotif;
            assetNotif.filePath = animatorPath;

            // publish() runs this window's own handler synchronously, inside the call.
            publishingOwnSave->store(true);
            try
            {
                events::EventDispatcher::instance().publish(assetNotif);
            }
            catch (...)
            {
                publishingOwnSave->store(false);
                throw;
            }
            publishingOwnSave->store(false);
        }
        else
        {
            vfLogError("Failed to save animator: {}", animatorPath);
        }
    }

    void AnimatorEditorWindow::reloadFromDisk()
    {
        auto result = animator::AnimatorAsset::load(animatorPath);
        if (!result)
        {
            // Keep what is on screen, unsaved edits included, rather than an empty default.
            vfLogError("Failed to reload animator: {}", animatorPath);
            return;
        }

        animatorData = std::make_unique<animator::AnimatorData>(std::move(*result));
        isDirty = false;
        showExternalChangeBanner = false;

        const uint32_t layerIndexBeforeReload = selectedLayerIndex;
        ensureLayersInitialized();
        if (!animatorData->layers.empty() && selectedLayerIndex >= animatorData->layers.size())
        {
            selectedLayerIndex = static_cast<uint32_t>(animatorData->layers.size()) - 1;
        }

        if (selectedLayerIndex != layerIndexBeforeReload)
        {
            selectedStateId = 0;
            selectedTransitionId = 0;
            needsNavigateToContent = true;
        }
        else if (const animator::AnimatorGraph* graph = getActiveGraph())
        {
            if (graph->findStateById(selectedStateId) == nullptr)
            {
                selectedStateId = 0;
            }
            const bool transitionExists = std::any_of(
                graph->transitions.begin(), graph->transitions.end(),
                [id = selectedTransitionId](const animator::AnimatorTransition& transition)
                {
                    return transition.id == id;
                });
            if (!transitionExists)
            {
                selectedTransitionId = 0;
            }
        }

        // The node editor keeps its own selection across graphs (and acts on it, e.g. Delete),
        // so it is re-seeded with what survived.
        nodeGraph.select(selectedStateId, selectedTransitionId, false);
        needsPositionInit = true;

        if (showValidationPanel)
        {
            validationIssues = animator::validateAnimator(*animatorData);
        }

        vfLogInfo("Reloaded animator: {}", animatorPath);
    }

    animator::AnimatorGraph* AnimatorEditorWindow::getActiveGraph()
    {
        if (!animatorData)
            return nullptr;

        if (!animatorData->layers.empty() && selectedLayerIndex < animatorData->layers.size())
        {
            return &animatorData->layers[selectedLayerIndex].graph;
        }

        return &animatorData->graph;
    }

    bool AnimatorEditorWindow::directClipLayerSelected() const
    {
        return animatorData && selectedLayerIndex < animatorData->layers.size() &&
               animatorData->layers[selectedLayerIndex].sourceMode == animator::LayerSourceMode::DirectClip;
    }

    void AnimatorEditorWindow::ensureLayersInitialized()
    {
        if (!animatorData || !animatorData->layers.empty())
            return;

        // Migrate single-graph format to layers format for editing. Not an edit, so the file
        // stays clean: the runtime plays a single graph exactly like this Override base layer
        // (AnimationLayerStack::initializeSingleGraphLayer vs initializeLayerRuntimes).
        animator::AnimationLayerData baseLayer;
        baseLayer.name = "Base Layer";
        baseLayer.weight = 1.0f;
        baseLayer.blendMode = animator::LayerBlendMode::Override;
        baseLayer.sourceMode = animator::LayerSourceMode::StateMachine;
        baseLayer.graph = animatorData->graph;
        animatorData->layers.push_back(std::move(baseLayer));
        selectedLayerIndex = 0;
    }

    void AnimatorEditorWindow::draw()
    {
        if (needsInit)
        {
            // The first load reads the file, so a save announced before it is already in.
            externalChange->store(false);
            initEditor();
            ensureLayersInitialized();
        }

        // Before the selected layer's graph is swapped into animatorData->graph below, because a
        // reload replaces animatorData.
        if (externalChange->exchange(false))
        {
            if (isDirty)
                showExternalChangeBanner = true;
            else
                reloadFromDisk();
        }

        // Per-asset ID so two open animators don't merge into one window.
        std::string title = windowTitle + (isDirty ? " *###Animator_" : "###Animator_") + animatorPath;

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("AnimatorEditor", ImVec2(1200, 800));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
        flags |= maximizer.windowFlags();
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            drawMenuBar();
            drawToolbar();
            drawExternalChangeBanner();

            ImVec2 contentSize = ImGui::GetContentRegionAvail();

            ImGui::BeginChild("MainContent", ImVec2(0, contentSize.y), false, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 innerSize = ImGui::GetContentRegionAvail();
                static float panelWidth = 300.0f;
                const float splitterThickness = 5.0f;
                panelWidth = std::clamp(panelWidth, 220.0f,
                                        std::max(220.0f, innerSize.x - 300.0f - splitterThickness));
                float graphWidth = innerSize.x - panelWidth - splitterThickness;

                // Save previous layer index and count to detect changes
                const uint32_t layerIndexBeforePanel = selectedLayerIndex;
                const size_t layerCountBeforePanel = animatorData ? animatorData->layers.size() : 0;

                ImGui::BeginChild("PropertiesPanel", ImVec2(panelWidth, innerSize.y), true);

                // Layers section
                if (ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    layerPanel.draw(animatorData.get(), selectedLayerIndex, isDirty);
                }

                ImGui::Separator();

                // Bone Masks section
                if (ImGui::CollapsingHeader("Bone Masks"))
                {
                    boneMaskPanel.draw(animatorData.get(), isDirty);
                }

                ImGui::Separator();

                // The selection may have moved in the layer panel above; removing the selected
                // layer also puts another layer under the same index.
                const bool layerChanged = selectedLayerIndex != layerIndexBeforePanel ||
                    (animatorData && animatorData->layers.size() < layerCountBeforePanel);
                if (layerChanged)
                {
                    selectedStateId = 0;
                    selectedTransitionId = 0;
                    // Layers share node ids, so the old layer's node selection would carry over.
                    nodeGraph.select(0, 0, false);
                    needsPositionInit = true;
                    needsNavigateToContent = true;
                }

                // The panels and the node graph below edit animatorData->graph, so the selected
                // layer's graph is swapped in until the swap-back after the node graph. Only that
                // layer is swapped: animatorData->graph holds the stale top-level copy here (the
                // previous frame's swap-back returned it), which must never land in a layer.
                const bool layerSwapped = animatorData && selectedLayerIndex < animatorData->layers.size();
                if (layerSwapped)
                {
                    std::swap(animatorData->graph, animatorData->layers[selectedLayerIndex].graph);
                }

                const bool directClipLayer = directClipLayerSelected();
                if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (directClipLayer)
                    {
                        ImGui::TextDisabled("Direct clip layer: select a state machine layer to edit parameters");
                        ImGui::BeginDisabled();
                    }
                    propertiesPanel.drawParametersPanel(animatorData.get(), isDirty, showAddParameterPopup);
                    if (directClipLayer)
                    {
                        ImGui::EndDisabled();
                    }
                }

                ImGui::Separator();

                if (selectedStateId != 0)
                {
                    if (ImGui::CollapsingHeader("State Properties", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        propertiesPanel.drawStatePropertiesPanel(animatorData.get(), selectedStateId, isDirty);
                    }
                }

                if (selectedTransitionId != 0)
                {
                    if (ImGui::CollapsingHeader("Transition Properties", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        propertiesPanel.drawTransitionPropertiesPanel(animatorData.get(), selectedTransitionId, isDirty);
                    }
                }

                ImGui::EndChild();

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##animatorSplit", splitterThickness, &panelWidth,
                                           &graphWidth, 220.0f, 300.0f, innerSize.y);
                ImGui::SameLine(0.0f, 0.0f);

                ImGui::BeginChild("NodeGraphPanel", ImVec2(graphWidth, innerSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                nodeGraph.draw(animatorData.get(), selectedStateId, selectedTransitionId,
                               isDirty, needsPositionInit, needsNavigateToContent, pendingZoomSteps);

                // Swap the edited graph back into the selected layer
                if (layerSwapped)
                {
                    std::swap(animatorData->graph, animatorData->layers[selectedLayerIndex].graph);
                }

                // After the swap-back, so the active graph is the layer's own (a layered file does
                // not save animatorData->graph). A DirectClip layer's graph is not saved at all.
                if (directClipLayer)
                {
                    showAddParameterPopup = false;
                }
                propertiesPanel.drawAddParameterPopup(directClipLayer ? nullptr : getActiveGraph(),
                                                       showAddParameterPopup, newParameterName, newParameterType,
                                                       isDirty);
                ImGui::EndChild();
            }
            ImGui::EndChild();
        }
        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("AnimatorEditor", maximizer.effectiveSize());
            sizeSaved = true;
        }
    }

    void AnimatorEditorWindow::drawMenuBar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    saveAnimator();
                }
                if (ImGui::MenuItem("Reload"))
                {
                    reloadFromDisk();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close"))
                {
                    isOpen = false;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                const bool editableGraph = !directClipLayerSelected();
                if (ImGui::MenuItem("Add State", nullptr, false, editableGraph))
                {
                    auto* graph = getActiveGraph();
                    if (graph)
                    {
                        animator::AnimatorState state;
                        state.id = graph->nextStateId++;
                        state.name = "New State " + std::to_string(state.id);
                        state.position = glm::vec2(200.0f, 100.0f);
                        graph->states.push_back(std::move(state));
                        isDirty = true;
                    }
                }
                if (ImGui::MenuItem("Add Parameter", nullptr, false, editableGraph))
                {
                    showAddParameterPopup = true;
                }
                ImGui::EndMenu();
            }

            maximizer.drawButton();

            ImGui::EndMenuBar();
        }
    }

    void AnimatorEditorWindow::drawToolbar()
    {
        if (ImGui::Button("Save"))
        {
            saveAnimator();
        }
        ImGui::SameLine();

        const bool directClipLayer = directClipLayerSelected();
        if (directClipLayer)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Add State"))
        {
            auto* graph = getActiveGraph();
            if (graph)
            {
                animator::AnimatorState state;
                state.id = graph->nextStateId++;
                state.name = "New State " + std::to_string(state.id);
                state.position = glm::vec2(200.0f + (graph->states.size() * 50.0f), 100.0f);
                graph->states.push_back(std::move(state));
                isDirty = true;
            }
        }
        if (directClipLayer)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Search
        ImGui::SetNextItemWidth(180.0f);
        const bool searchChanged = ImGui::InputTextWithHint(
            "##Search", ICON_FA_MAGNIFYING_GLASS " Search states by name...",
            searchBuffer, sizeof(searchBuffer));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Type to find a state by name and jump to it in the graph");
        if (searchChanged)
        {
            // Navigate to the first matching state of the selected layer
            const animator::AnimatorGraph* graph = getActiveGraph();
            if (searchBuffer[0] != '\0' && graph)
            {
                std::string query(searchBuffer);
                for (auto& c : query) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                for (const auto& state : graph->states)
                {
                    std::string name = state.name;
                    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                    if (name.find(query) != std::string::npos)
                    {
                        selectedStateId = state.id;
                        selectedTransitionId = 0;
                        nodeGraph.select(state.id, 0, true);
                        break;
                    }
                }
            }
        }

        ImGui::SameLine();

        // Validate graph
        if (ImGui::SmallButton(ICON_FA_TRIANGLE_EXCLAMATION " Validate"))
        {
            if (animatorData)
            {
                // Before draw() swaps the selected layer in, so every layer holds its own graph.
                validationIssues = animator::validateAnimator(*animatorData);
                showValidationPanel = true;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Check every layer for errors (no default state, undefined or mistyped "
                              "parameters, broken transitions) and warnings (dead ends, unreachable "
                              "states, missing clips). Click an issue to select it.");

        // Status badge
        if (showValidationPanel)
        {
            ImGui::SameLine();
            if (validationIssues.empty())
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                                   ICON_FA_CIRCLE_CHECK " No issues");
            else
                ImGui::TextColored(animator::hasErrors(validationIssues)
                                       ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f)
                                       : ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "(%zu)", validationIssues.size());
        }

        ImGui::Separator();

        // Validation panel - only shown when there are warnings to display.
        // On success the inline "No issues" badge above is enough; no need for an empty panel.
        if (showValidationPanel && !validationIssues.empty())
        {
            ImGui::BeginChild("ValidationPanel", ImVec2(0, 80), true);
            for (const animator::GraphIssue& issue : validationIssues)
            {
                const bool isError = issue.level == animator::GraphIssue::Level::Error;
                ImVec4 color = isError
                    ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f)
                    : ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
                const char* icon = isError ? ICON_FA_CIRCLE_EXCLAMATION : ICON_FA_TRIANGLE_EXCLAMATION;

                // Name the layer of an issue that is not on the one being edited.
                const bool otherLayer = isOnOtherLayer(issue);
                const bool layerExists = issueLayerExists(issue);
                std::string layerPrefix;
                if (otherLayer)
                {
                    const auto index = static_cast<size_t>(issue.layer);
                    layerPrefix = "[" + (layerExists ? animatorData->layers[index].name
                                                     : "Layer " + std::to_string(index)) + "] ";
                }
                ImGui::TextColored(color, "%s %s%s", icon, layerPrefix.c_str(), issue.message.c_str());

                const bool selectable = layerExists && (otherLayer || issue.stateId != 0 || issue.transitionId != 0);
                if (selectable && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to select it in the graph");
                if (selectable && ImGui::IsItemClicked())
                    focusIssue(issue);
            }
            ImGui::EndChild();
        }
    }

    void AnimatorEditorWindow::drawExternalChangeBanner()
    {
        if (!showExternalChangeBanner)
            return;

        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                           ICON_FA_TRIANGLE_EXCLAMATION " This animator changed on disk");
        ImGui::SameLine();
        // Drawn before draw() swaps the selected layer in, so reloading here is safe.
        if (ImGui::SmallButton("Reload##externalChange"))
        {
            reloadFromDisk();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Discard your unsaved edits and load the version on disk");
        ImGui::SameLine();
        if (ImGui::SmallButton("Keep mine##externalChange"))
        {
            showExternalChangeBanner = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Keep your edits; saving overwrites the version on disk");
        ImGui::Separator();
    }

    bool AnimatorEditorWindow::isOnOtherLayer(const animator::GraphIssue& issue) const
    {
        return issue.layer >= 0 && static_cast<uint32_t>(issue.layer) != selectedLayerIndex;
    }

    bool AnimatorEditorWindow::issueLayerExists(const animator::GraphIssue& issue) const
    {
        // Issues are a snapshot taken by Validate: a layer removed since then is gone.
        return animatorData &&
               (issue.layer < 0 || static_cast<size_t>(issue.layer) < animatorData->layers.size());
    }

    void AnimatorEditorWindow::focusIssue(const animator::GraphIssue& issue)
    {
        if (!issueLayerExists(issue))
            return;

        // Called from the toolbar, before draw() swaps the selected layer in, so a layer switch
        // here is the same single swap-in as a click in the layer panel.
        const bool switchLayer = isOnOtherLayer(issue);
        if (switchLayer)
        {
            selectedLayerIndex = static_cast<uint32_t>(issue.layer);
            needsPositionInit = true;
            needsNavigateToContent = true;
        }

        selectedStateId = issue.stateId;
        selectedTransitionId = issue.transitionId;
        // Another layer's node positions are only seeded by the next node graph draw, so the
        // view scrolls to the issue on the current layer only.
        nodeGraph.select(issue.stateId, issue.transitionId, !switchLayer);
    }
}
