#include "print/Log.hpp"
#include "PrefabPreviewWindow.hpp"
#include "PrefabTransformWriter.hpp"
#include "PrefabRefWriter.hpp"
#include "asset/AssetRef.hpp"
#include "PreviewInputHandler.hpp"
#include "PreviewToolbar.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "MeshSocketWriter.hpp"
#include "MeshIKChainWriter.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
#include "imgui.h"
#include "ImGuizmo.h"
#include "events/EventDispatcher.hpp"
#include "events/render/PrefabRigPreviewEvents.hpp"
#include "events/physics/SocketEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
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

        glm::vec3 parseVec3(const json& j, float dx, float dy, float dz)
        {
            if (j.is_array() && j.size() >= 3)
                return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
            if (j.is_object())
                return {j.value("x", dx), j.value("y", dy), j.value("z", dz)};
            return {dx, dy, dz};
        }

        // Reads an AssetRef *resolved path* the way writeAssetRef stores it: the GUID lives in
        // "<key>" and the resolved disk path in "<key>Path". For a standalone (no AssetDatabase)
        // parse the path variant is the robust one; fall back to the bare value if it looks like
        // a path (legacy / cold-DB scenes).
        std::string readRefPath(const json& components, const std::string& key)
        {
            const std::string pathKey = key + "Path";
            if (auto it = components.find(pathKey); it != components.end() && it->is_string())
            {
                return it->get<std::string>();
            }
            if (auto it = components.find(key); it != components.end() && it->is_string())
            {
                std::string val = it->get<std::string>();
                const bool looksLikePath = val.find('.') != std::string::npos ||
                                           val.find('/') != std::string::npos ||
                                           val.find('\\') != std::string::npos;
                if (looksLikePath) return val;
            }
            return {};
        }

        animator::ik::JointConstraint parseConstraint(const json& j)
        {
            animator::ik::JointConstraint c;
            if (auto it = j.find("type"); it != j.end() && it->is_string())
                c.type = animator::ik::stringToConstraintType(it->get<std::string>());

            if (auto it = j.find("hingeAxis"); it != j.end() && it->is_array() && it->size() >= 3)
                c.hingeAxis = parseVec3(*it, 0, 1, 0);
            if (auto it = j.find("coneAngle"); it != j.end() && it->is_number())
                c.coneAngle = it->get<float>();
            if (auto it = j.find("swingAngle"); it != j.end() && it->is_number())
                c.swingAngle = it->get<float>();
            if (auto it = j.find("twistMin"); it != j.end() && it->is_number())
                c.twistMin = it->get<float>();
            if (auto it = j.find("twistMax"); it != j.end() && it->is_number())
                c.twistMax = it->get<float>();
            return c;
        }

        void parseIKChains(const json& ikTarget, std::vector<animator::ik::IKChainConfig>& out)
        {
            if (!ikTarget.contains("chains") || !ikTarget["chains"].is_array()) return;
            for (const auto& chainJ : ikTarget["chains"])
            {
                animator::ik::IKChainConfig chain;
                chain.chainName = chainJ.value("chainName", "");
                chain.tipBoneName = chainJ.value("tipBoneName", "");
                if (auto it = chainJ.find("weight"); it != chainJ.end() && it->is_number())
                    chain.weight = it->get<float>();
                if (auto it = chainJ.find("enabled"); it != chainJ.end() && it->is_boolean())
                    chain.enabled = it->get<bool>();
                if (chainJ.contains("chainBoneNames") && chainJ["chainBoneNames"].is_array())
                {
                    for (const auto& b : chainJ["chainBoneNames"])
                        if (b.is_string()) chain.chainBoneNames.push_back(b.get<std::string>());
                }
                if (chainJ.contains("constraints") && chainJ["constraints"].is_array())
                {
                    for (const auto& cJ : chainJ["constraints"])
                        chain.constraints.push_back(parseConstraint(cJ));
                }
                out.push_back(std::move(chain));
            }
        }

        PrefabEntityNode parseEntityFromJson(const json& entityJson, ComponentStats& stats)
        {
            PrefabEntityNode node;
            stats.totalEntities++;

            node.name = entityJson.value("name", "Entity");

            if (entityJson.contains("transform"))
            {
                const auto& t = entityJson["transform"];
                if (t.contains("position")) node.position = parseVec3(t["position"], 0, 0, 0);
                if (t.contains("rotation")) node.rotation = parseVec3(t["rotation"], 0, 0, 0);
                if (t.contains("scale"))    node.scale = parseVec3(t["scale"], 1, 1, 1);
            }

            if (entityJson.contains("components"))
            {
                const auto& c = entityJson["components"];

                static const std::vector<std::pair<std::string, std::string>> componentMap = {
                    {"camera", "Camera"}, {"ibl", "IBL"}, {"mesh", "Mesh"}, {"material", "Material"},
                    {"billboard", "Billboard"}, {"audioSource2D", "AudioSource2D"},
                    {"audioSource3D", "AudioSource3D"}, {"collider", "Collider"}, {"rigidBody", "RigidBody"},
                    {"script", "Script"}, {"navmeshAgent", "NavmeshAgent"}, {"navmeshObstacle", "NavmeshObstacle"},
                    {"vfx", "VFX"}, {"socketAttachment", "SocketAttachment"}, {"socketOverride", "SocketOverride"},
                    {"directionalLight", "DirectionalLight"}, {"pointLight", "PointLight"},
                    {"spotLight", "SpotLight"}, {"controller", "Controller"}, {"behaviorTree", "BehaviorTree"},
                    {"prefabInstance", "PrefabInstance"}, {"ikTarget", "IKTarget"}
                };
                for (const auto& [key, displayName] : componentMap)
                {
                    if (c.contains(key))
                    {
                        node.componentTypes.push_back(displayName);
                        stats.counts[displayName]++;
                    }
                }

                // MeshComponent: writeAssetRef stores meshRef / meshRefPath (+ animatorRef[Path],
                // retargetRef[Path]). The standalone parse reads the resolved-path variant.
                if (c.contains("mesh"))
                {
                    node.meshPath = readRefPath(c["mesh"], "meshRef");
                    node.animatorPath = readRefPath(c["mesh"], "animatorRef");
                    node.retargetPath = readRefPath(c["mesh"], "retargetRef");
                }

                // MaterialComponent: defaultMaterialRef[Path] + per-submesh subMeshMaterials[name].ref[Path].
                if (c.contains("material"))
                {
                    const auto& m = c["material"];
                    node.defaultMaterialPath = readRefPath(m, "defaultMaterialRef");
                    if (auto it = m.find("subMeshMaterials"); it != m.end() && it->is_object())
                    {
                        for (auto& [submeshName, value] : it->items())
                        {
                            if (value.is_object())
                            {
                                std::string p = readRefPath(value, "ref");
                                if (!p.empty()) node.subMeshMaterials[submeshName] = p;
                            }
                            else if (value.is_string())
                            {
                                std::string val = value.get<std::string>();
                                const bool looksLikePath = val.find('.') != std::string::npos ||
                                                           val.find('/') != std::string::npos ||
                                                           val.find('\\') != std::string::npos;
                                if (looksLikePath) node.subMeshMaterials[submeshName] = val;
                            }
                        }
                    }
                }

                // AudioSourceComponent: audioRef[Path] (was wrongly read as audioSource2D.filePath).
                if (c.contains("audioSource2D"))
                    node.audioPath = readRefPath(c["audioSource2D"], "audioRef");
                else if (c.contains("audioSource3D"))
                    node.audioPath = readRefPath(c["audioSource3D"], "audioRef");

                // SocketAttachmentComponent: parentEntityName / socketName / isActive.
                if (c.contains("socketAttachment"))
                {
                    const auto& s = c["socketAttachment"];
                    node.hasSocketAttachment = true;
                    node.attachParentEntityName = s.value("parentEntityName", "");
                    node.attachSocketName = s.value("socketName", "");
                }

                // IKTargetComponent chains.
                if (c.contains("ikTarget"))
                {
                    parseIKChains(c["ikTarget"], node.ikChains);
                }
            }

            if (entityJson.contains("children") && entityJson["children"].is_array())
            {
                for (const auto& childJson : entityJson["children"])
                {
                    if (childJson.is_object())
                        node.children.push_back(parseEntityFromJson(childJson, stats));
                }
            }

            return node;
        }
    } // anonymous namespace

    // The prefab-tree -> PrefabRigDescDTO conversion (buildPrefabRigDescDTO) lives header-only in
    // PrefabRigDescBuilder.hpp so the Tests project can exercise it without linking the Editor.

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
        // applyPrefabRigSnapshot (CQRS) BEFORE this runs; this keeps the window's panels + gizmo base
        // anchor in sync.
        if (snap.previewTransforms.has_value())
            previewTransforms = *snap.previewTransforms;

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

        loadingCancelled.store(true);
        if (loadFuture.valid())
        {
            loadFuture.wait();
        }
        cleanUpPreviewRenderer();
    }

    void PrefabPreviewWindow::draw()
    {
        if (!isOpen)
        {
            if (!previewCleanedUp)
            {
                cleanUpPreviewRenderer();
            }
            return;
        }

        if (needsInit)
        {
            startAsyncLoad();
            needsInit = false;
        }

        updateAsyncLoading();

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
                if (loadingInProgress.load())
                {
                    drawLoadingIndicator();
                }
                else
                {
                    drawEntityTreePanel();
                }
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

                // --- Right: authoring ---
                ImGui::BeginChild("AuthoringPanel", ImVec2(rightWidth, contentSize.y), true);
                drawAuthoringPanel();
                ImGui::EndChild();
            }
        }
        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("PrefabPreview", maximizer.effectiveSize());
            sizeSaved = true;
        }
    }

    // ----------------------------------------------------------------------
    // Async load
    // ----------------------------------------------------------------------
    void PrefabPreviewWindow::startAsyncLoad()
    {
        loadingInProgress.store(true);
        loadingCancelled.store(false);
        loadingStatus = "Loading prefab...";

        loadFuture = std::async(std::launch::async, [this]()
        {
            return loadPrefabBackground(prefabPath);
        });
    }

    void PrefabPreviewWindow::updateAsyncLoading()
    {
        if (!loadingInProgress.load() || !loadFuture.valid())
        {
            return;
        }

        if (loadFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            PrefabLoadResult result = loadFuture.get();

            if (result.success)
            {
                prefabName = std::move(result.prefabName);
                prefabVersion = std::move(result.version);
                rootEntity = std::move(result.rootEntity);
                componentStats = std::move(result.stats);
                prefabLoaded = true;

                rigDesc = buildPrefabRigDescDTO(rootEntity);
                revalidateRefs();

                // Lazily init the renderer, then build from the description.
                initPreviewRenderer();
                buildPreviewFromDesc();
            }
            else
            {
                errorMessage = std::move(result.errorMessage);
                loadFailed = true;
            }

            loadingInProgress.store(false);
        }
    }

    PrefabLoadResult PrefabPreviewWindow::loadPrefabBackground(const std::string& path)
    {
        PrefabLoadResult result;

        try
        {
            if (loadingCancelled.load())
            {
                result.errorMessage = "Cancelled";
                return result;
            }

            std::ifstream file(path);
            if (!file.is_open())
            {
                result.errorMessage = "Failed to open file";
                return result;
            }

            json prefabJson = json::parse(file);
            file.close();

            if (loadingCancelled.load())
            {
                result.errorMessage = "Cancelled";
                return result;
            }

            if (!prefabJson.contains("prefab") || !prefabJson["prefab"].contains("entity"))
            {
                result.errorMessage = "Invalid prefab format";
                return result;
            }

            result.version = prefabJson.value("version", "unknown");
            result.prefabName = prefabJson["prefab"].value("name", "Unnamed");
            result.rootEntity = parseEntityFromJson(prefabJson["prefab"]["entity"], result.stats);
            result.success = true;
        }
        catch (const json::parse_error& e)
        {
            result.errorMessage = std::string("JSON parse error: ") + e.what();
            vfLogError("Prefab preview JSON parse error: {}", e.what());
        }
        catch (const std::exception& e)
        {
            result.errorMessage = e.what();
            vfLogError("Prefab preview error: {}", e.what());
        }

        return result;
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

        // A rebuild reconstructs the assembly's parts (preview transforms reset to identity there),
        // so drop the window's mirror copy too — the gizmo must not re-apply a stale transform.
        previewTransforms.clear();

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
        if (part < 0 || part >= static_cast<int>(rigDesc.parts.size())) return;

        std::string ext = std::filesystem::path(assetPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        services::PrefabRigPartDTO& p = rigDesc.parts[part];
        if (ext == ".vfmesh")
            p.meshPath = assetPath;
        else if (ext == ".vfmaterial")
            p.defaultMaterialPath = assetPath;
        else if (ext == ".vfanim")
            p.animatorPath = assetPath; // makes a static part skeletal / re-points the animator
        else
            return; // unknown extension — leave untouched

        hasUnsavedRefSwap = true;
        swappedParts.insert(part);

        // Transient swap: the whole desc round-trips on rebuild (zero new CQRS). Revalidate so a
        // freshly-dropped (and possibly missing) ref updates the badges, then rebuild the preview.
        revalidateRefs();
        buildPreviewFromDesc();

        // The selected part's sockets came from the OLD mesh; re-pull from the rebuilt assembly so
        // the socket panels reflect the swapped part.
        pullEditSocketsForPart(part);
        chainsLoaded = false; // re-pull chains lazily on next IK-panel draw
    }

    void PrefabPreviewWindow::saveRefSwapsToPrefab()
    {
        refSaveTimer = kSaveFeedbackSeconds;
        refSaveSuccess = false;
        if (swappedParts.empty()) return;

        // Build the ref edits from the live (swapped) rigDesc for the parts the user actually
        // changed. Only non-empty fields are rewritten by the writer.
        std::vector<prefabref::PartRefEdit> edits;
        for (int part : swappedParts)
        {
            if (part < 0 || part >= static_cast<int>(rigDesc.parts.size())) continue;
            const services::PrefabRigPartDTO& p = rigDesc.parts[part];
            prefabref::PartRefEdit e;
            e.part = part;
            e.meshPath = p.meshPath;
            e.animatorPath = p.animatorPath;
            e.defaultMaterialPath = p.defaultMaterialPath;
            edits.push_back(std::move(e));
        }

        json prefabJson;
        try
        {
            std::ifstream in(prefabPath);
            if (!in.is_open())
            {
                vfLogError("Save Refs: cannot open '{}'", prefabPath);
                return;
            }
            prefabJson = json::parse(in);
        }
        catch (const std::exception& e)
        {
            vfLogError("Save Refs: parse error in '{}': {}", prefabPath, e.what());
            return;
        }

        // Inject the real path -> GUID resolver (AssetDatabase-backed; editor links AssetDB). If the
        // DB cannot resolve a path the writer writes the new path into BOTH ref keys, so the loader's
        // path-shaped fallback resolves the new asset (never the stale GUID).
        const int applied = prefabref::applyRefEdits(prefabJson, edits, [](const std::string& path)
        {
            auto ref = asset::AssetRef::fromPath(path);
            return ref.isValid() ? ref.toHexString() : std::string();
        });

        try
        {
            std::ofstream out(prefabPath, std::ios::trunc);
            if (!out.is_open())
            {
                vfLogError("Save Refs: cannot write '{}'", prefabPath);
                return;
            }
            out << prefabJson.dump(2);
        }
        catch (const std::exception& e)
        {
            vfLogError("Save Refs: write error in '{}': {}", prefabPath, e.what());
            return;
        }

        refSaveSuccess = true;
        hasUnsavedRefSwap = false;
        swappedParts.clear();

        events::resource::AssetSavedNotification notif;
        notif.filePath = prefabPath;
        events::EventDispatcher::instance().publish(notif);

        vfLogInfo("Save Refs: persisted {} swapped part ref(s) to '{}'", applied, prefabPath);
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

    prefabrigedit::PrefabRigEditSnapshot PrefabPreviewWindow::snapshotTransforms() const
    {
        prefabrigedit::PrefabRigEditSnapshot snap;
        snap.previewTransforms = previewTransforms;
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

    void PrefabPreviewWindow::pushTransformUndo(prefabrigedit::PrefabRigEditSnapshot before)
    {
        prefabrigedit::PrefabRigEditSnapshot after = snapshotTransforms();
        if (before.previewTransforms == after.previewTransforms)
            return;

        const services::PreviewInstanceId id = getInstanceId();
        // Capture ONLY the id (value). applyPrefabRigSnapshot restores the controller; resyncMirror
        // restores the window's previewTransforms mirror (no-op if the window has been closed) so the
        // gizmo base anchor stays consistent.
        auto cmd = std::make_shared<prefabrigedit::PrefabRigEditUndoCommand>(
            id, std::move(before), std::move(after), "Edit part transform",
            [id](const prefabrigedit::PrefabRigEditSnapshot& snap)
            {
                PrefabPreviewWindow::resyncMirror(id, snap);
            });

        events::undoredo::PushUndoableCommand push;
        push.command = std::make_shared<services::SharedUndoCommand>(std::move(cmd));
        events::EventDispatcher::instance().execute(push);
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
            drawGizmos();
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

    glm::mat4 PrefabPreviewWindow::currentPartPreviewTransform(int part) const
    {
        auto it = previewTransforms.find(part);
        return (it != previewTransforms.end()) ? it->second : glm::mat4(1.0f);
    }

    void PrefabPreviewWindow::drawTransformGizmo()
    {
        // VK-1433 — TRS gizmo on the selected part's EDITOR-TRANSIENT preview transform. Root part
        // -> moves the whole rig; child part -> moves that part (descendants follow because the
        // assembly composes each child off its parent's partWorld). Never serialized.
        if (selectedPart < 0) return;

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

        // The assembly composes partWorld = base * previewTransform, where `base` is the turntable/
        // attachment-chain world (independent of the preview transform). Anchor the gizmo at the
        // LIVE partWorld (which already includes the current preview), and recover `base` from our
        // window-authoritative preview copy:  base = liveWorld * inverse(currentPreview).
        // The identity liveWorld == base * currentPreview holds every frame (the assembly enforces
        // it), so recomputing base each frame is self-consistent even mid-drag.
        const glm::mat4 liveWorld = partWorldLive(selectedPart);
        const glm::mat4 currentPreview = currentPartPreviewTransform(selectedPart);
        const glm::mat4 base = liveWorld * glm::inverse(currentPreview);

        glm::mat4 objectMatrix = liveWorld;

        // Undo bracket: snapshot the whole previewTransforms map on the IsUsing() rising edge
        // (drag start), push one coalesced entry on release. Anchoring on the drag boundaries (not
        // per-Manipulate-frame) makes a full drag undo to its pre-drag state in one Ctrl+Z.
        const bool usingGizmo = ImGuizmo::IsUsing();
        if (usingGizmo && !gizmoEditActive)
        {
            gizmoEditActive = true;
            gizmoEditBefore = snapshotTransforms();
        }

        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 transformGizmoOp, ImGuizmo::WORLD, glm::value_ptr(objectMatrix)))
        {
            // newWorld = base * newPreview  =>  newPreview = inverse(base) * newWorld.
            const glm::mat4 newPreview = glm::inverse(base) * objectMatrix;
            previewTransforms[selectedPart] = newPreview;

            services::events::prefabrigpreview::SetPrefabRigPartPreviewTransformCommand cmd;
            cmd.instanceId = getInstanceId();
            cmd.part = static_cast<size_t>(selectedPart);
            cmd.transform = newPreview;
            events::EventDispatcher::instance().execute(cmd);
        }

        if (!usingGizmo && gizmoEditActive)
        {
            gizmoEditActive = false;
            pushTransformUndo(std::move(gizmoEditBefore));
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
        ImGui::Text("Entities: %u", componentStats.totalEntities);
        ImGui::Text("Rig parts: %zu", rigDesc.parts.size());
        ImGui::Text("IK chains: %zu", rigDesc.ik.size());

        // Phase 2 — broken-asset-reference summary (computed once per build in revalidateRefs).
        if (missingRefCount > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%d missing ref%s",
                               missingRefCount, missingRefCount == 1 ? "" : "s");
        }
    }

    void PrefabPreviewWindow::drawEntityTreePanel()
    {
        ImGui::TextDisabled("Hierarchy");
        if (loadFailed || !prefabLoaded)
        {
            ImGui::TextDisabled("No data");
            return;
        }
        int partCounter = 0; // DFS pre-order mesh-bearing index == part index (matches build)
        drawEntityNode(rootEntity, rootEntity.name, partCounter);
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

    void PrefabPreviewWindow::drawEntityNode(const PrefabEntityNode& node, const std::string& path,
                                             int& partCounter)
    {
        ImGui::PushID(path.c_str());

        // A mesh-bearing node becomes part `partCounter`; consume the index in DFS pre-order so it
        // lines up with buildPrefabRigDescDTO / the broken-ref report.
        const int thisPart = node.hasMesh() ? partCounter++ : -1;
        const std::string tooltip = (thisPart >= 0) ? missingRefTooltip(thisPart) : std::string();
        const bool missing = !tooltip.empty();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
        if (selectedEntityPath.has_value() && *selectedEntityPath == path)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (node.children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        std::string label = node.name;
        if (node.hasMesh()) label += node.isSkeletal() ? " [skel]" : " [static]";

        bool nodeOpen;
        if (missing)
        {
            // Tint the broken-ref node red so it stands out in the tree.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            nodeOpen = ImGui::TreeNodeEx(path.c_str(), flags, "%s  (!)", label.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tooltip.c_str());
        }
        else
        {
            nodeOpen = ImGui::TreeNodeEx(path.c_str(), flags, "%s", label.c_str());
        }

        if (ImGui::IsItemClicked())
            selectedEntityPath = path;

        if (nodeOpen && !node.children.empty())
        {
            for (const auto& child : node.children)
                drawEntityNode(child, path + "/" + child.name, partCounter);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void PrefabPreviewWindow::drawLoadingIndicator()
    {
        ImGui::TextDisabled("%s", loadingStatus.c_str());
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
                    selectedPart = p;
                    pullEditSocketsForPart(selectedPart);
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

        // Drag-drop a .vfMesh / .vfMaterial / .vfAnim from the content browser onto the part combo
        // to swap that ref on the selected part and rebuild live (transient until saved).
        if (selectedPart >= 0)
        {
            if (auto dropped = acceptAssetDropOnLastItem("##partDrop", {".vfMesh", ".vfMaterial", ".vfAnim"}))
            {
                applyAssetDropToPart(selectedPart, *dropped);
            }
        }
        if (refSaveTimer > 0.0f) refSaveTimer -= ImGui::GetIO().DeltaTime;
        if (hasUnsavedRefSwap)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(unsaved)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Part refs were swapped via drag-drop. The change is preview-only "
                                  "until persisted to the .vfPrefab.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Save Refs"))
                saveRefSwapsToPrefab();
        }
        if (refSaveTimer > 0.0f)
        {
            ImGui::SameLine();
            ImGui::TextColored(refSaveSuccess ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.3f, 0.3f, 1),
                               refSaveSuccess ? "Saved" : "Failed");
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
            // NIT #4: if the part deselected MID-edit, close the bracket here (push the in-flight
            // edit against its captured `before`) and clear the flag, so the next session does not
            // coalesce against a stale snapshot. pushTransformUndo no-ops if nothing actually changed.
            if (transformPanelEditActive)
            {
                transformPanelEditActive = false;
                pushTransformUndo(std::move(transformPanelBefore));
            }
            ImGui::TextDisabled("Select a part to transform.");
            return;
        }

        // Undo bracket for the numeric Transform fields + Reset buttons (the viewport gizmo has its
        // own bracket in drawTransformGizmo). Snapshot the whole previewTransforms map before edits.
        if (!transformPanelEditActive)
            transformPanelBefore = snapshotTransforms();

        const bool isRoot = (selectedPart >= 0 && selectedPart < static_cast<int>(rigDesc.parts.size()))
                                ? rigDesc.parts[selectedPart].parentPartIndex < 0
                                : false;
        if (isRoot)
            ImGui::TextDisabled("Root part — moves the whole rig.");
        else
            ImGui::TextDisabled("Child part — moves this part (children follow).");

        // Socket-attached-child TRANSLATE-drop warning. SocketAttachmentUpdater::applyModelOffset
        // builds entityLocal = rot*scale, DROPPING translation — so a non-zero source position on a
        // socketed child will NOT reproduce at instantiation (rotation/scale will). Warn loudly and
        // offer a one-click bake-to-zero through the same JSON round-trip the transform save uses.
        if (!isRoot)
        {
            const int parentPart = (selectedPart < static_cast<int>(rigDesc.parts.size()))
                                       ? rigDesc.parts[selectedPart].parentPartIndex : -1;
            const glm::vec3 srcPos = prefabrigval::sourcePositionForPart(rootEntity, selectedPart);
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

        ImGui::Spacing();
        ImGui::TextDisabled("Operation");
        if (ImGui::RadioButton("Move##tr", transformGizmoOp == ImGuizmo::TRANSLATE))
            transformGizmoOp = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate##tr", transformGizmoOp == ImGuizmo::ROTATE))
            transformGizmoOp = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale##tr", transformGizmoOp == ImGuizmo::SCALE))
            transformGizmoOp = ImGuizmo::SCALE;

        // Numeric transform fields (parallel to the inspector's Transform component). They show the
        // part's EFFECTIVE local transform = base(read from the prefab) * previewTransform(gizmo
        // offset). On OPEN (offset identity) they read the prefab's stored Position/Rotation/Scale;
        // dragging the gizmo updates them live, and editing a field drives the gizmo — both feed the
        // same previewTransform, and the shown values equal what "Save Transforms to Prefab" writes
        // (math::composeMatrix == the prefab / TransformComponent schema; rotation = Euler XYZ deg).
        ImGui::Spacing();
        ImGui::TextDisabled("Transform");
        {
            // Part's base local transform from the parsed prefab tree (DFS pre-order, k-th
            // mesh-bearing node = part k — the same mapping buildPrefabRigDescDTO / the writer use).
            const glm::mat4 base = [this](int part) -> glm::mat4 {
                std::vector<const PrefabEntityNode*> nodes;
                detail::flattenNodes(rootEntity, nodes);
                int k = 0;
                for (const PrefabEntityNode* n : nodes)
                {
                    if (!n->hasMesh()) continue;
                    if (k == part) return math::composeMatrix(n->position, n->rotation, n->scale);
                    ++k;
                }
                return glm::mat4(1.0f);
            }(selectedPart);

            const glm::mat4 effective = base * currentPartPreviewTransform(selectedPart);
            const math::DecomposedTransform d = math::decomposeMatrix(effective);
            float t[3] = {d.position.x, d.position.y, d.position.z};
            float r[3] = {d.rotation.x, d.rotation.y, d.rotation.z};
            float s[3] = {d.scale.x, d.scale.y, d.scale.z};

            bool changed = false;
            ImGui::PushItemWidth(-70.0f);
            changed |= ImGui::DragFloat3("Position##trnum", t, 0.01f);
            changed |= ImGui::DragFloat3("Rotation##trnum", r, 0.1f);
            changed |= ImGui::DragFloat3("Scale##trnum", s, 0.01f);
            ImGui::PopItemWidth();
            if (changed)
            {
                const glm::mat4 newEffective = math::composeMatrix(
                    glm::vec3(t[0], t[1], t[2]), glm::vec3(r[0], r[1], r[2]), glm::vec3(s[0], s[1], s[2]));
                const glm::mat4 newPreview = glm::inverse(base) * newEffective;
                previewTransforms[selectedPart] = newPreview;
                services::events::prefabrigpreview::SetPrefabRigPartPreviewTransformCommand cmd;
                cmd.instanceId = getInstanceId();
                cmd.part = static_cast<size_t>(selectedPart);
                cmd.transform = newPreview;
                events::EventDispatcher::instance().execute(cmd);
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset Transform"))
        {
            previewTransforms[selectedPart] = glm::mat4(1.0f);
            services::events::prefabrigpreview::SetPrefabRigPartPreviewTransformCommand cmd;
            cmd.instanceId = getInstanceId();
            cmd.part = static_cast<size_t>(selectedPart);
            cmd.transform = glm::mat4(1.0f);
            events::EventDispatcher::instance().execute(cmd);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset All"))
        {
            previewTransforms.clear();
            services::events::prefabrigpreview::ResetPrefabRigPreviewTransformsCommand cmd;
            cmd.instanceId = getInstanceId();
            events::EventDispatcher::instance().execute(cmd);
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Live preview by default — not saved unless you click below.");

        ImGui::Separator();
        if (transformSaveTimer > 0.0f) transformSaveTimer -= ImGui::GetIO().DeltaTime;

        ImGui::Checkbox("Include root (whole-rig)", &includeRootInSave);

        // Disable Save when no part has actually been moved (every previewTransform is identity).
        bool anyMoved = false;
        for (const auto& [part, m] : previewTransforms)
        {
            if (m != glm::mat4(1.0f)) { anyMoved = true; break; }
        }
        if (!anyMoved) ImGui::BeginDisabled();
        if (ImGui::Button("Save Transforms to Prefab"))
        {
            saveTransformsToPrefab();
        }
        if (!anyMoved) ImGui::EndDisabled();

        if (transformSaveTimer > 0.0f)
        {
            ImGui::SameLine();
            if (transformSaveSuccess)
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Saved %d", transformSaveCount);
            else
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed");
        }
        ImGui::TextWrapped("Writes part transforms into the .vfPrefab (children by default; "
                           "root only if checked).");

        // Close the transform-panel undo bracket: coalesce a numeric-field drag or a Reset click
        // into ONE entry (the viewport gizmo brackets itself separately in drawTransformGizmo).
        const bool anyItemActive = ImGui::IsAnyItemActive();
        if (anyItemActive)
        {
            transformPanelEditActive = true;
        }
        else if (transformPanelEditActive)
        {
            transformPanelEditActive = false;
            pushTransformUndo(std::move(transformPanelBefore));
        }
    }

    void PrefabPreviewWindow::saveTransformsToPrefab()
    {
        transformSaveTimer = kSaveFeedbackSeconds;
        transformSaveSuccess = false;
        transformSaveCount = 0;

        // Parts to skip: every ROOT part (parentPartIndex < 0) unless the user opted in. A root
        // gizmo edit means "frame the whole rig", not a local node transform.
        std::set<int> skipParts;
        if (!includeRootInSave)
        {
            for (int p = 0; p < static_cast<int>(rigDesc.parts.size()); ++p)
            {
                if (rigDesc.parts[p].parentPartIndex < 0)
                    skipParts.insert(p);
            }
        }

        // Load -> mutate ONLY the targeted transforms -> dump back (round-trip preserves all other
        // fields). entt-free: no PrefabSerialization, no EntityRegistry touched.
        json prefabJson;
        try
        {
            std::ifstream in(prefabPath);
            if (!in.is_open())
            {
                vfLogError("Save Transforms to Prefab: cannot open '{}'", prefabPath);
                return;
            }
            prefabJson = json::parse(in);
        }
        catch (const std::exception& e)
        {
            vfLogError("Save Transforms to Prefab: parse error in '{}': {}", prefabPath, e.what());
            return;
        }

        const int written = prefabtransform::applyPreviewTransforms(prefabJson, previewTransforms, skipParts);

        try
        {
            std::ofstream out(prefabPath, std::ios::trunc);
            if (!out.is_open())
            {
                vfLogError("Save Transforms to Prefab: cannot write '{}'", prefabPath);
                return;
            }
            out << prefabJson.dump(2);
        }
        catch (const std::exception& e)
        {
            vfLogError("Save Transforms to Prefab: write error in '{}': {}", prefabPath, e.what());
            return;
        }

        transformSaveSuccess = true;
        transformSaveCount = written;

        // Refresh the editor/content browser the way other asset saves do.
        events::resource::AssetSavedNotification notif;
        notif.filePath = prefabPath;
        events::EventDispatcher::instance().publish(notif);

        vfLogInfo("Save Transforms to Prefab: wrote {} part transform(s) to '{}'", written, prefabPath);
    }

    void PrefabPreviewWindow::zeroSourceTranslationForPart(int part)
    {
        // Same entt-free JSON round-trip as saveTransformsToPrefab: load -> zero ONLY this node's
        // position -> dump. Every other field round-trips untouched.
        json prefabJson;
        try
        {
            std::ifstream in(prefabPath);
            if (!in.is_open())
            {
                vfLogError("Zero translation: cannot open '{}'", prefabPath);
                return;
            }
            prefabJson = json::parse(in);
        }
        catch (const std::exception& e)
        {
            vfLogError("Zero translation: parse error in '{}': {}", prefabPath, e.what());
            return;
        }

        if (!prefabtransform::zeroPartTranslation(prefabJson, part))
        {
            vfLogWarning("Zero translation: part {} not found in '{}'", part, prefabPath);
            return;
        }

        try
        {
            std::ofstream out(prefabPath, std::ios::trunc);
            if (!out.is_open())
            {
                vfLogError("Zero translation: cannot write '{}'", prefabPath);
                return;
            }
            out << prefabJson.dump(2);
        }
        catch (const std::exception& e)
        {
            vfLogError("Zero translation: write error in '{}': {}", prefabPath, e.what());
            return;
        }

        // The on-disk source position is now zero; re-parse so the warning clears and the preview
        // matches. Re-running the full async parse would be heavy, so just refresh the rig desc by
        // reloading the node tree synchronously is unnecessary — mutate the in-memory mirror.
        // sourcePositionForPart reads rootEntity, so zero it there too (k-th mesh-bearing node).
        {
            std::vector<PrefabEntityNode*> mnodes;
            std::function<void(PrefabEntityNode&)> collect = [&](PrefabEntityNode& n)
            {
                mnodes.push_back(&n);
                for (auto& c : n.children) collect(c);
            };
            collect(rootEntity);
            int k = 0;
            for (PrefabEntityNode* n : mnodes)
            {
                if (!n->hasMesh()) continue;
                if (k == part) { n->position = glm::vec3(0.0f); break; }
                ++k;
            }
        }

        events::resource::AssetSavedNotification notif;
        notif.filePath = prefabPath;
        events::EventDispatcher::instance().publish(notif);

        vfLogInfo("Zero translation: zeroed part {} source position in '{}'", part, prefabPath);
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
            if (ImGui::DragFloat3("Position##bone", pos, 0.01f))
            {
                socket.localPosition = glm::vec3(pos[0], pos[1], pos[2]);
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
            if (ImGui::DragFloat3("Position##static", pos, 0.01f))
            {
                socket.localPosition = glm::vec3(pos[0], pos[1], pos[2]);
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
