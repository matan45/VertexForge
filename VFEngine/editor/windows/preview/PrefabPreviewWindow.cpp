#include "print/Log.hpp"
#include "PrefabPreviewWindow.hpp"
#include "PrefabRigEditDetail.hpp"
#include "PreviewInputHandler.hpp"
#include "PreviewToolbar.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "imgui.h"
#include "ImGuizmo.h"
#include "events/EventDispatcher.hpp"
#include "events/render/PrefabRigPreviewEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp" // SavePrefabCommand
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <algorithm>

namespace windows
{
    // VK-1443 — the former file-local NaN-guard / timing / save-badge helpers now live in
    // PrefabRigEditDetail.hpp (namespace windows::prefabdetail) so the sub-controller TUs share them.

    // VK-1433 Phase 4 — the rig description is now re-derived from the live sandbox subtree via
    // buildPrefabRigDescFromEntity (PrefabRigLiveDescBuilder.hpp), not parsed from the prefab JSON.
    // The JSON-tree builder (buildPrefabRigDescDTO) is retained header-only for the Tests parity
    // suite, but the window no longer parses the prefab itself.

    std::unordered_map<std::uintptr_t, PrefabPreviewWindow*>& PrefabPreviewWindow::liveWindows()
    {
        static std::unordered_map<std::uintptr_t, PrefabPreviewWindow*> windows;
        return windows;
    }

    void PrefabPreviewWindow::resyncMirror(services::PreviewInstanceId id,
                                           const prefabrigedit::PrefabRigEditSnapshot& snap)
    {
        auto& windows = liveWindows();
        auto it = windows.find(id.raw());
        if (it != windows.end() && it->second)
            it->second->applyMirrorSnapshot(snap);
        // else: the window was closed — the controller CQRS already no-op'd; nothing to mirror.
    }

    void PrefabPreviewWindow::applyMirrorSnapshot(const prefabrigedit::PrefabRigEditSnapshot& snap)
    {
        // Restore only the mirror fields the snapshot engaged. The controller was already updated by
        // applyPrefabRigSnapshot (CQRS) BEFORE this runs; this keeps the window's panels in sync.
        // (VK-1433 Phase 4c/4d: the part Transform gizmo edits the source ENTITY transform directly;
        // its undo is the separate PrefabRigEntityTransformUndoCommand, so this socket/chain-mirror
        // path never handles transforms — the retired previewTransforms snapshot field is gone.)
        if (snap.chains.has_value())
            editChains = *snap.chains;

        // Sockets are per-part: only adopt them if the snapshot's part is the one currently selected
        // (otherwise the live editSockets belongs to a different part and must not be overwritten).
        if (snap.socketPart.has_value() && *snap.socketPart == selectedPart)
        {
            editSockets = snap.sockets;
            if (selectedSocketIndex >= static_cast<int>(editSockets.size()))
                selectedSocketIndex = -1;
        }

        // SHOULD-FIX #2: restore the IK target bindings onto the DTO. A binding change can't be a pure
        // CQRS push (no binding command); it requires re-resolving the assembly from the DTO. So if the
        // restored bindings differ from the live ones, write them back and rebuild — mirroring the live
        // bindingChanged path in drawIKPanel — then re-apply chains + sockets (the rebuild reloaded
        // them from disk). applyPrefabRigSnapshot's controller pushes are superseded by this rebuild.
        if (snap.ikBindings.has_value())
        {
            const auto& bindings = *snap.ikBindings;
            bool bindingChanged = false;
            for (size_t i = 0; i < rigDesc.ik.size() && i < bindings.size(); ++i)
            {
                if (rigDesc.ik[i].targetPartIndex != bindings[i].targetPartIndex ||
                    rigDesc.ik[i].targetSocketName != bindings[i].targetSocketName)
                {
                    rigDesc.ik[i].targetPartIndex = bindings[i].targetPartIndex;
                    rigDesc.ik[i].targetSocketName = bindings[i].targetSocketName;
                    bindingChanged = true;
                }
            }
            if (bindingChanged)
            {
                sandboxController.buildPreviewFromDesc(); // re-resolve with the restored bindings
                if (snap.chains.has_value())
                    authoringController.pushEditChains(); // restore chain configs onto the fresh assembly
                if (snap.socketPart.has_value() && *snap.socketPart == selectedPart && !editSockets.empty())
                    authoringController.pushEditSocketsForPart(selectedPart);
            }
        }
    }

    PrefabPreviewWindow::PrefabPreviewWindow(const std::string& filePath)
        : prefabPath(filePath)
        , camera(std::make_unique<editor::OrbitCamera>())
        // VK-1443 — bind the shared-state view to this window's fields (positional aggregate init;
        // order MUST match PrefabRigEditContext's member declaration order).
        , ctx{ getInstanceId(),
               sandboxRoot, rigDesc, partEntities, sandboxEntityCount,
               selectedSandboxEntity_, selectedPart, selectedSocketIndex, selectedChainIndex,
               editSockets, editChains, chainsLoaded,
               gizmoMode, bonePickArmed,
               previewInitialized, previewBuilt,
               prefabLoaded, loadFailed, errorMessage, prefabName, prefabVersion,
               partRefStatuses, missingRefCount,
               dirty }
        // VK-1443 — bind each sub-controller to the shared context + this window (friend access).
        , sandboxController(ctx, *this)
        , hierarchyPanel(ctx, *this)
        , authoringController(ctx, *this)
        , undoCoordinator(ctx, *this)
    {
        std::filesystem::path path(filePath);
        windowTitle = "Prefab Preview: " + path.filename().string();

        // Register in the live-window table so an undo command (which holds only our instanceId)
        // can find us — or safely resolve to nullptr once we are destroyed.
        liveWindows()[getInstanceId().raw()] = this;
    }

    PrefabPreviewWindow::~PrefabPreviewWindow()
    {
        // Deregister BEFORE any member is torn down, so a concurrent/queued undo resolves to nullptr
        // rather than a half-destroyed object.
        liveWindows().erase(getInstanceId().raw());

        // VK-1433 Phase 4 — intentionally NOT calling closeSandbox() here. In-session teardown is
        // explicit: the title-bar X flips isOpen, and draw()'s !isOpen branch runs closeSandbox()
        // (DeleteEntity + renderer cleanup) while the engine still lives. The dtor can run during
        // editor shutdown after the scene registry / EventDispatcher are gone, so dispatching
        // DeleteEntity / CleanUp then would be unsafe; the sandbox entity dies with the registry and
        // the preview controller is owned (and cleared) by the Core adapter. Mirrors the
        // UILayerBuilderWindow dtor note.
    }

    void PrefabPreviewWindow::draw()
    {
        if (!isOpen)
        {
            // VK-1433 Phase 4 — explicit in-session teardown: delete the sandbox subtree and tear
            // down the renderer while the engine is still alive. Guarded idempotent (closeSandbox
            // no-ops once the renderer is cleaned and the sandbox is gone).
            sandboxController.closeSandbox();
            return;
        }

        if (needsInit)
        {
            sandboxController.openSandbox();
            needsInit = false;
        }

        float currentTime = static_cast<float>(ImGui::GetTime());
        float deltaTime = lastFrameTime > 0.0f ? (currentTime - lastFrameTime) : 0.0f;
        lastFrameTime = currentTime;

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("PrefabPreview", ImVec2(1100, 650));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse | maximizer.windowFlags()))
        {
            if (isOpen)
            {
                maximizer.drawButton();

                // VK-1433 Phase 4c — Save / Save As + unsaved badge. Saving persists the whole live
                // sandbox edit (hierarchy + transforms + refs) back to the .vfPrefab.
                if (prefabSaveTimer > 0.0f) prefabSaveTimer -= ImGui::GetIO().DeltaTime;
                ImGui::SameLine();
                if (ImGui::Button("Save")) savePrefab(false);
                ImGui::SameLine();
                if (ImGui::Button("Save As...")) savePrefab(true);
                if (dirty)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(unsaved)");
                }
                prefabdetail::drawSaveBadge(prefabSaveTimer, prefabSaveSuccess);

                static float leftWidth = 160.0f;
                static float rightWidth = 280.0f;
                const float splitterThickness = 5.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                leftWidth = std::clamp(leftWidth, 130.0f, std::max(130.0f, contentSize.x * 0.35f));
                rightWidth = std::clamp(rightWidth, 220.0f, std::max(220.0f, contentSize.x * 0.45f));
                float middleWidth = contentSize.x - leftWidth - rightWidth - splitterThickness * 2.0f;

                // --- Left: info + tree ---
                ImGui::BeginChild("InfoPanel", ImVec2(leftWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::Separator();
                hierarchyPanel.drawEntityTreePanel();
                ImGui::EndChild();

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##prefabSplitL", splitterThickness, &leftWidth,
                                           &middleWidth, 130.0f, 300.0f, contentSize.y);
                ImGui::SameLine(0.0f, 0.0f);

                // --- Middle: 3D viewport ---
                ImGui::BeginChild("ViewportPanel", ImVec2(middleWidth, contentSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                drawViewport(middleWidth, contentSize.y, deltaTime);
                ImGui::EndChild();

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##prefabSplitR", splitterThickness, &middleWidth,
                                           &rightWidth, 300.0f, 220.0f, contentSize.y);
                ImGui::SameLine(0.0f, 0.0f);

                // --- Right: authoring + embedded entity inspector ---
                ImGui::BeginChild("AuthoringPanel", ImVec2(rightWidth, contentSize.y), true);
                authoringController.drawAuthoringPanel();
                ImGui::Separator();
                // VK-1433 Phase 4b — full embedded component inspector for the selected sandbox
                // entity (O1). Drawn after the rig authoring tabs so a component add/remove here
                // doesn't fold into the tab edit state.
                if (ImGui::CollapsingHeader("Entity Inspector", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    // VK-1433 Phase 4d — isolate the embedded inspector's ID scope. Without this, the
                    // inspector's CollapsingHeader("Transform") hashes to the SAME id as the gizmo
                    // toolbar's RadioButton("Transform") (both at the AuthoringPanel window-root scope,
                    // the toolbar being outside the tab bar), which ImGui flags as a conflicting-ID
                    // error. PushID re-bases every inspector widget so no inspector label can collide
                    // with an authoring-panel label drawn at the same level.
                    ImGui::PushID("entityInspector");
                    hierarchyPanel.drawEntityInspector();
                    ImGui::PopID();
                }
                ImGui::EndChild();

                // VK-1433 Phase 4d — confirm-on-delete modal. Drawn at the window-root scope (not in a
                // child) so OpenPopup/BeginPopupModal share the same popup-stack ID; the three delete
                // affordances only set the open flag (via requestDeleteEntity), this issues the popup.
                hierarchyPanel.drawDeleteConfirmPopup();
            }
        }
        ImGui::End();

        // VK-1433 Phase 4b — detect a structural component change made through the embedded inspector
        // (Add/Remove) — or any create/delete that didn't already rebuild — by diffing the sandbox
        // structure signature against last frame's, plus the transform-dirty cheap sync. Both live in
        // the sandbox controller now (VK-1443 tickEndOfFrame).
        if (isOpen && sandboxRoot.isValid())
            sandboxController.tickEndOfFrame();

        // VK-1433 Phase 4 — the title-bar X flips isOpen=false DURING this Begin/End, and
        // ImguiWindowHandler::draw() erases shouldClose() windows the SAME frame (so a follow-up
        // draw() with the !isOpen branch never runs). Tear the sandbox down HERE, this frame, while
        // the engine is still alive — closeSandbox() is idempotent and ImguiWindowHandler waitIdle's
        // before the erase, so the CQRS DeleteEntity + renderer cleanup are safe.
        if (!isOpen)
        {
            sandboxController.closeSandbox();
            if (!sizeSaved)
            {
                editor::preview::rememberWindowSize("PrefabPreview", maximizer.effectiveSize());
                sizeSaved = true;
            }
        }
    }

    void PrefabPreviewWindow::resyncRebuild(services::PreviewInstanceId id)
    {
        auto& windows = liveWindows();
        auto it = windows.find(id.raw());
        if (it != windows.end() && it->second)
            it->second->sandboxController.rebuildRigFromSandbox();
        // else: the window was closed — the SetTransformCommand replay already no-op'd on the dead
        // (deleted) entity; nothing to rebuild.
    }

    // ----------------------------------------------------------------------
    // VK-1433 Phase 4c — prefab save
    // ----------------------------------------------------------------------
    void PrefabPreviewWindow::savePrefab(bool saveAs)
    {
        if (!sandboxRoot.isValid()) return;

        std::string path = prefabPath;
        if (saveAs || path.empty())
        {
            path = fileDialog.saveFileDialog({{L"VF Prefab (*.vfPrefab)", L"*.vfPrefab"}}, L"vfPrefab");
            if (path.empty()) return; // user cancelled
        }

        prefabSaveTimer = prefabdetail::kSaveFeedbackSeconds;

        // Save the whole tagged sandbox subtree. PrefabSerialization strips PreviewSandboxTagComponent
        // and normalizes the root's isActive back to true, so the .vfPrefab round-trips in its real
        // (active, untagged) form. Mirrors UILayerBuilderWindow::saveLayer.
        events::scene::SavePrefabCommand cmd;
        cmd.entity = sandboxRoot;
        cmd.filePath = path;
        prefabSaveSuccess = events::EventDispatcher::instance().execute(cmd);

        if (prefabSaveSuccess)
        {
            prefabPath = path;
            dirty = false;

            events::resource::AssetSavedNotification notif;
            notif.filePath = path;
            events::EventDispatcher::instance().publish(notif);

            vfLogInfo("Prefab preview: saved sandbox to '{}'", path);
        }
        else
        {
            vfLogError("Prefab preview: SavePrefab failed for '{}'", path);
        }
    }

    // ----------------------------------------------------------------------
    // Viewport
    // ----------------------------------------------------------------------
    void PrefabPreviewWindow::drawViewport(float regionWidth, float regionHeight, float deltaTime)
    {
        (void)regionWidth;

        if (!prefabLoaded)
        {
            ImGui::TextDisabled("Loading prefab...");
            return;
        }
        if (!previewBuilt)
        {
            ImGui::TextDisabled("Prefab has no renderable rig parts.");
            return;
        }

        // Toolbar (environment + camera framing).
        editor::preview::PreviewToolbar::draw(environment, camera.get());

        // Rig debug overlays (VK-1433 Phase 1). Prefab-specific, so they live here rather than in
        // the shared PreviewToolbar. They ride the existing SetPrefabRigEnvironmentCommand channel.
        ImGui::TextDisabled("Overlays:");
        ImGui::SameLine();
        ImGui::Checkbox("Skeleton", &environment.showSkeleton);
        ImGui::SameLine();
        ImGui::Checkbox("Sockets", &environment.showSockets);
        ImGui::SameLine();
        ImGui::Checkbox("IK Targets", &environment.showIKTargets);

        // Debug shading + analytic lighting (VK-1433 Phase 3). Same prefab-specific channel; the
        // shading dropdown folds in-shader debug modes AND the wireframe pipeline variant.
        static const char* kShadingItems[] = {
            "Lit", "Clay", "Normals", "UVs", "Albedo (unlit)", "Wireframe"};
        ImGui::TextDisabled("Shading:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        int shadingIdx = static_cast<int>(environment.shadingMode);
        if (ImGui::Combo("##PrefabShadingMode", &shadingIdx, kShadingItems, IM_ARRAYSIZE(kShadingItems)))
        {
            environment.shadingMode = static_cast<uint8_t>(shadingIdx);
        }

        ImGui::SameLine();
        bool threePoint = (environment.lightingMode == editor::preview::LightingMode::ThreePoint);
        if (ImGui::Checkbox("3-Point Light", &threePoint))
        {
            environment.lightingMode = threePoint ? editor::preview::LightingMode::ThreePoint
                                                   : editor::preview::LightingMode::Default;
        }
        if (threePoint)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderFloat("Intensity", &environment.lightingIntensity, 0.0f, 4.0f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::SliderAngle("Azimuth", &environment.lightAzimuth, -180.0f, 180.0f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::SliderAngle("Elevation", &environment.lightElevation, -89.0f, 89.0f);
        }

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        float width = viewportSize.x;
        float height = viewportSize.y;
        if (width <= 0.0f || height <= 0.0f) return;

        camera->setAspectRatio(width / height);

        // Don't let the orbit camera fight the socket gizmo while it is being dragged.
        if (!ImGuizmo::IsUsing())
        {
            editor::preview::PreviewInputHandler::handleInput(camera.get(), isDraggingOrbit, isDraggingPan);
        }

        // TODO(Phase1-stretch): bone-pick socket creation. With the skeleton overlay on, project
        // each joint world (assembly skeleton accessor + camera view/proj) to screen, nearest-joint
        // hit-test on click, prefill a new SocketDefinition on editableSockets(part), then edit via
        // the existing gizmo + save through SetPrefabRigSocketsCommand -> .vfMesh. CPU picking only.

        // Advance the assembly first, then push camera + environment.
        {
            services::events::prefabrigpreview::UpdatePrefabRigPreviewCommand updateCmd;
            updateCmd.instanceId = getInstanceId();
            updateCmd.deltaTime = deltaTime;
            events::EventDispatcher::instance().execute(updateCmd);
        }

        {
            services::events::prefabrigpreview::SetPrefabRigRootMatrixCommand rootCmd;
            rootCmd.instanceId = getInstanceId();
            rootCmd.model = glm::mat4_cast(previewRotation);
            events::EventDispatcher::instance().execute(rootCmd);
        }

        {
            services::PreviewEnvironmentParams envParams;
            envParams.backgroundMode =
                static_cast<uint8_t>(environment.backgroundMode == editor::preview::BackgroundMode::Gradient ? 1 : 0);
            envParams.backgroundColor = environment.backgroundColor;
            envParams.gradientTopColor = environment.gradientTopColor;
            envParams.gradientBottomColor = environment.gradientBottomColor;
            envParams.showGrid = environment.showGrid;
            envParams.lightingMode =
                static_cast<uint8_t>(environment.lightingMode == editor::preview::LightingMode::ThreePoint ? 1 : 0);
            envParams.lightingIntensity = environment.lightingIntensity;
            envParams.showSkeleton = environment.showSkeleton;
            envParams.showSockets = environment.showSockets;
            envParams.showIKTargets = environment.showIKTargets;

            // VK-1433 Phase 1c — highlight the selected socket only while a socket tab is active (so
            // the selection is meaningful); otherwise leave -1/-1 (no highlight). Mirrors the
            // window's live selection into the overlay; the controller's socket loop draws a halo.
            const bool socketTabActive =
                gizmoMode == GizmoMode::BoneSocket || gizmoMode == GizmoMode::StaticSocket;
            envParams.highlightedSocketPart = socketTabActive ? selectedPart : -1;
            envParams.highlightedSocketIndex = socketTabActive ? selectedSocketIndex : -1;

            envParams.shadingMode = environment.shadingMode;
            envParams.lightAzimuth = environment.lightAzimuth;
            envParams.lightElevation = environment.lightElevation;

            services::events::prefabrigpreview::SetPrefabRigEnvironmentCommand envCmd;
            envCmd.instanceId = getInstanceId();
            envCmd.params = envParams;
            events::EventDispatcher::instance().execute(envCmd);
        }

        {
            services::events::prefabrigpreview::UpdatePrefabRigCameraCommand camCmd;
            camCmd.instanceId = getInstanceId();
            camCmd.view = camera->getViewMatrix();
            camCmd.projection = camera->getProjectionMatrix();
            camCmd.cameraPos = camera->getPosition();
            events::EventDispatcher::instance().execute(camCmd);
        }

        // Render query (update happened BEFORE Image, per editor-tool gotcha).
        services::events::prefabrigpreview::RenderPrefabRigPreviewQuery renderQuery;
        renderQuery.instanceId = getInstanceId();
        auto textureHandle = events::EventDispatcher::instance().query(renderQuery);

        if (textureHandle.imguiDescriptorSet)
        {
            ImGui::Image(textureHandle.imguiDescriptorSet, ImVec2(width, height));

            // The rendered Image's screen rect is the gizmo/pick viewport (== ImGuizmo::SetRect args).
            const ImVec2 imgMin = ImGui::GetItemRectMin();
            const ImVec2 imgSz = ImGui::GetItemRectSize();
            const bool imageHovered = ImGui::IsItemHovered();

            authoringController.drawGizmos();

            // An armed pick belongs to the Bone Socket tab's "Click a joint..." affordance; disarm
            // if the user has moved to another tab (mirrors the disarm-on-part-switch) so a stale
            // armed pick can't fire from a tab where that affordance isn't shown.
            if (gizmoMode != GizmoMode::BoneSocket)
                bonePickArmed = false;

            // VK-1433 Phase 1b — bone pick. After drawGizmos() so ImGuizmo::IsUsing() is current; a
            // click that the gizmo consumed must not also create a socket.
            if (bonePickArmed && imageHovered && !ImGuizmo::IsUsing() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                authoringController.tryBonePick(imgMin, imgSz);
            }
        }
        else
        {
            ImGui::Dummy(ImVec2(width, height));
        }

        (void)regionHeight;
    }

    // ----------------------------------------------------------------------
    // Info panel
    // ----------------------------------------------------------------------
    void PrefabPreviewWindow::drawInfoPanel()
    {
        if (loadFailed)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load");
            if (!errorMessage.empty())
            {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", errorMessage.c_str());
            }
            return;
        }

        if (!prefabLoaded)
        {
            ImGui::TextDisabled("Loading...");
            return;
        }

        ImGui::Text("Name:"); ImGui::TextWrapped("  %s", prefabName.c_str());
        ImGui::Text("Version: %s", prefabVersion.c_str());
        ImGui::Text("Entities: %u", sandboxEntityCount);
        ImGui::Text("Rig parts: %zu", rigDesc.parts.size());
        ImGui::Text("IK chains: %zu", rigDesc.ik.size());

        // Phase 2 — broken-asset-reference summary (computed once per build in revalidateRefs).
        if (missingRefCount > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%d missing ref%s",
                               missingRefCount, missingRefCount == 1 ? "" : "s");
        }
    }
}
