#include "print/Log.hpp"
#include "PrefabPreviewWindow.hpp"
#include "asset/AssetRef.hpp"
#include "PreviewInputHandler.hpp"
#include "PreviewToolbar.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "MeshSocketWriter.hpp"
#include "MeshIKChainWriter.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
#include "imgui.h"
#include "ImGuizmo.h"
#include <IconsFontAwesome6.h>                      // eye / eye-slash hide toggle
#include "events/EventDispatcher.hpp"
#include "events/render/PrefabRigPreviewEvents.hpp"
#include "events/render/MaterialEvents.hpp"        // SetDefaultMaterialCommand (Phase 4c drag-swap)
#include "events/physics/SocketEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp" // LoadPrefab/SavePrefabCommand
#include "events/scene/EntityTransformEvents.hpp"  // Create/Delete/Get/Select/Reparent/Reorder/Name/SetTransform
#include "events/scene/ComponentMediaEvents.hpp"   // SetMeshDataCommand (Phase 4c drag-swap)
#include "math/TransformUtils.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <memory>
#include <functional>

using json = nlohmann::json;

namespace windows
{
    namespace
    {
        // De-hardcoded UI timings (formerly literal 3.0f / 0.25f sprinkled through the panels).
        constexpr float kSaveFeedbackSeconds = 3.0f;   // how long "Saved/Failed" stays on screen
        constexpr float kDefaultStateBlendSeconds = 0.25f; // default animator state-transition blend
    } // anonymous namespace

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
                buildPreviewFromDesc();                 // re-resolve with the restored bindings
                if (snap.chains.has_value())
                    pushEditChains();                   // restore chain configs onto the fresh assembly
                if (snap.socketPart.has_value() && *snap.socketPart == selectedPart && !editSockets.empty())
                    pushEditSocketsForPart(selectedPart);
            }
        }
    }

    PrefabPreviewWindow::PrefabPreviewWindow(const std::string& filePath)
        : prefabPath(filePath)
        , camera(std::make_unique<editor::OrbitCamera>())
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
            closeSandbox();
            return;
        }

        if (needsInit)
        {
            openSandbox();
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

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
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
                if (prefabSaveTimer > 0.0f)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(prefabSaveSuccess ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.3f, 0.3f, 1),
                                       prefabSaveSuccess ? "Saved" : "Failed");
                }

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
                drawEntityTreePanel();
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
                drawAuthoringPanel();
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
                    drawEntityInspector();
                    ImGui::PopID();
                }
                ImGui::EndChild();

                // VK-1433 Phase 4d — confirm-on-delete modal. Drawn at the window-root scope (not in a
                // child) so OpenPopup/BeginPopupModal share the same popup-stack ID; the three delete
                // affordances only set the open flag (via requestDeleteEntity), this issues the popup.
                drawDeleteConfirmPopup();
            }
        }
        ImGui::End();

        // VK-1433 Phase 4b — detect a structural component change made through the embedded inspector
        // (Add/Remove) — or any create/delete that didn't already rebuild — by diffing the sandbox
        // structure signature against last frame's. A change re-derives the rig. (Mutations issued by
        // the tree itself already call rebuildRigFromSandbox(), so this only fires for the inspector's
        // add/remove, which dispatch their own commands.)
        if (isOpen && sandboxRoot.isValid())
        {
            std::vector<uint64_t> sig = sandboxStructureSignature();
            if (sig != lastStructureSignature)
            {
                const bool firstFrame = lastStructureSignature.empty();
                lastStructureSignature = std::move(sig);
                if (!firstFrame)
                {
                    dirty = true;
                    rebuildRigFromSandbox();
                }
            }
        }

        // VK-1433 Phase 4 — the title-bar X flips isOpen=false DURING this Begin/End, and
        // ImguiWindowHandler::draw() erases shouldClose() windows the SAME frame (so a follow-up
        // draw() with the !isOpen branch never runs). Tear the sandbox down HERE, this frame, while
        // the engine is still alive — closeSandbox() is idempotent and ImguiWindowHandler waitIdle's
        // before the erase, so the CQRS DeleteEntity + renderer cleanup are safe.
        if (!isOpen)
        {
            closeSandbox();
            if (!sizeSaved)
            {
                editor::preview::rememberWindowSize("PrefabPreview", maximizer.effectiveSize());
                sizeSaved = true;
            }
        }
    }

    // ----------------------------------------------------------------------
    // VK-1433 Phase 4 — sandbox lifecycle (replaces the JSON-parse spine)
    // ----------------------------------------------------------------------
    namespace
    {
        // Count the entities in a (visible) sandbox subtree for the info panel, pruning hidden
        // nodes the same way the live builder does. CQRS-only (entt-free), cheap (only on rebuild).
        uint32_t countSandboxEntities(services::EntityHandle entity,
                                      const std::unordered_set<uint64_t>& hidden)
        {
            if (!entity.isValid() || hidden.count(entity.id) > 0) return 0;
            events::scene::GetEntityQuery q;
            q.entity = entity;
            auto data = events::EventDispatcher::instance().query(q);
            if (!data.has_value()) return 0;
            uint32_t n = 1;
            for (const services::EntityHandle& child : data->children)
                n += countSandboxEntities(child, hidden);
            return n;
        }
    }

    void PrefabPreviewWindow::readPrefabHeader()
    {
        // Only the prefab's display name / version come from the JSON header now; the entity
        // hierarchy is the LoadPrefab'd sandbox subtree. A missing header is non-fatal.
        prefabName = "Unnamed";
        prefabVersion = "unknown";
        try
        {
            std::ifstream file(prefabPath);
            if (!file.is_open()) return;
            json prefabJson = json::parse(file);
            prefabVersion = prefabJson.value("version", "unknown");
            if (auto it = prefabJson.find("prefab"); it != prefabJson.end() && it->is_object())
                prefabName = it->value("name", "Unnamed");
        }
        catch (const std::exception& e)
        {
            vfLogWarning("Prefab preview header read failed for '{}': {}", prefabPath, e.what());
        }
    }

    void PrefabPreviewWindow::openSandbox()
    {
        readPrefabHeader();
        initPreviewRenderer();

        // LoadPrefab into an isolated sandbox subtree (parent = scene root), then tag it so it is
        // excluded from the main passes + the scene serializer (mirrors UILayerBuilderWindow).
        events::scene::LoadPrefabCommand loadCmd;
        loadCmd.filePath = prefabPath;
        loadCmd.parent = std::nullopt;
        auto loaded = events::EventDispatcher::instance().execute(loadCmd);
        if (!loaded.has_value() || !loaded->isValid())
        {
            errorMessage = "Failed to load prefab";
            loadFailed = true;
            return;
        }
        sandboxRoot = *loaded;

        events::scene::MarkPreviewSandboxCommand markCmd;
        markCmd.entity = sandboxRoot;
        markCmd.tagged = true;
        events::EventDispatcher::instance().execute(markCmd);

        prefabLoaded = true;
        rebuildRigFromSandbox();

        // Initial selection: the first mesh part if there is one (so the authoring panels are
        // immediately usable), otherwise the sandbox root.
        if (!partEntities.empty())
            selectPart(0);
        else
            selectEntity(sandboxRoot);
    }

    void PrefabPreviewWindow::closeSandbox()
    {
        cleanUpPreviewRenderer();

        if (sandboxRoot.isValid())
        {
            // Destroy the whole tagged sandbox subtree. No untag needed — it's gone.
            events::scene::DeleteEntityCommand del;
            del.entity = sandboxRoot;
            events::EventDispatcher::instance().execute(del);
            sandboxRoot = services::EntityHandle::invalid();
        }
        partEntities.clear();
        hiddenEntities.clear();
        renamingEntity = services::EntityHandle::invalid();
        renameFocusPending = false;
        lastStructureSignature.clear();
    }

    void PrefabPreviewWindow::rebuildRigFromSandbox()
    {
        // Re-derive the rig description (and the parallel source entities) from the live sandbox
        // subtree, then rebuild the preview. Called on open and after every structural edit.
        LiveRigBuildResult built = buildPrefabRigDescFromEntity(sandboxRoot, hiddenEntities);
        rigDesc = std::move(built.desc);
        partEntities = std::move(built.partEntities);
        sandboxEntityCount = countSandboxEntities(sandboxRoot, hiddenEntities);

        revalidateRefs();
        buildPreviewFromDesc();

        // The assembly was rebuilt from the fresh desc.ik, so the IK panel must re-pull its chain
        // copies on next draw (it caches them once via chainsLoaded).
        chainsLoaded = false;

        // VK-1433 Phase 4b — a structural edit (reparent/reorder/create/delete/hide) can shuffle the
        // DFS-pre-order part indices, so re-map the selected part FROM the live selection (which is an
        // entity, stable across the rebuild). If the selection is no longer a part (or was deleted),
        // fall back to re-clamping into range. selectPart() re-pulls the part's sockets.
        const int mapped = partForEntity(selectedEntity());
        if (mapped >= 0)
        {
            selectedPart = mapped;
            pullEditSocketsForPart(selectedPart);
        }
        else if (selectedPart >= static_cast<int>(rigDesc.parts.size()))
        {
            selectedPart = rigDesc.parts.empty() ? -1 : static_cast<int>(rigDesc.parts.size()) - 1;
        }
    }

    // ----------------------------------------------------------------------
    // Rig preview lifecycle (service boundary)
    // ----------------------------------------------------------------------
    void PrefabPreviewWindow::initPreviewRenderer()
    {
        if (previewInitialized) return;

        services::events::prefabrigpreview::InitPrefabRigPreviewCommand cmd;
        cmd.instanceId = getInstanceId();
        events::EventDispatcher::instance().execute(cmd);
        previewInitialized = true;
    }

    void PrefabPreviewWindow::buildPreviewFromDesc()
    {
        if (!previewInitialized || rigDesc.parts.empty()) return;

        services::events::prefabrigpreview::BuildPrefabRigPreviewCommand cmd;
        cmd.instanceId = getInstanceId();
        cmd.desc = rigDesc;
        previewBuilt = events::EventDispatcher::instance().execute(cmd);

        if (!previewBuilt)
        {
            vfLogWarning("Prefab rig preview build produced no parts for: {}", prefabPath);
        }
    }

    void PrefabPreviewWindow::cleanUpPreviewRenderer()
    {
        if (previewCleanedUp) return;

        if (previewInitialized)
        {
            services::events::prefabrigpreview::CleanUpPrefabRigPreviewCommand cmd;
            cmd.instanceId = getInstanceId();
            events::EventDispatcher::instance().execute(cmd);
        }
        previewCleanedUp = true;
    }

    // ----------------------------------------------------------------------
    // Phase 2: broken-ref validation
    // ----------------------------------------------------------------------
    void PrefabPreviewWindow::revalidateRefs()
    {
        // Inject the real filesystem predicate; the free helper is unit-tested with a fake.
        partRefStatuses = prefabrigval::validatePartRefs(rigDesc, [](const std::string& p)
        {
            std::error_code ec;
            return std::filesystem::exists(p, ec);
        });
        missingRefCount = prefabrigval::countPartsWithMissingRefs(partRefStatuses);
    }

    void PrefabPreviewWindow::applyAssetDropToPart(int part, const std::string& assetPath)
    {
        if (part < 0 || part >= static_cast<int>(partEntities.size())) return;
        const services::EntityHandle entity = partEntities[part];
        if (!entity.isValid()) return;

        std::string ext = std::filesystem::path(assetPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        auto& dispatcher = events::EventDispatcher::instance();

        // VK-1433 Phase 4c — the part's SOURCE ENTITY is the source of truth (SavePrefab persists it),
        // so a drag-swap dispatches the persistent component command rather than mutating rigDesc.
        // .vfMesh / .vfAnim resolve onto the existing MeshData (preserving the other ref); .vfMaterial
        // re-points the default material. The rebuild re-derives the rig from the mutated entity.
        if (ext == ".vfmesh" || ext == ".vfanim")
        {
            // Read the current MeshData so we keep the unchanged ref (a .vfMesh drop preserves the
            // animator, a .vfAnim drop preserves the mesh + makes a static part skeletal).
            events::scene::GetMeshDataQuery mq;
            mq.entity = entity;
            services::MeshData meshData = dispatcher.query(mq).value_or(services::MeshData{});

            const asset::AssetRef ref = asset::AssetRef::fromPath(assetPath);
            if (ext == ".vfmesh") meshData.meshRef = ref;
            else                  meshData.animatorRef = ref;

            events::scene::SetMeshDataCommand cmd;
            cmd.entity = entity;
            cmd.meshData = meshData;
            dispatcher.execute(cmd);
        }
        else if (ext == ".vfmaterial")
        {
            // SetDefaultMaterialCommand takes a path directly (resolves + assigns the default slot).
            events::material::SetDefaultMaterialCommand cmd;
            cmd.entity = entity;
            cmd.materialPath = assetPath;
            dispatcher.execute(cmd);
        }
        else
        {
            return; // unknown extension — leave untouched
        }

        dirty = true;
        rebuildRigFromSandbox();
    }

    // ----------------------------------------------------------------------
    // Phase 2: edit-undo snapshot / push helpers
    // ----------------------------------------------------------------------
    prefabrigedit::PrefabRigEditSnapshot PrefabPreviewWindow::snapshotSockets() const
    {
        prefabrigedit::PrefabRigEditSnapshot snap;
        snap.socketPart = selectedPart;
        snap.sockets = editSockets;
        return snap;
    }

    std::vector<prefabrigedit::IKBindingSnapshot> PrefabPreviewWindow::snapshotIKBindings() const
    {
        std::vector<prefabrigedit::IKBindingSnapshot> out;
        out.reserve(rigDesc.ik.size());
        for (const auto& ik : rigDesc.ik)
            out.push_back({ik.targetPartIndex, ik.targetSocketName});
        return out;
    }

    prefabrigedit::PrefabRigEditSnapshot PrefabPreviewWindow::snapshotChains() const
    {
        prefabrigedit::PrefabRigEditSnapshot snap;
        snap.chains = editChains;
        snap.ikBindings = snapshotIKBindings(); // carry the transient target bindings too
        return snap;
    }

    void PrefabPreviewWindow::pushSocketUndo(prefabrigedit::PrefabRigEditSnapshot before)
    {
        prefabrigedit::PrefabRigEditSnapshot after = snapshotSockets();
        if (before.socketPart == after.socketPart &&
            prefabrigedit::socketsEqual(before.sockets, after.sockets))
            return; // no actual change — don't pollute the stack

        const services::PreviewInstanceId id = getInstanceId();
        // Capture ONLY the id (a value) — never `this`. resyncMirror looks the window up by id and
        // no-ops if it has been closed, so the command is safe on the process-global undo stack.
        auto cmd = std::make_shared<prefabrigedit::PrefabRigEditUndoCommand>(
            id, std::move(before), std::move(after), "Edit rig sockets",
            [id](const prefabrigedit::PrefabRigEditSnapshot& snap)
            {
                PrefabPreviewWindow::resyncMirror(id, snap);
            });

        events::undoredo::PushUndoableCommand push;
        push.command = std::make_shared<services::SharedUndoCommand>(std::move(cmd));
        events::EventDispatcher::instance().execute(push);
    }

    void PrefabPreviewWindow::pushChainUndo(prefabrigedit::PrefabRigEditSnapshot before)
    {
        prefabrigedit::PrefabRigEditSnapshot after = snapshotChains();

        // SHOULD-FIX #2: the IK target BINDING (rigDesc.ik[i].targetPartIndex/targetSocketName) is
        // NOT part of IKChainConfig, so chainsEqual ignores it. snapshotChains() captures the bindings
        // too (both before, at session start, and after here) so a re-target is undoable.
        const bool chainsChanged = !(before.chains.has_value() && after.chains.has_value() &&
                                     prefabrigedit::chainsEqual(*before.chains, *after.chains));
        const bool bindingsChanged = !(before.ikBindings.has_value() && after.ikBindings.has_value() &&
                                       prefabrigedit::ikBindingsEqual(*before.ikBindings, *after.ikBindings));
        if (!chainsChanged && !bindingsChanged)
            return; // nothing actually changed — don't pollute the stack

        // A chain undo restores chains AND re-applies the currently selected part's sockets, because
        // a target-binding change rebuilds the assembly (reloading sockets from disk). Carry the live
        // sockets along so an undo/redo that touched a binding does not strand stale socket data.
        if (selectedPart >= 0 && !editSockets.empty())
        {
            before.socketPart = selectedPart;
            before.sockets = editSockets; // restored state mirrors what's live now
            after.socketPart = selectedPart;
            after.sockets = editSockets;
        }

        const services::PreviewInstanceId id = getInstanceId();
        auto cmd = std::make_shared<prefabrigedit::PrefabRigEditUndoCommand>(
            id, std::move(before), std::move(after), "Edit IK chains",
            [id](const prefabrigedit::PrefabRigEditSnapshot& snap)
            {
                PrefabPreviewWindow::resyncMirror(id, snap);
            });

        events::undoredo::PushUndoableCommand push;
        push.command = std::make_shared<services::SharedUndoCommand>(std::move(cmd));
        events::EventDispatcher::instance().execute(push);
    }

    void PrefabPreviewWindow::resyncRebuild(services::PreviewInstanceId id)
    {
        auto& windows = liveWindows();
        auto it = windows.find(id.raw());
        if (it != windows.end() && it->second)
            it->second->rebuildRigFromSandbox();
        // else: the window was closed — the SetTransformCommand replay already no-op'd on the dead
        // (deleted) entity; nothing to rebuild.
    }

    void PrefabPreviewWindow::pushTransformUndo(services::EntityHandle entity,
                                                const services::TransformData& before,
                                                const services::TransformData& after)
    {
        if (!entity.isValid() || before == after)
            return; // nothing actually changed — don't pollute the stack

        const services::PreviewInstanceId id = getInstanceId();
        // Capture ONLY the id (a value) — never `this`. resyncRebuild looks the window up by id and
        // no-ops if it has been closed, so the command is safe on the process-global undo stack.
        auto cmd = std::make_shared<prefabrigedit::PrefabRigEntityTransformUndoCommand>(
            entity, before, after, "Edit part transform",
            [id]() { PrefabPreviewWindow::resyncRebuild(id); });

        events::undoredo::PushUndoableCommand push;
        push.command = std::make_shared<services::SharedUndoCommand>(std::move(cmd));
        events::EventDispatcher::instance().execute(push);
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

        prefabSaveTimer = kSaveFeedbackSeconds;

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

            drawGizmos();

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
                tryBonePick(imgMin, imgSz);
            }
        }
        else
        {
            ImGui::Dummy(ImVec2(width, height));
        }

        (void)regionHeight;
    }

    glm::mat4 PrefabPreviewWindow::partWorldLive(int part) const
    {
        if (part < 0) return glm::mat4(1.0f);
        services::events::prefabrigpreview::GetPrefabRigPartWorldQuery q;
        q.instanceId = getInstanceId();
        q.part = static_cast<size_t>(part);
        return events::EventDispatcher::instance().query(q);
    }

    void PrefabPreviewWindow::drawGizmos()
    {
        // Exactly ONE gizmo is drawn per frame (gated by gizmoMode) so they never fight over the
        // mouse. Bone-socket + IK modes have no viewport gizmo (bone sockets ride the live pose;
        // IK is panel-driven), so only Transform and StaticSocket draw here.
        switch (gizmoMode)
        {
        case GizmoMode::Transform:    drawTransformGizmo(); break;
        case GizmoMode::StaticSocket: drawSocketGizmo();    break;
        case GizmoMode::BoneSocket:
        case GizmoMode::IK:           break;
        }
    }

    void PrefabPreviewWindow::drawTransformGizmo()
    {
        // VK-1433 Phase 4c — TRS gizmo on the selected part's SOURCE ENTITY transform (Q5). Dragging
        // the gizmo edits the entity's OWN local transform (SetTransformCommand), so the change is
        // persisted by Save Prefab. Root part -> moves the whole rig; a socket-attached child's
        // translation is dropped at instantiation (the Transform panel warns about this), but
        // rotation/scale carry — consistent with attachChildRotation/attachChildScale.
        if (selectedPart < 0 || selectedPart >= static_cast<int>(partEntities.size())) return;

        ImVec2 imgMin = ImGui::GetItemRectMin();
        ImVec2 imgSz = ImGui::GetItemRectSize();
        if (imgSz.x <= 0.0f || imgSz.y <= 0.0f) return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSz.x, imgSz.y);

        // Undo Vulkan Y-flip for ImGuizmo (expects OpenGL-style projection).
        glm::mat4 view = camera->getViewMatrix();
        glm::mat4 proj = camera->getProjectionMatrix();
        proj[1][1] *= -1.0f;

        // The assembly composes the live partWorld from the part's parent chain folded together with
        // the part's OWN local transform. Treat that own-local matrix as the editable factor: recover
        // its containing frame `base = liveWorld * inverse(compose(ownLocal))`, then a manipulated
        // world maps back to the new own-local via `inverse(base) * newWorld`. This is the same
        // base/inverse(base) pattern the old preview gizmo used, but the editable factor is now the
        // persisted entity transform rather than the retired transient preview offset.
        const services::TransformData ownLocal = partSourceLocalTransform(selectedPart);
        const glm::mat4 ownLocalMat = math::composeMatrix(ownLocal.position, ownLocal.rotation, ownLocal.scale);
        const glm::mat4 liveWorld = partWorldLive(selectedPart);
        const glm::mat4 base = liveWorld * glm::inverse(ownLocalMat);

        glm::mat4 objectMatrix = liveWorld;

        // VK-1433 Phase 4d — undo bracket. ImGuizmo drags do NOT register as ImGui items, so snapshot
        // the entity's pre-drag transform on the IsUsing() rising edge (ownLocal is read BEFORE the
        // Manipulate below, so it's the pristine state even on the first drag frame), then push ONE
        // coalesced entity-transform undo on release.
        const bool usingGizmo = ImGuizmo::IsUsing();
        if (usingGizmo && !transformGizmoEditActive)
        {
            transformGizmoEditActive = true;
            transformGizmoBefore = ownLocal;
        }

        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 transformGizmoOp, ImGuizmo::WORLD, glm::value_ptr(objectMatrix)))
        {
            // newWorld = base * newOwnLocal  =>  newOwnLocal = inverse(base) * newWorld.
            const glm::mat4 newOwnLocal = glm::inverse(base) * objectMatrix;
            const math::DecomposedTransform d = math::decomposeMatrix(newOwnLocal);

            services::TransformData t;
            t.position = d.position;
            t.rotation = d.rotation; // Euler XYZ degrees == TransformComponent schema
            t.scale = d.scale;

            events::scene::SetTransformCommand cmd;
            cmd.entity = partEntities[selectedPart];
            cmd.transform = t;
            events::EventDispatcher::instance().execute(cmd);

            dirty = true;
            rebuildRigFromSandbox(); // re-derive the rig so the preview tracks the entity edit
        }

        if (!usingGizmo && transformGizmoEditActive)
        {
            transformGizmoEditActive = false;
            // after = the entity transform as it stands now (post-drag). No-op-gated inside.
            pushTransformUndo(partEntities[selectedPart], transformGizmoBefore,
                              partSourceLocalTransform(selectedPart));
        }
    }

    void PrefabPreviewWindow::drawSocketGizmo()
    {
        // Only when authoring a STATIC part's socket (skeletal sockets ride bones; no gizmo).
        if (selectedPart < 0 || partIsSkeletal(selectedPart)) return;
        if (selectedSocketIndex < 0 || selectedSocketIndex >= static_cast<int>(editSockets.size())) return;

        ImVec2 imgMin = ImGui::GetItemRectMin();
        ImVec2 imgSz = ImGui::GetItemRectSize();
        if (imgSz.x <= 0.0f || imgSz.y <= 0.0f) return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSz.x, imgSz.y);

        // Undo Vulkan Y-flip for ImGuizmo (expects OpenGL-style projection).
        glm::mat4 view = camera->getViewMatrix();
        glm::mat4 proj = camera->getProjectionMatrix();
        proj[1][1] *= -1.0f;

        // VK-1433 F1 fix: anchor at the part's LIVE composed world (where the part actually renders
        // after the attachment chain), not the turntable-local matrix. The gizmo still edits the
        // part's OWN socket offset, so we transform between socket-local and world through partWorld.
        auto& socket = editSockets[selectedSocketIndex];
        glm::mat4 model = partWorldLive(selectedPart);
        glm::mat4 objectMatrix = model * socket.getLocalOffsetMatrix();

        // Undo bracket on the gizmo drag (ImGuizmo drags do NOT register as ImGui items, so the
        // socket-panel IsAnyItemActive bracket does not catch them). Snapshot on the IsUsing() rising
        // edge, push a socket undo on release.
        const bool usingGizmo = ImGuizmo::IsUsing();
        if (usingGizmo && !gizmoEditActive)
        {
            gizmoEditActive = true;
            gizmoEditBefore = snapshotSockets();
        }

        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 socketGizmoOp, ImGuizmo::LOCAL, glm::value_ptr(objectMatrix)))
        {
            glm::mat4 localMatrix = glm::inverse(model) * objectMatrix;
            float translation[3], rotation[3], scale[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localMatrix), translation, rotation, scale);
            socket.localPosition = glm::vec3(translation[0], translation[1], translation[2]);
            socket.localRotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
            pushEditSocketsForPart(selectedPart); // live: next update() re-resolves
        }

        if (!usingGizmo && gizmoEditActive)
        {
            gizmoEditActive = false;
            pushSocketUndo(std::move(gizmoEditBefore));
        }
    }

    void PrefabPreviewWindow::tryBonePick(const ImVec2& viewportMin, const ImVec2& viewportSize)
    {
        // Guard: only meaningful for a skeletal part with the skeleton overlay visible (the joints the
        // user is clicking are the overlay's markers). The caller already gated on the arm flag + a
        // non-gizmo click inside the Image.
        if (selectedPart < 0 || !partIsSkeletal(selectedPart) || !environment.showSkeleton)
            return;

        // World-space joints for the part (queried across the preview boundary; entt-free).
        services::events::prefabrigpreview::GetPrefabRigJointWorldsQuery jq;
        jq.instanceId = getInstanceId();
        jq.part = static_cast<size_t>(selectedPart);
        const std::vector<services::PrefabRigJoint> joints =
            events::EventDispatcher::instance().query(jq);
        if (joints.empty())
            return;

        std::vector<glm::vec3> jointWorlds;
        jointWorlds.reserve(joints.size());
        for (const services::PrefabRigJoint& j : joints)
            jointWorlds.push_back(j.world);

        // Same camera matrices the gizmo path uses (the helper applies the Vulkan-Y flip itself).
        const glm::mat4 view = camera->getViewMatrix();
        const glm::mat4 proj = camera->getProjectionMatrix();

        const ImVec2 mouse = ImGui::GetMousePos();
        constexpr float kPickPixelThreshold = 18.0f; // generous click radius around a joint marker
        const prefabrigpick::JointPickResult pick = prefabrigpick::nearestJointToScreenPoint(
            jointWorlds, view, proj,
            glm::vec2(viewportMin.x, viewportMin.y), glm::vec2(viewportSize.x, viewportSize.y),
            glm::vec2(mouse.x, mouse.y), kPickPixelThreshold);
        if (!pick.hit())
            return; // clicked empty space — no-op

        const services::PrefabRigJoint& hitJoint = joints[static_cast<size_t>(pick.index)];

        // Phase-2 undo bracket: adding a socket is a STRUCTURAL edit, so snapshot the sockets before
        // the add and push one coalesced entry after (mirrors pushSocketUndo's before/after contract).
        prefabrigedit::PrefabRigEditSnapshot before = snapshotSockets();

        // Prefill the new bone socket: ride the picked bone, identity local offset (the user fine-tunes
        // it with the existing gizmo/fields). De-dup the default name against the live sockets.
        animator::SocketDefinition s;
        s.targetBoneName = hitJoint.boneName;
        s.boneIndex = hitJoint.boneIndex;
        const std::string baseName =
            (hitJoint.boneName.empty() ? std::string("bone") : hitJoint.boneName) + "_socket";
        std::string candidate = baseName;
        for (int suffix = 2; animator::indexOfSocket(editSockets, candidate) >= 0; ++suffix)
            candidate = baseName + "_" + std::to_string(suffix);
        s.name = candidate;

        editSockets.push_back(std::move(s));
        selectedSocketIndex = static_cast<int>(editSockets.size()) - 1;
        pushEditSocketsForPart(selectedPart); // live: next update() re-resolves with the new socket

        pushSocketUndo(std::move(before));

        bonePickArmed = false; // one pick per arm — make the disarm obvious in the panel
    }

    // ----------------------------------------------------------------------
    // Info / tree panels
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

    namespace
    {
        constexpr const char* kPrefabSceneEntityPayload = "DND_SCENE_ENTITY"; // shared with SceneHierarchyPanel

        // Is `ancestor` an ancestor of (or equal to) `node` within the sandbox subtree? Used to guard
        // a reparent/reorder that would create a cycle (drop a node onto its own descendant). CQRS-only.
        bool isAncestorOrSelf(services::EntityHandle ancestor, services::EntityHandle node)
        {
            services::EntityHandle cur = node;
            while (cur.isValid())
            {
                if (cur == ancestor) return true;
                events::scene::GetEntityQuery q;
                q.entity = cur;
                auto data = events::EventDispatcher::instance().query(q);
                if (!data.has_value() || !data->parent.has_value()) break;
                cur = *data->parent;
            }
            return false;
        }

        // Is an entity-hierarchy drag currently in flight? (mirror SceneHierarchyPanel) — gates the
        // between-siblings reorder drop zones so they only appear mid-drag.
        bool isPrefabEntityDragActive()
        {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            return payload != nullptr && payload->IsDataType(kPrefabSceneEntityPayload);
        }
    }

    services::EntityHandle PrefabPreviewWindow::createChildEntity(services::EntityHandle parent)
    {
        if (!parent.isValid()) parent = sandboxRoot;
        if (!parent.isValid()) return services::EntityHandle::invalid();

        events::scene::CreateEntityCommand cmd;
        cmd.name = "Entity";
        cmd.parent = parent;
        const services::EntityHandle created = events::EventDispatcher::instance().execute(cmd);
        if (created.isValid())
        {
            selectEntity(created);
            dirty = true;
            rebuildRigFromSandbox();
        }
        return created;
    }

    void PrefabPreviewWindow::requestDeleteEntity(services::EntityHandle entity)
    {
        // VK-1433 Phase 4d — stage the delete + open the confirm modal instead of deleting now. Never
        // the sandbox root. The actual DeleteEntityCommand runs in deleteEntity() on confirm.
        if (!entity.isValid() || entity == sandboxRoot) return;

        pendingDeleteEntity = entity;
        // Capture the name now (for the modal message) — the entity still exists at request time.
        events::scene::GetEntityQuery q;
        q.entity = entity;
        auto data = events::EventDispatcher::instance().query(q);
        pendingDeleteName = (data.has_value() && !data->name.empty()) ? data->name : "(unnamed)";
        openDeleteConfirmPopup = true; // consumed by drawDeleteConfirmPopup() this frame
    }

    bool PrefabPreviewWindow::deleteEntity(services::EntityHandle entity)
    {
        // Never the sandbox root — that's the saved subtree's top (the prefab itself).
        if (!entity.isValid() || entity == sandboxRoot) return false;

        const bool wasSelected = (selectedEntity() == entity);
        events::scene::DeleteEntityCommand del;
        del.entity = entity;
        events::EventDispatcher::instance().execute(del);
        if (wasSelected) selectEntity(sandboxRoot);
        hiddenEntities.erase(entity.id);
        dirty = true;
        rebuildRigFromSandbox();
        return true;
    }

    void PrefabPreviewWindow::drawDeleteConfirmPopup()
    {
        // VK-1433 Phase 4d — confirm-on-delete modal shared by all three delete affordances. Opened by
        // requestDeleteEntity (sets openDeleteConfirmPopup); Delete runs DeleteEntityCommand via
        // deleteEntity, Cancel is a no-op. The popup is defined at the window-root ID scope (called once
        // per frame from draw(), outside the hierarchy recursion).
        if (openDeleteConfirmPopup)
        {
            ImGui::OpenPopup("Delete Entity?##prefabDelete");
            openDeleteConfirmPopup = false;
        }

        // Center the modal over the window.
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Delete Entity?##prefabDelete", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete '%s' and its children?", pendingDeleteName.c_str());
            ImGui::TextDisabled("This removes the entity (and its descendants) from the prefab and "
                                "cannot be undone.");
            ImGui::Spacing();
            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                deleteEntity(pendingDeleteEntity);
                pendingDeleteEntity = services::EntityHandle::invalid();
                pendingDeleteName.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                pendingDeleteEntity = services::EntityHandle::invalid();
                pendingDeleteName.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }
    }

    void PrefabPreviewWindow::drawEntityTreePanel()
    {
        // VK-1433 Phase 4d — Hierarchy header toolbar: "+ Add" (child under selection/root) and a
        // trash "Delete" (enabled only for a non-root selection). Both share createChildEntity /
        // deleteEntity with the per-node context menu and the Delete-key shortcut.
        ImGui::TextDisabled("Hierarchy");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_PLUS " Add##addEntity"))
        {
            createChildEntity(selectedEntity().isValid() ? selectedEntity() : sandboxRoot);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add a child entity under the selection (or the root)");

        ImGui::SameLine();
        const services::EntityHandle sel = selectedEntity();
        const bool canDelete = sel.isValid() && sel != sandboxRoot;
        ImGui::BeginDisabled(!canDelete);
        if (ImGui::SmallButton(ICON_FA_TRASH " Delete##delEntity"))
        {
            requestDeleteEntity(sel); // confirm modal; actual delete on confirm
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(canDelete ? "Delete the selected entity (Del)"
                                        : "Select a non-root entity to delete (Del)");

        if (loadFailed || !prefabLoaded || !sandboxRoot.isValid())
        {
            ImGui::TextDisabled("No data");
            return;
        }
        // VK-1433 Phase 4b — CQRS-recursive tree over the live sandbox subtree with add/remove/
        // rename/reparent/reorder/hide. Drag a node onto another to reparent; onto a between-siblings
        // zone to reorder; double-click to rename; the eye toggles editor-only preview hide.
        drawEntityNode(sandboxRoot, 0);

        // VK-1433 Phase 4d — Delete-key shortcut: when this hierarchy panel (or any of its children) is
        // focused and no inline rename / text field is active, Del removes the selected non-root entity
        // via the same deleteEntity path. IsWindowFocused(RootAndChildWindows) covers the InfoPanel
        // child the tree lives in; the rename guard avoids stealing Del from the rename InputText.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !renamingEntity.isValid() && !ImGui::IsAnyItemActive() &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            requestDeleteEntity(selectedEntity()); // confirm modal; actual delete on confirm
        }
    }

    void PrefabPreviewWindow::drawReorderDropZone(services::EntityHandle parent, size_t index)
    {
        ImGui::PushID(static_cast<int>(index));
        const float width = std::max(ImGui::GetContentRegionAvail().x, 10.0f);
        ImGui::InvisibleButton("##reorderZone", ImVec2(width, 4.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPrefabSceneEntityPayload))
            {
                const services::EntityHandle dragged =
                    *static_cast<const services::EntityHandle*>(payload->Data);
                // Guard: a node can't be reordered under itself or any of its own descendants.
                if (dragged.isValid() && dragged != parent && !isAncestorOrSelf(dragged, parent))
                {
                    events::scene::ReorderEntityCommand cmd;
                    cmd.entity = dragged;
                    cmd.newParent = parent;
                    cmd.insertIndex = static_cast<int>(index);
                    events::EventDispatcher::instance().execute(cmd);
                    dirty = true;
                    rebuildRigFromSandbox();
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }

    std::string PrefabPreviewWindow::missingRefTooltip(int part) const
    {
        if (part < 0 || part >= static_cast<int>(partRefStatuses.size())) return {};
        const prefabrigval::PartRefStatus& s = partRefStatuses[part];
        if (!s.anyMissing()) return {};

        const services::PrefabRigPartDTO& p = rigDesc.parts[part];
        std::string out;
        auto addLine = [&out](const std::string& label, const std::string& path)
        {
            if (!out.empty()) out += "\n";
            out += label + " not found: " + path;
        };
        if (s.meshMissing) addLine("Mesh", p.meshPath);
        if (s.animatorMissing) addLine("Animator", p.animatorPath);
        if (s.retargetMissing) addLine("Retarget", p.retargetPath);
        if (s.defaultMaterialMissing) addLine("Material", p.defaultMaterialPath);
        for (const auto& submesh : s.missingSubMeshMaterials)
        {
            auto it = p.subMeshMaterials.find(submesh);
            addLine("Submesh material '" + submesh + "'", it != p.subMeshMaterials.end() ? it->second : "");
        }
        return out;
    }

    void PrefabPreviewWindow::drawEntityNode(services::EntityHandle entity, int depth)
    {
        if (!entity.isValid()) return;

        events::scene::GetEntityQuery q;
        q.entity = entity;
        auto data = events::EventDispatcher::instance().query(q);
        if (!data.has_value()) return;

        ImGui::PushID(static_cast<int>(entity.id));

        // A mesh-bearing entity maps to a rig part (partEntities is the live builder's parallel
        // source-entity vector); use it for the [skel]/[static] badge and the broken-ref tint.
        const int thisPart = partForEntity(entity);
        const std::string tooltip = (thisPart >= 0) ? missingRefTooltip(thisPart) : std::string();
        const bool missing = !tooltip.empty();

        const bool isSelected = (selectedEntity() == entity);
        const bool hasChildren = !data->children.empty();
        const bool isRoot = (entity == sandboxRoot);
        const bool isRenaming = (renamingEntity == entity);
        const bool isHidden = hiddenEntities.count(entity.id) > 0;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen
                                   | ImGuiTreeNodeFlags_SpanAvailWidth
                                   | ImGuiTreeNodeFlags_AllowOverlap; // let the right-aligned eye button take its own clicks
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

        std::string label = data->name.empty() ? "(unnamed)" : data->name;
        if (thisPart >= 0) label += partIsSkeletal(thisPart) ? " [skel]" : " [static]";
        if (isHidden) label += " (hidden)";

        // A hidden node (editor-only preview hide) is dimmed; a broken-ref node is tinted red.
        bool pushedColor = false;
        if (missing) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f)); pushedColor = true; }
        else if (isHidden) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f)); pushedColor = true; }

        const bool nodeOpen = ImGui::TreeNodeEx("##node", flags, "%s%s",
                                                isRenaming ? "" : label.c_str(),
                                                (missing && !isRenaming) ? "  (!)" : "");
        if (pushedColor) ImGui::PopStyleColor();
        if (missing && !isRenaming && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip.c_str());

        if (!isRenaming)
        {
            // Select on click; double-click begins an inline rename (the root included — it's the
            // saved subtree's own node).
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                selectEntity(entity);
                if (thisPart >= 0) selectPart(thisPart);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                renamingEntity = entity;
                renameFocusPending = true;
                snprintf(renameBuf, sizeof(renameBuf), "%s", data->name.c_str());
            }

            // Drag source (right after TreeNodeEx, before any SameLine widgets). The sandbox root is
            // not draggable (it has no parent to reparent under).
            if (!isRoot && ImGui::BeginDragDropSource())
            {
                services::EntityHandle payload = entity;
                ImGui::SetDragDropPayload(kPrefabSceneEntityPayload, &payload, sizeof(services::EntityHandle));
                ImGui::Text("Move %s", data->name.empty() ? "(unnamed)" : data->name.c_str());
                ImGui::EndDragDropSource();
            }

            // Drop target: reparent the dragged node under this one (guarded against cycles).
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPrefabSceneEntityPayload))
                {
                    const services::EntityHandle dragged =
                        *static_cast<const services::EntityHandle*>(payload->Data);
                    if (dragged.isValid() && dragged != entity && !isAncestorOrSelf(dragged, entity))
                    {
                        events::scene::ReparentEntityCommand cmd;
                        cmd.entity = dragged;
                        cmd.newParent = entity; // handler also enforces its own cycle-check
                        events::EventDispatcher::instance().execute(cmd);
                        dirty = true;
                        rebuildRigFromSandbox();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // VK-1433 Phase 4d — eye toggle (editor-only preview hide), right-aligned and ALWAYS
            // visible. Toggles the hiddenEntities SET ONLY; isActive is never touched, so a hidden node
            // stays active in the real game. The icon is clearly colored (a bright eye when visible, a
            // dimmed orange eye-slash when hidden) instead of a transparent button so the preview-hide
            // state reads at a glance; the row label is also dimmed for hidden nodes (above).
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, isHidden ? ImVec4(0.95f, 0.6f, 0.2f, 1.0f)
                                                          : ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
            if (ImGui::SmallButton(isHidden ? ICON_FA_EYE_SLASH "##hide" : ICON_FA_EYE "##hide"))
            {
                if (isHidden) hiddenEntities.erase(entity.id);
                else          hiddenEntities.insert(entity.id);
                rebuildRigFromSandbox(); // re-derive: hidden subtrees are pruned from the DTO
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(isHidden ? "Hidden in preview — click to show (saved Active state unchanged)"
                                           : "Preview-only hide (does not change the saved Active state)");

            // Context menu: add child / delete (never the sandbox root — that's the saved subtree's
            // top). Shares createChildEntity / deleteEntity with the header toolbar + Delete key.
            if (!isRoot && ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Add Child"))
                {
                    createChildEntity(entity);
                }
                if (ImGui::MenuItem("Delete"))
                {
                    // Phase 4d — defer to the confirm modal. The entity still exists this frame (the
                    // actual delete runs on confirm), so the old abort-recursion early-return is gone.
                    requestDeleteEntity(entity);
                }
                ImGui::EndPopup();
            }
        }
        else
        {
            // Inline rename editor (replaces the node's interactions for this frame).
            ImGui::SameLine();
            if (renameFocusPending)
            {
                ImGui::SetKeyboardFocusHere();
                renameFocusPending = false;
            }
            ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 10.0f, 80.0f));
            const bool committed = ImGui::InputText("##rename", renameBuf, sizeof(renameBuf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue
                                                        | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                renamingEntity = services::EntityHandle::invalid();
            }
            else if (committed || ImGui::IsItemDeactivated())
            {
                if (renameBuf[0] != '\0' && renameBuf != data->name)
                {
                    // O2: a socket attachment resolves its parent BY NAME (subtree-scoped), so
                    // renaming an entity that is some child's attach-parent drops that child's link to
                    // root on the next re-derive. WARN (non-destructive, no cascade) and proceed.
                    if (renameWouldOrphanAttachment(data->name))
                    {
                        vfLogWarning("Prefab preview: renaming '{}' breaks a socket attachment that "
                                     "resolves to it by name; the attached child will fall back to "
                                     "the root until you re-point its socket parent.", data->name);
                    }
                    events::scene::SetEntityNameCommand cmd;
                    cmd.entity = entity;
                    cmd.newName = renameBuf;
                    events::EventDispatcher::instance().execute(cmd);
                    dirty = true;
                    rebuildRigFromSandbox();
                }
                renamingEntity = services::EntityHandle::invalid();
            }
        }

        if (nodeOpen && hasChildren)
        {
            const bool dragActive = isPrefabEntityDragActive();
            const auto& children = data->children;
            for (size_t i = 0; i < children.size(); ++i)
            {
                if (dragActive) drawReorderDropZone(entity, i);
                drawEntityNode(children[i], depth + 1);
            }
            if (dragActive) drawReorderDropZone(entity, children.size());
            ImGui::TreePop();
        }
        else if (nodeOpen)
        {
            // Leaf nodes still TreePush unless NoTreePushOnOpen; we did NOT set that flag (so DnD +
            // SameLine widgets attach correctly), so balance the push here.
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    bool PrefabPreviewWindow::renameWouldOrphanAttachment(const std::string& oldName) const
    {
        if (oldName.empty()) return false;
        // Scan the live parts' source entities for a socket attachment whose parentEntityName equals
        // the about-to-be-renamed name. If one exists, that link resolves by name and will drop.
        for (const services::EntityHandle& part : partEntities)
        {
            events::socket::GetSocketAttachmentDataQuery sq;
            sq.entity = part;
            auto attach = events::EventDispatcher::instance().query(sq);
            if (attach.has_value() && attach->parentEntityName == oldName)
                return true;
        }
        return false;
    }

    std::vector<uint64_t> PrefabPreviewWindow::sandboxStructureSignature() const
    {
        // DFS over the visible sandbox subtree, emitting each entity id followed by its component
        // type-id list (a separator sentinel between entities). A change (component add/remove via the
        // embedded inspector, a create/delete) shifts this vector, which the caller uses to re-derive
        // the rig without coupling to any specific Add/Remove command.
        std::vector<uint64_t> sig;
        if (!sandboxRoot.isValid()) return sig;

        std::function<void(services::EntityHandle)> walk = [&](services::EntityHandle e)
        {
            if (!e.isValid()) return;
            events::scene::GetEntityQuery q;
            q.entity = e;
            auto data = events::EventDispatcher::instance().query(q);
            if (!data.has_value()) return;
            sig.push_back(e.id);
            for (services::ComponentTypeId c : data->components)
                sig.push_back(static_cast<uint64_t>(c));
            sig.push_back(~0ull); // entity separator sentinel
            for (const services::EntityHandle& child : data->children)
                walk(child);
        };
        walk(sandboxRoot);
        return sig;
    }

    void PrefabPreviewWindow::drawEntityInspector()
    {
        const services::EntityHandle sel = selectedEntity();
        if (!sel.isValid())
        {
            ImGui::TextDisabled("Select an entity in the hierarchy.");
            return;
        }

        // Embedded shared inspector: full per-component edit + Remove + Add Component (O1). A
        // structural component change (add/remove) shifts the structure signature, which we detect
        // on the next frame to re-derive the rig.
        entityInspector.drawComponentSection(sel);
    }

    // ----------------------------------------------------------------------
    // Authoring
    // ----------------------------------------------------------------------
    bool PrefabPreviewWindow::partIsSkeletal(int part) const
    {
        if (part < 0 || part >= static_cast<int>(rigDesc.parts.size())) return false;
        return !rigDesc.parts[part].animatorPath.empty();
    }

    const std::string& PrefabPreviewWindow::partMeshPath(int part) const
    {
        static const std::string empty;
        if (part < 0 || part >= static_cast<int>(rigDesc.parts.size())) return empty;
        return rigDesc.parts[part].meshPath;
    }

    void PrefabPreviewWindow::pullEditSocketsForPart(int part)
    {
        editSockets.clear();
        selectedSocketIndex = -1;
        bonePickArmed = false; // an armed pick targets the previously-selected part; clear on switch
        if (part < 0) return;

        services::events::prefabrigpreview::GetPrefabRigSocketsQuery query;
        query.instanceId = getInstanceId();
        query.part = static_cast<size_t>(part);
        editSockets = events::EventDispatcher::instance().query(query);
    }

    void PrefabPreviewWindow::pushEditSocketsForPart(int part)
    {
        if (part < 0) return;
        services::events::prefabrigpreview::SetPrefabRigSocketsCommand cmd;
        cmd.instanceId = getInstanceId();
        cmd.part = static_cast<size_t>(part);
        cmd.sockets = editSockets;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PrefabPreviewWindow::pullEditChains()
    {
        services::events::prefabrigpreview::GetPrefabRigChainsQuery query;
        query.instanceId = getInstanceId();
        editChains = events::EventDispatcher::instance().query(query);
    }

    void PrefabPreviewWindow::pushEditChains()
    {
        services::events::prefabrigpreview::SetPrefabRigChainsCommand cmd;
        cmd.instanceId = getInstanceId();
        cmd.chains = editChains;
        events::EventDispatcher::instance().execute(cmd);
    }

    // ----------------------------------------------------------------------
    // VK-1433 Phase 4 — selection link (hierarchy <-> part-indexed panels)
    // ----------------------------------------------------------------------
    services::EntityHandle PrefabPreviewWindow::selectedEntity() const
    {
        events::scene::GetSelectedEntityQuery q;
        auto sel = events::EventDispatcher::instance().query(q);
        return sel.value_or(services::EntityHandle::invalid());
    }

    void PrefabPreviewWindow::selectEntity(services::EntityHandle entity)
    {
        events::scene::SelectEntityCommand cmd;
        if (entity.isValid()) cmd.entity = entity;
        events::EventDispatcher::instance().execute(cmd);
    }

    int PrefabPreviewWindow::partForEntity(services::EntityHandle entity) const
    {
        if (!entity.isValid()) return -1;
        for (size_t i = 0; i < partEntities.size(); ++i)
        {
            if (partEntities[i] == entity)
                return static_cast<int>(i);
        }
        return -1;
    }

    void PrefabPreviewWindow::selectPart(int part)
    {
        if (part < 0 || part >= static_cast<int>(rigDesc.parts.size()))
        {
            selectedPart = -1;
            pullEditSocketsForPart(-1);
            return;
        }
        selectedPart = part;
        pullEditSocketsForPart(selectedPart);
    }

    services::TransformData PrefabPreviewWindow::partSourceLocalTransform(int part) const
    {
        services::TransformData t; // identity defaults
        if (part < 0 || part >= static_cast<int>(partEntities.size())) return t;
        events::scene::GetEntityQuery q;
        q.entity = partEntities[part];
        auto data = events::EventDispatcher::instance().query(q);
        if (data.has_value()) return data->localTransform;
        return t;
    }

    void PrefabPreviewWindow::drawAuthoringPanel()
    {
        if (!previewBuilt)
        {
            ImGui::TextDisabled("Authoring available once the rig is built.");
            return;
        }

        // Part picker (drives socket panels).
        ImGui::TextDisabled("Part");
        const char* preview = (selectedPart >= 0 && selectedPart < static_cast<int>(rigDesc.parts.size()))
                                  ? rigDesc.parts[selectedPart].meshPath.c_str()
                                  : "Select part...";
        if (ImGui::BeginCombo("##part", preview))
        {
            for (int p = 0; p < static_cast<int>(rigDesc.parts.size()); ++p)
            {
                const bool partMissing = (p < static_cast<int>(partRefStatuses.size())) &&
                                         partRefStatuses[p].anyMissing();
                std::string label = std::to_string(p) + ": " +
                    std::filesystem::path(rigDesc.parts[p].meshPath).filename().string() +
                    (partIsSkeletal(p) ? " [skel]" : " [static]") +
                    (partMissing ? " (!)" : "");
                bool selected = (selectedPart == p);
                if (partMissing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    // Part combo -> select the part AND its source entity (keeps the hierarchy tree
                    // selection in sync with the authoring panels). selectPart pulls the sockets.
                    selectPart(p);
                    if (p >= 0 && p < static_cast<int>(partEntities.size()))
                        selectEntity(partEntities[p]);
                }
                if (partMissing)
                {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", missingRefTooltip(p).c_str());
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // VK-1433 Phase 4c — drag a .vfMesh / .vfMaterial / .vfAnim onto the part combo to swap that
        // ref on the selected part's SOURCE ENTITY (persistent — Save Prefab writes it). The rig
        // re-derives from the mutated entity.
        if (selectedPart >= 0)
        {
            if (auto dropped = acceptAssetDropOnLastItem("##partDrop", {".vfMesh", ".vfMaterial", ".vfAnim"}))
            {
                applyAssetDropToPart(selectedPart, *dropped);
            }
        }

        ImGui::Separator();

        // VK-1433 gizmo-mode toolbar — exactly one viewport gizmo is active (never fight).
        drawGizmoModeToolbar();

        ImGui::Separator();

        if (ImGui::BeginTabBar("##authoringTabs"))
        {
            if (ImGui::BeginTabItem("Transform"))
            {
                if (ImGui::IsItemActivated()) gizmoMode = GizmoMode::Transform;
                drawTransformPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("State"))
            {
                drawStatePicker();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Bone Socket"))
            {
                if (ImGui::IsItemActivated()) gizmoMode = GizmoMode::BoneSocket;
                drawBoneSocketPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Static Socket"))
            {
                if (ImGui::IsItemActivated()) gizmoMode = GizmoMode::StaticSocket;
                drawStaticSocketPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("IK"))
            {
                if (ImGui::IsItemActivated()) gizmoMode = GizmoMode::IK;
                drawIKPanel();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void PrefabPreviewWindow::drawGizmoModeToolbar()
    {
        ImGui::TextDisabled("Gizmo:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Transform", gizmoMode == GizmoMode::Transform))
            gizmoMode = GizmoMode::Transform;
        ImGui::SameLine();
        if (ImGui::RadioButton("Bone##gm", gizmoMode == GizmoMode::BoneSocket))
            gizmoMode = GizmoMode::BoneSocket;
        ImGui::SameLine();
        if (ImGui::RadioButton("Static##gm", gizmoMode == GizmoMode::StaticSocket))
            gizmoMode = GizmoMode::StaticSocket;
        ImGui::SameLine();
        if (ImGui::RadioButton("IK##gm", gizmoMode == GizmoMode::IK))
            gizmoMode = GizmoMode::IK;
    }

    void PrefabPreviewWindow::drawTransformPanel()
    {
        if (selectedPart < 0)
        {
            ImGui::TextDisabled("Select a part to transform.");
            return;
        }

        const bool isRoot = (selectedPart >= 0 && selectedPart < static_cast<int>(rigDesc.parts.size()))
                                ? rigDesc.parts[selectedPart].parentPartIndex < 0
                                : false;
        if (isRoot)
            ImGui::TextDisabled("Root part — moves the whole rig.");
        else
            ImGui::TextDisabled("Child part — moves this part (children follow).");

        // VK-1433 Phase 4c — the viewport gizmo edits the part's SOURCE ENTITY transform directly
        // (SetTransformCommand), persisted by Save Prefab. Phase 4d — the duplicate numeric fields are
        // retired; edit exact numbers in Entity Inspector → Transform (the embedded inspector below).
        ImGui::TextWrapped("Drag the 3D gizmo to move this part — Save Prefab persists it. For exact "
                           "numbers, use Entity Inspector \xE2\x86\x92 Transform.");

        // Socket-attached-child TRANSLATE-drop warning. SocketAttachmentUpdater::applyModelOffset
        // builds entityLocal = rot*scale, DROPPING translation — so a non-zero source position on a
        // socketed child will NOT reproduce at instantiation (rotation/scale will). Warn loudly and
        // offer a one-click bake-to-zero (writes the entity transform position to 0).
        if (!isRoot)
        {
            const int parentPart = (selectedPart < static_cast<int>(rigDesc.parts.size()))
                                       ? rigDesc.parts[selectedPart].parentPartIndex : -1;
            const glm::vec3 srcPos = partSourceLocalTransform(selectedPart).position;
            if (prefabrigval::childHasDroppedTranslation(parentPart, srcPos))
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "Warning: socket-attached child translation (%.3f, %.3f, %.3f) is "
                                   "dropped at instantiation.", srcPos.x, srcPos.y, srcPos.z);
                ImGui::TextWrapped("Socketed children ride the socket origin; only rotation/scale "
                                   "carry. Bake the translation to zero so the prefab matches runtime.");
                if (ImGui::Button("Zero translation"))
                {
                    zeroSourceTranslationForPart(selectedPart);
                }
            }
        }

        // The Move/Rotate/Scale toggle drives the viewport gizmo's operation (transformGizmoOp, read by
        // drawTransformGizmo). VK-1433 Phase 4d — the duplicate numeric Position/Rotation/Scale block is
        // removed; numeric editing lives in Entity Inspector → Transform (which dispatches the same
        // SetTransformCommand on this entity). Keeping only the gizmo-operation toggle here.
        ImGui::Spacing();
        ImGui::TextDisabled("Gizmo operation");
        if (ImGui::RadioButton("Move##tr", transformGizmoOp == ImGuizmo::TRANSLATE))
            transformGizmoOp = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate##tr", transformGizmoOp == ImGuizmo::ROTATE))
            transformGizmoOp = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale##tr", transformGizmoOp == ImGuizmo::SCALE))
            transformGizmoOp = ImGuizmo::SCALE;
    }

    void PrefabPreviewWindow::zeroSourceTranslationForPart(int part)
    {
        // VK-1433 Phase 4c — zero the part's source ENTITY position (SetTransformCommand) and
        // re-derive the rig so the preview matches and the warning clears. The change is persisted by
        // Save Prefab (the entity is the source of truth — the old JSON round-trip is retired).
        if (part < 0 || part >= static_cast<int>(partEntities.size())) return;

        const services::TransformData before = partSourceLocalTransform(part);
        services::TransformData t = before;
        t.position = glm::vec3(0.0f);

        events::scene::SetTransformCommand cmd;
        cmd.entity = partEntities[part];
        cmd.transform = t;
        events::EventDispatcher::instance().execute(cmd);

        dirty = true;
        rebuildRigFromSandbox();

        // VK-1433 Phase 4d — make the bake undoable too (same entity-transform replay path as the
        // Transform gizmo). No-op-gated, so a part already at zero translation pushes nothing.
        pushTransformUndo(partEntities[part], before, t);

        vfLogInfo("Prefab preview: zeroed part {} source entity translation", part);
    }

    void PrefabPreviewWindow::drawStatePicker()
    {
        // Play / pause.
        services::events::prefabrigpreview::IsPrefabRigPausedQuery pausedQuery;
        pausedQuery.instanceId = getInstanceId();
        bool paused = events::EventDispatcher::instance().query(pausedQuery);

        if (ImGui::Button(paused ? "Play" : "Pause"))
        {
            if (paused)
            {
                services::events::prefabrigpreview::PlayPrefabRigCommand cmd;
                cmd.instanceId = getInstanceId();
                events::EventDispatcher::instance().execute(cmd);
            }
            else
            {
                services::events::prefabrigpreview::PausePrefabRigCommand cmd;
                cmd.instanceId = getInstanceId();
                events::EventDispatcher::instance().execute(cmd);
            }
        }

        // VK-1433 frame-by-frame scrub (needs a skeletal part selected).
        drawFrameScrub();

        ImGui::Separator();

        if (selectedPart < 0)
        {
            ImGui::TextDisabled("Select a part.");
            return;
        }
        if (!partIsSkeletal(selectedPart))
        {
            ImGui::TextDisabled("Static part — no animator states.");
            return;
        }

        services::events::prefabrigpreview::GetPrefabRigStatesQuery statesQuery;
        statesQuery.instanceId = getInstanceId();
        statesQuery.part = static_cast<size_t>(selectedPart);
        auto states = events::EventDispatcher::instance().query(statesQuery);

        if (states.empty())
        {
            ImGui::TextDisabled("No states.");
        }
        else
        {
            // De-hardcoded transition blend (was a literal 0.25f); the user can tune it per session.
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("Blend (s)", &stateBlendDuration, 0.0f, 1.0f, "%.2f");

            ImGui::TextDisabled("States");
            for (const auto& state : states)
            {
                if (ImGui::Button(state.name.c_str(), ImVec2(-1, 0)))
                {
                    services::events::prefabrigpreview::SetPrefabRigStateCommand cmd;
                    cmd.instanceId = getInstanceId();
                    cmd.part = static_cast<size_t>(selectedPart);
                    cmd.stateName = state.name;
                    cmd.blendDuration = stateBlendDuration;
                    events::EventDispatcher::instance().execute(cmd);
                }
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Parameters");

        // Lightweight named-parameter drivers (the common idle/run/fire knobs). These are
        // generic by name; the user types the parameter the animator graph expects.
        static char paramName[64] = "";
        ImGui::InputText("Name##param", paramName, sizeof(paramName));
        static float floatVal = 0.0f;
        static bool boolVal = false;
        static int intVal = 0;

        if (ImGui::Button("Set Bool"))
        {
            services::events::prefabrigpreview::SetPrefabRigBoolCommand cmd;
            cmd.instanceId = getInstanceId();
            cmd.part = static_cast<size_t>(selectedPart);
            cmd.name = paramName;
            cmd.value = boolVal;
            events::EventDispatcher::instance().execute(cmd);
        }
        ImGui::SameLine();
        ImGui::Checkbox("##boolVal", &boolVal);

        if (ImGui::Button("Set Float"))
        {
            services::events::prefabrigpreview::SetPrefabRigFloatCommand cmd;
            cmd.instanceId = getInstanceId();
            cmd.part = static_cast<size_t>(selectedPart);
            cmd.name = paramName;
            cmd.value = floatVal;
            events::EventDispatcher::instance().execute(cmd);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat("##floatVal", &floatVal, 0.01f);

        if (ImGui::Button("Set Int"))
        {
            services::events::prefabrigpreview::SetPrefabRigIntCommand cmd;
            cmd.instanceId = getInstanceId();
            cmd.part = static_cast<size_t>(selectedPart);
            cmd.name = paramName;
            cmd.value = intVal;
            events::EventDispatcher::instance().execute(cmd);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragInt("##intVal", &intVal);

        if (ImGui::Button("Trigger"))
        {
            services::events::prefabrigpreview::SetPrefabRigTriggerCommand cmd;
            cmd.instanceId = getInstanceId();
            cmd.part = static_cast<size_t>(selectedPart);
            cmd.name = paramName;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void PrefabPreviewWindow::drawFrameScrub()
    {
        if (selectedPart < 0 || !partIsSkeletal(selectedPart))
            return; // scrub applies to a skeletal part's animator

        ImGui::Spacing();
        ImGui::TextDisabled("Frame scrub (part %d)", selectedPart);

        // Step ±1 frame even while paused (stepFrame advances the layer stack by one source frame).
        if (ImGui::Button("|< Prev"))
        {
            services::events::prefabrigpreview::StepPrefabRigFrameCommand cmd;
            cmd.instanceId = getInstanceId();
            cmd.part = static_cast<size_t>(selectedPart);
            cmd.frames = -1;
            events::EventDispatcher::instance().execute(cmd);
        }
        ImGui::SameLine();
        if (ImGui::Button("Next >|"))
        {
            services::events::prefabrigpreview::StepPrefabRigFrameCommand cmd;
            cmd.instanceId = getInstanceId();
            cmd.part = static_cast<size_t>(selectedPart);
            cmd.frames = 1;
            events::EventDispatcher::instance().execute(cmd);
        }

        // Absolute scrub slider: read the current normalized time, seek on edit.
        services::events::prefabrigpreview::GetPrefabRigNormalizedTimeQuery q;
        q.instanceId = getInstanceId();
        q.part = static_cast<size_t>(selectedPart);
        float normalized = events::EventDispatcher::instance().query(q);

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##prefabScrub", &normalized, 0.0f, 1.0f, "t = %.3f"))
        {
            services::events::prefabrigpreview::SetPrefabRigNormalizedTimeCommand cmd;
            cmd.instanceId = getInstanceId();
            cmd.part = static_cast<size_t>(selectedPart);
            cmd.t = normalized;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void PrefabPreviewWindow::drawBoneSocketPanel()
    {
        if (selectedPart < 0 || !partIsSkeletal(selectedPart))
        {
            ImGui::TextDisabled("Select a skeletal part.");
            return;
        }

        // Undo bracket: snapshot the sockets BEFORE the controls run while no edit session is in
        // flight; the matching push at the end of the panel coalesces a completed drag into ONE entry.
        if (!socketEditActive)
            socketEditBefore = snapshotSockets();

        // VK-1433 Phase 1b — bone-pick socket creation. Arm, then click a skeleton joint in the
        // viewport to prefill a new bone socket on the picked bone (identity offset; fine-tune below
        // or with the existing fields, then "Save Sockets to Mesh"). Requires the Skeleton overlay so
        // the user can see the joints they are clicking.
        if (!environment.showSkeleton)
        {
            bonePickArmed = false; // can't aim at joints that aren't drawn
            ImGui::BeginDisabled();
            ImGui::Button("Pick bone (click joint)");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(enable the Skeleton overlay)");
        }
        else if (!bonePickArmed)
        {
            if (ImGui::Button("Pick bone (click joint)"))
                bonePickArmed = true;
        }
        else
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Click a joint in the viewport...");
            ImGui::SameLine();
            if (ImGui::Button("Cancel##bonePick"))
                bonePickArmed = false;
        }
        ImGui::Separator();

        ImGui::TextDisabled("Bone sockets (%zu)", editSockets.size());
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(editSockets.size()); ++i)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selectedSocketIndex == i) flags |= ImGuiTreeNodeFlags_Selected;
            bool open = ImGui::TreeNodeEx(editSockets[i].name.c_str(), flags);
            if (ImGui::IsItemClicked()) selectedSocketIndex = i;
            if (open) ImGui::TreePop();
        }

        if (selectedSocketIndex >= 0 && selectedSocketIndex < static_cast<int>(editSockets.size()))
        {
            auto& socket = editSockets[selectedSocketIndex];
            ImGui::Separator();
            ImGui::Text("Target bone: %s", socket.targetBoneName.c_str());

            float pos[3] = {socket.localPosition.x, socket.localPosition.y, socket.localPosition.z};
            if (ImGui::DragFloat3("Local Position##bone", pos, 0.01f))
            {
                socket.localPosition = glm::vec3(pos[0], pos[1], pos[2]);
                pushEditSocketsForPart(selectedPart);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("0##bonePos"))
            {
                socket.localPosition = glm::vec3(0.0f); // a zero-offset socket lands exactly on its joint
                pushEditSocketsForPart(selectedPart);
            }
            glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(socket.localRotation));
            float rot[3] = {eulerDeg.x, eulerDeg.y, eulerDeg.z};
            if (ImGui::DragFloat3("Rotation##bone", rot, 0.5f))
            {
                socket.localRotation = glm::quat(glm::radians(glm::vec3(rot[0], rot[1], rot[2])));
                pushEditSocketsForPart(selectedPart);
            }
        }

        ImGui::Separator();
        if (socketSaveTimer > 0.0f) socketSaveTimer -= ImGui::GetIO().DeltaTime;
        bool canSave = !partMeshPath(selectedPart).empty();
        if (!canSave) ImGui::BeginDisabled();
        if (ImGui::Button("Save Sockets to Mesh"))
        {
            socketSaveSuccess =
                types::MeshSocketWriter::saveSocketsToMesh(partMeshPath(selectedPart), editSockets);
            socketSaveTimer = kSaveFeedbackSeconds;
            if (socketSaveSuccess)
            {
                events::socket::SocketDataSavedNotification notif;
                notif.meshPath = partMeshPath(selectedPart);
                events::EventDispatcher::instance().publish(notif);
            }
        }
        if (!canSave) ImGui::EndDisabled();
        if (socketSaveTimer > 0.0f)
        {
            ImGui::SameLine();
            ImGui::TextColored(socketSaveSuccess ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.3f, 0.3f, 1),
                               socketSaveSuccess ? "Saved" : "Failed");
        }

        // Close the undo bracket: an edit session is "in flight" while any item is active; when it
        // ends, push one coalesced entry against the snapshot taken at session start.
        const bool anyItemActive = ImGui::IsAnyItemActive();
        if (anyItemActive)
        {
            socketEditActive = true;
        }
        else if (socketEditActive)
        {
            socketEditActive = false;
            pushSocketUndo(std::move(socketEditBefore));
        }
    }

    void PrefabPreviewWindow::drawStaticSocketPanel()
    {
        if (selectedPart < 0 || partIsSkeletal(selectedPart))
        {
            ImGui::TextDisabled("Select a static part (e.g. weapon).");
            return;
        }

        // Undo bracket (see drawBoneSocketPanel): snapshot before edits while no session active.
        if (!socketEditActive)
            socketEditBefore = snapshotSockets();

        // Create.
        static char newName[128] = "";
        ImGui::InputText("Name##newStatic", newName, sizeof(newName));
        ImGui::SameLine();
        if (ImGui::Button("Add") && std::strlen(newName) > 0)
        {
            animator::SocketDefinition s;
            s.name = newName;
            editSockets.push_back(s);
            selectedSocketIndex = static_cast<int>(editSockets.size()) - 1;
            newName[0] = '\0';
            pushEditSocketsForPart(selectedPart);
        }

        ImGui::TextDisabled("Static sockets (%zu)", editSockets.size());
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(editSockets.size()); ++i)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selectedSocketIndex == i) flags |= ImGuiTreeNodeFlags_Selected;
            bool open = ImGui::TreeNodeEx(editSockets[i].name.c_str(), flags);
            if (ImGui::IsItemClicked()) selectedSocketIndex = i;
            if (open) ImGui::TreePop();
        }

        if (selectedSocketIndex >= 0 && selectedSocketIndex < static_cast<int>(editSockets.size()))
        {
            auto& socket = editSockets[selectedSocketIndex];
            ImGui::Separator();

            float pos[3] = {socket.localPosition.x, socket.localPosition.y, socket.localPosition.z};
            if (ImGui::DragFloat3("Local Position##static", pos, 0.01f))
            {
                socket.localPosition = glm::vec3(pos[0], pos[1], pos[2]);
                pushEditSocketsForPart(selectedPart);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("0##staticPos"))
            {
                socket.localPosition = glm::vec3(0.0f); // a zero-offset socket lands at the part origin
                pushEditSocketsForPart(selectedPart);
            }
            glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(socket.localRotation));
            float rot[3] = {eulerDeg.x, eulerDeg.y, eulerDeg.z};
            if (ImGui::DragFloat3("Rotation##static", rot, 0.5f))
            {
                socket.localRotation = glm::quat(glm::radians(glm::vec3(rot[0], rot[1], rot[2])));
                pushEditSocketsForPart(selectedPart);
            }

            ImGui::TextDisabled("Gizmo:");
            ImGui::SameLine();
            if (ImGui::RadioButton("Move", socketGizmoOp == ImGuizmo::TRANSLATE))
                socketGizmoOp = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", socketGizmoOp == ImGuizmo::ROTATE))
                socketGizmoOp = ImGuizmo::ROTATE;

            if (ImGui::Button("Delete Socket"))
            {
                editSockets.erase(editSockets.begin() + selectedSocketIndex);
                selectedSocketIndex = -1;
                pushEditSocketsForPart(selectedPart);
            }
        }

        ImGui::Separator();
        if (socketSaveTimer > 0.0f) socketSaveTimer -= ImGui::GetIO().DeltaTime;
        bool canSave = !partMeshPath(selectedPart).empty() && !editSockets.empty();
        if (!canSave) ImGui::BeginDisabled();
        if (ImGui::Button("Save Sockets to Mesh##static"))
        {
            socketSaveSuccess =
                types::MeshSocketWriter::saveSocketsToMesh(partMeshPath(selectedPart), editSockets);
            socketSaveTimer = kSaveFeedbackSeconds;
            if (socketSaveSuccess)
            {
                events::socket::SocketDataSavedNotification notif;
                notif.meshPath = partMeshPath(selectedPart);
                events::EventDispatcher::instance().publish(notif);
            }
        }
        if (!canSave) ImGui::EndDisabled();
        if (socketSaveTimer > 0.0f)
        {
            ImGui::SameLine();
            ImGui::TextColored(socketSaveSuccess ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.3f, 0.3f, 1),
                               socketSaveSuccess ? "Saved" : "Failed");
        }

        // Close the undo bracket (see drawBoneSocketPanel).
        const bool anyItemActive = ImGui::IsAnyItemActive();
        if (anyItemActive)
        {
            socketEditActive = true;
        }
        else if (socketEditActive)
        {
            socketEditActive = false;
            pushSocketUndo(std::move(socketEditBefore));
        }
    }

    void PrefabPreviewWindow::drawIKPanel()
    {
        if (!chainsLoaded)
        {
            // Pull the controller's chain copies once (so an empty prefab doesn't re-query
            // every frame). Subsequent edits stay in editChains and are pushed back.
            pullEditChains();
            chainsLoaded = true;
        }

        if (editChains.empty())
        {
            ImGui::TextDisabled("No IK chains in this prefab.");
            return;
        }

        // Undo bracket: snapshot the chains BEFORE the controls run while no edit session is active.
        if (!chainEditActive)
            chainEditBefore = snapshotChains();

        bool configChanged = false;  // weight/enabled — cheap live push, no rebuild
        bool bindingChanged = false; // target part/socket — needs a re-resolve (rebuild)

        int clickedChain = -1;       // per-chain selection (wires the dead selectedChainIndex)

        for (int i = 0; i < static_cast<int>(editChains.size()); ++i)
        {
            auto& chain = editChains[i];
            ImGui::PushID(i);

            // Per-chain selection: clicking a chain header makes it the active chain. The detail
            // editor (weight/enabled/binding) only shows for the selected chain, so a multi-chain
            // rig is no longer an undifferentiated wall of controls.
            ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_DefaultOpen;
            if (i == selectedChainIndex) treeFlags |= ImGuiTreeNodeFlags_Selected;
            const bool chainOpen = ImGui::TreeNodeEx(chain.chainName.c_str(), treeFlags);
            if (ImGui::IsItemClicked()) clickedChain = i;

            if (chainOpen)
            {
                ImGui::Text("Tip: %s", chain.tipBoneName.c_str());

                const bool isSelectedChain = (i == selectedChainIndex);
                if (!isSelectedChain)
                {
                    ImGui::TextDisabled("(click to edit this chain)");
                }
                else
                {
                if (ImGui::SliderFloat("Weight", &chain.weight, 0.0f, 1.0f, "%.2f")) configChanged = true;
                if (ImGui::Checkbox("Enabled", &chain.enabled)) configChanged = true;

                if (ImGui::TreeNode("Chain Bones"))
                {
                    for (size_t b = 0; b < chain.chainBoneNames.size(); ++b)
                        ImGui::BulletText("%s", chain.chainBoneNames[b].c_str());
                    ImGui::TreePop();
                }

                // Editor-transient target binding override (matches rigDesc.ik[i]).
                if (i < static_cast<int>(rigDesc.ik.size()))
                {
                    auto& ik = rigDesc.ik[i];
                    ImGui::Separator();
                    ImGui::TextDisabled("Target binding (transient)");

                    const char* partPreview = (ik.targetPartIndex >= 0 &&
                                               ik.targetPartIndex < static_cast<int>(rigDesc.parts.size()))
                        ? rigDesc.parts[ik.targetPartIndex].meshPath.c_str()
                        : "None";
                    if (ImGui::BeginCombo("Target part", partPreview))
                    {
                        for (int p = 0; p < static_cast<int>(rigDesc.parts.size()); ++p)
                        {
                            if (p == ik.bodyPartIndex) continue;
                            std::string label = std::filesystem::path(rigDesc.parts[p].meshPath).filename().string();
                            if (ImGui::Selectable(label.c_str(), ik.targetPartIndex == p))
                            {
                                ik.targetPartIndex = p;
                                ik.targetSocketName.clear();
                                bindingChanged = true;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    // Target socket dropdown: the target part's static sockets.
                    if (ik.targetPartIndex >= 0)
                    {
                        services::events::prefabrigpreview::GetPrefabRigSocketsQuery q;
                        q.instanceId = getInstanceId();
                        q.part = static_cast<size_t>(ik.targetPartIndex);
                        auto targetSockets = events::EventDispatcher::instance().query(q);

                        const char* sockPreview = ik.targetSocketName.empty() ? "Select socket..."
                                                                              : ik.targetSocketName.c_str();
                        if (ImGui::BeginCombo("Target socket", sockPreview))
                        {
                            for (const auto& s : targetSockets)
                            {
                                if (ImGui::Selectable(s.name.c_str(), s.name == ik.targetSocketName))
                                {
                                    ik.targetSocketName = s.name;
                                    bindingChanged = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                }
                } // end: selected-chain detail editor

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        // Apply a per-chain selection click after the loop (so it survives this frame's tree state).
        if (clickedChain >= 0)
            selectedChainIndex = clickedChain;

        // Config edits (weight/enabled) flow live to editableChains() without a rebuild.
        if (configChanged)
        {
            pushEditChains();
        }

        // A target-binding change must re-resolve the assembly. Rebuilding reloads sockets from
        // disk, so first push the current chain configs AND re-apply any in-memory socket edits
        // for the selected part, then rebuild, so nothing the user already tweaked is lost.
        if (bindingChanged)
        {
            buildPreviewFromDesc();      // re-assembles with the new rigDesc.ik bindings
            pushEditChains();            // restore live chain configs onto the fresh assembly
            if (selectedPart >= 0 && !editSockets.empty())
            {
                pushEditSocketsForPart(selectedPart);
            }
            // A binding combo click is a single-frame event: by the next frame IsAnyItemActive()
            // may already be false, so the bracket below could miss it. Engage the chains-undo
            // session explicitly so the next frame's "active -> inactive" edge pushes the undo
            // deterministically (chainEditBefore was captured at the top of this panel).
            chainEditActive = true;
        }

        ImGui::Separator();
        if (ikSaveTimer > 0.0f) ikSaveTimer -= ImGui::GetIO().DeltaTime;

        // Save chain CONFIG to the skeletal (body) part's mesh. The per-frame target stays transient.
        int bodyPart = (!rigDesc.ik.empty()) ? rigDesc.ik.front().bodyPartIndex : -1;
        bool canSave = bodyPart >= 0 && !partMeshPath(bodyPart).empty();
        if (!canSave) ImGui::BeginDisabled();
        if (ImGui::Button("Save IK Chains to Mesh"))
        {
            ikSaveSuccess = types::MeshIKChainWriter::saveIKChainsToMesh(partMeshPath(bodyPart), editChains);
            ikSaveTimer = kSaveFeedbackSeconds;
        }
        if (!canSave) ImGui::EndDisabled();
        if (ikSaveTimer > 0.0f)
        {
            ImGui::SameLine();
            ImGui::TextColored(ikSaveSuccess ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.3f, 0.3f, 1),
                               ikSaveSuccess ? "Saved" : "Failed");
        }

        // Close the chains undo bracket: coalesce a completed weight/enabled edit into one entry.
        // (A target-binding change rebuilds the assembly; the chains-undo re-applies sockets to keep
        // the rebuild's disk reload from stranding in-memory socket edits — see pushChainUndo.)
        const bool anyItemActive = ImGui::IsAnyItemActive();
        if (anyItemActive)
        {
            chainEditActive = true;
        }
        else if (chainEditActive)
        {
            chainEditActive = false;
            pushChainUndo(std::move(chainEditBefore));
        }
    }
}
