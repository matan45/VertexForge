#include "RetargetingEditorWindow.hpp"
#include "../../camera/OrbitCamera.hpp"

#include "imgui.h"
#include "resource/MeshStreamHandle.hpp"
#include "resource/ResourceManager.hpp"
#include "retargeting/RetargetAsset.hpp"
#include "retargeting/HumanoidBoneAutoMap.hpp"
#include "asset/AssetRef.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "nfd/FileDialog.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/animation/AnimationPreviewEvents.hpp"

#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;
using retargeting::HumanoidBoneRole;

namespace windows
{
    namespace
    {
        void setRoleBone(retargeting::HumanoidRigData& rig, const resource::SkeletonData& skel,
                         HumanoidBoneRole role, const std::string& boneName)
        {
            std::erase_if(rig.bindings, [&](const auto& b) { return b.role == role; });
            if (boneName.empty()) return;

            retargeting::HumanoidBoneBinding b;
            b.role = role;
            b.boneName = boneName;
            const int idx = skel.getBoneIndex(boneName);
            if (idx >= 0)
                b.referenceLocalRotation = retargeting::localBindRotation(skel.bones[static_cast<size_t>(idx)].offsetMatrix);
            b.retargetTranslation = (role == HumanoidBoneRole::Hips);
            rig.bindings.push_back(std::move(b));
        }

        void writeMeta(const std::string& assetPath, resource::AssetType type, std::string& outGuidHex)
        {
            const auto metaPath = asset::AssetMetadataSerializer::getMetaPath(assetPath);

            asset::AssetMetadata meta;
            if (auto existing = asset::AssetMetadataSerializer::load(metaPath))
                meta.guid = existing->guid; // preserve GUID across re-saves
            else
                meta.guid = asset::AssetGUID::generate();

            meta.type = type;
            meta.importSourcePath = "editor://retargeting";
            meta.formatVersion = asset::AssetMetadata::kCurrentFormatVersion;

            std::time_t t = std::time(nullptr);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
            meta.importTimestamp = buf;

            asset::AssetMetadataSerializer::save(meta, metaPath);
            outGuidHex = meta.guid.toString();
        }
    }

    RetargetingEditorWindow::RetargetingEditorWindow(const std::string& filePath)
        : retargetPath(filePath)
        , camera(std::make_unique<editor::OrbitCamera>())
    {
        if (retargetPath.empty())
            windowTitle = "Animation Retargeting (new)";
        else
            windowTitle = "Animation Retargeting: " + fs::path(retargetPath).filename().string();
    }

    RetargetingEditorWindow::~RetargetingEditorWindow()
    {
        cleanUpPreview();
    }

    void RetargetingEditorWindow::initPreview()
    {
        services::events::animpreview::InitAnimationPreviewCommand cmd;
        cmd.instanceId = previewInstanceId();
        events::EventDispatcher::instance().execute(cmd);
        previewInitialized = true;
    }

    void RetargetingEditorWindow::cleanUpPreview()
    {
        if (previewCleanedUp || !previewInitialized) return;
        services::events::animpreview::CleanUpAnimationPreviewCommand cmd;
        cmd.instanceId = previewInstanceId();
        events::EventDispatcher::instance().execute(cmd);
        previewCleanedUp = true;
    }

    void RetargetingEditorWindow::updateBonesFromService()
    {
        services::events::animpreview::GetAnimationPreviewEvaluatedBonesQuery query;
        query.instanceId = previewInstanceId();
        evaluatedBones = events::EventDispatcher::instance().query(query);
    }

    bool RetargetingEditorWindow::loadMesh(Side& side, const std::string& meshPath)
    {
        resource::MeshStreamHandle handle;
        if (!handle.openStream(meshPath) || !handle.hasSkeletonData())
        {
            statusMessage = "Mesh has no skeleton: " + meshPath;
            return false;
        }
        resource::SkeletonData skel;
        if (!handle.readSkeleton(skel) || skel.bones.empty())
        {
            statusMessage = "Failed to read skeleton: " + meshPath;
            return false;
        }

        side.meshPath = meshPath;
        side.meshName = fs::path(meshPath).filename().string();
        side.skeleton = std::move(skel);

        const std::string meshGuid = asset::AssetRef::fromPath(meshPath).getGUID().toString();
        side.rig = retargeting::HumanoidRigAsset::createFromSkeleton(side.skeleton, meshGuid);
        side.loaded = true;
        return true;
    }

    bool RetargetingEditorWindow::loadRigInto(Side& side, const std::string& rigPath)
    {
        auto rig = retargeting::HumanoidRigAsset::load(rigPath);
        if (!rig) return false;

        // Resolve the source mesh skeleton this rig describes (for the overlay).
        const std::string meshPath = asset::AssetRef::fromHexString(rig->sourceSkeletonAssetGuid).resolve();
        if (!meshPath.empty())
            loadMesh(side, meshPath); // fills skeleton; then override rig with the saved bindings
        side.rig = *rig;
        side.rigPath = rigPath;
        side.rigGuid = asset::AssetRef::fromPath(rigPath).getGUID().toString();
        side.loaded = !side.skeleton.bones.empty();
        return true;
    }

    void RetargetingEditorWindow::loadExisting()
    {
        std::string ext = fs::path(retargetPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".vfrig")
        {
            // Double-clicked a rig profile: edit it as the target side; Save authors a new .vfretarget.
            loadRigInto(target, retargetPath);
            retargetPath.clear();
            return;
        }

        auto map = retargeting::RetargetMapAsset::load(retargetPath);
        if (!map) return;
        mapData = *map;

        const std::string srcRigPath = asset::AssetRef::fromHexString(mapData.sourceRigAssetGuid).resolve();
        const std::string dstRigPath = asset::AssetRef::fromHexString(mapData.targetRigAssetGuid).resolve();
        if (!srcRigPath.empty()) loadRigInto(source, srcRigPath);
        if (!dstRigPath.empty()) loadRigInto(target, dstRigPath);
    }

    std::string RetargetingEditorWindow::saveRig(Side& side)
    {
        if (side.rigPath.empty())
        {
            // Default: alongside the mesh, same basename + .vfrig
            fs::path p(side.meshPath);
            p.replace_extension(".vfrig");
            side.rigPath = p.string();
        }
        retargeting::HumanoidRigAsset::save(side.rigPath, side.rig);
        writeMeta(side.rigPath, resource::AssetType::HumanoidRig, side.rigGuid);

        events::resource::AssetSavedNotification savedNotif;
        savedNotif.filePath = side.rigPath;
        events::EventDispatcher::instance().publish(savedNotif);
        return side.rigGuid;
    }

    void RetargetingEditorWindow::save()
    {
        if (!source.loaded || !target.loaded)
        {
            statusMessage = "Load both source and target meshes first.";
            return;
        }

        mapData.sourceRigAssetGuid = saveRig(source);
        mapData.targetRigAssetGuid = saveRig(target);
        if (mapData.name.empty())
            mapData.name = source.meshName + " -> " + target.meshName;

        if (retargetPath.empty())
        {
            nfd::FileDialog dialog;
            const std::vector<std::pair<std::wstring, std::wstring>> types = {
                {L"VF Retarget (*.vfretarget)", L"*.vfretarget"}};
            retargetPath = dialog.saveFileDialog(types, L"vfretarget");
            if (retargetPath.empty())
            {
                statusMessage = "Save cancelled.";
                return;
            }
        }

        if (!retargeting::RetargetMapAsset::save(retargetPath, mapData))
        {
            statusMessage = "Failed to write " + retargetPath;
            return;
        }
        std::string mapGuid;
        writeMeta(retargetPath, resource::AssetType::RetargetMap, mapGuid);
        events::resource::AssetSavedNotification mapSaved;
        mapSaved.filePath = retargetPath;
        events::EventDispatcher::instance().publish(mapSaved);

        windowTitle = "Animation Retargeting: " + fs::path(retargetPath).filename().string();
        statusMessage = "Saved " + fs::path(retargetPath).filename().string();
    }

    void RetargetingEditorWindow::drawRoleTable(const char* id, Side& side)
    {
        // Bone-name choices for the combos.
        std::vector<const char*> boneNames;
        boneNames.reserve(side.skeleton.bones.size() + 1);
        boneNames.push_back("(none)");
        for (const auto& b : side.skeleton.bones)
            boneNames.push_back(b.name.c_str());

        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
            return;

        ImGui::TableSetupColumn("Role");
        ImGui::TableSetupColumn("Bone");
        ImGui::TableHeadersRow();

        for (uint8_t r = 1; r < retargeting::humanoidBoneRoleCount; ++r)
        {
            const auto role = static_cast<HumanoidBoneRole>(r);
            ImGui::PushID(r);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            const auto* binding = side.rig.find(role);
            const bool missing = retargeting::isRequiredRole(role) && binding == nullptr;
            if (missing)
                ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%s *", retargeting::humanoidBoneRoleName(role));
            else if (ImGui::Selectable(retargeting::humanoidBoneRoleName(role), side.selectedRole == r))
                side.selectedRole = r;
            if (missing && ImGui::IsItemClicked())
                side.selectedRole = r;

            ImGui::TableSetColumnIndex(1);
            int current = 0;
            if (binding)
            {
                const int idx = side.skeleton.getBoneIndex(binding->boneName);
                current = (idx >= 0) ? idx + 1 : 0;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##bone", &current, boneNames.data(), static_cast<int>(boneNames.size())))
            {
                const std::string chosen = (current == 0) ? std::string{} : side.skeleton.bones[static_cast<size_t>(current - 1)].name;
                setRoleBone(side.rig, side.skeleton, role, chosen);
                side.selectedRole = r;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void RetargetingEditorWindow::drawSkeletonOverlay(const char* id, const Side& side)
    {
        ImGui::BeginChild(id, ImVec2(0, 220), true);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        const auto& skel = side.skeleton;
        const size_t n = skel.bones.size();
        if (n == 0 || skel.inverseBindPoses.size() != n)
        {
            ImGui::TextDisabled("No skeleton");
            ImGui::EndChild();
            return;
        }

        // Model-space bind positions (front view, project X/Y).
        std::vector<glm::vec3> pos(n);
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (size_t i = 0; i < n; ++i)
        {
            const glm::mat4 model = (i < skel.bindPoses.size()) ? skel.bindPoses[i]
                                                                : glm::inverse(skel.inverseBindPoses[i]);
            pos[i] = glm::vec3(model[3]);
            minX = std::min(minX, pos[i].x); maxX = std::max(maxX, pos[i].x);
            minY = std::min(minY, pos[i].y); maxY = std::max(maxY, pos[i].y);
        }

        const float spanX = std::max(maxX - minX, 1e-3f);
        const float spanY = std::max(maxY - minY, 1e-3f);
        const float scale = 0.85f * std::min(avail.x / spanX, avail.y / spanY);
        const float cx = (minX + maxX) * 0.5f;
        const float cy = (minY + maxY) * 0.5f;
        auto toScreen = [&](const glm::vec3& w) {
            return ImVec2(origin.x + avail.x * 0.5f + (w.x - cx) * scale,
                          origin.y + avail.y * 0.5f - (w.y - cy) * scale);
        };

        const std::string highlightBone = [&]() -> std::string {
            if (side.selectedRole <= 0) return {};
            const auto* b = side.rig.find(static_cast<HumanoidBoneRole>(side.selectedRole));
            return b ? b->boneName : std::string{};
        }();

        for (size_t i = 0; i < n; ++i)
        {
            const int parent = skel.bones[i].parentIndex;
            if (parent >= 0)
                dl->AddLine(toScreen(pos[static_cast<size_t>(parent)]), toScreen(pos[i]),
                            IM_COL32(200, 200, 80, 200), 1.5f);
        }
        for (size_t i = 0; i < n; ++i)
        {
            const bool hi = !highlightBone.empty() && skel.bones[i].name == highlightBone;
            dl->AddCircleFilled(toScreen(pos[i]), hi ? 5.0f : 2.5f,
                                hi ? IM_COL32(80, 220, 255, 255) : IM_COL32(230, 120, 120, 255));
        }

        (void)p0;
        ImGui::EndChild();
    }

    void RetargetingEditorWindow::drawSide(const char* id, Side& side)
    {
        ImGui::PushID(id);
        ImGui::TextUnformatted(id);
        ImGui::Separator();

        if (ImGui::Button("Load Mesh..."))
        {
            nfd::FileDialog dialog;
            const std::vector<std::pair<std::wstring, std::wstring>> types = {
                {L"VF Mesh (*.vfMesh)", L"*.vfMesh"}};
            const std::string path = dialog.openFileDialog(types);
            if (!path.empty())
                loadMesh(side, path);
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", side.loaded ? side.meshName.c_str() : "(no mesh)");

        if (side.loaded)
        {
            if (ImGui::Button("Auto-Map"))
                side.rig.bindings = retargeting::autoMapHumanoidBones(side.skeleton);

            // Required-role completeness.
            int missing = 0;
            for (uint8_t r = 1; r < retargeting::humanoidBoneRoleCount; ++r)
            {
                const auto role = static_cast<HumanoidBoneRole>(r);
                if (retargeting::isRequiredRole(role) && !side.rig.find(role)) ++missing;
            }
            ImGui::SameLine();
            if (missing == 0)
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "All required roles mapped");
            else
                ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "%d required role(s) missing", missing);

            drawSkeletonOverlay("##overlay", side);
            drawRoleTable("##roles", side);
        }
        ImGui::PopID();
    }

    void RetargetingEditorWindow::drawPreview()
    {
        ImGui::TextUnformatted("Retarget Preview");
        ImGui::TextDisabled("Target plays the Source clip, retargeted live.");
        ImGui::Separator();

        if (ImGui::Button("Source Anim..."))
        {
            nfd::FileDialog dialog;
            const std::vector<std::pair<std::wstring, std::wstring>> types = {
                {L"VF Animation (*.vfAnim)", L"*.vfAnim"}};
            const std::string path = dialog.openFileDialog(types);
            if (!path.empty())
                sourceAnimPath = path;
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", sourceAnimPath.empty()
                                     ? "(no clip)"
                                     : fs::path(sourceAnimPath).filename().string().c_str());

        const bool canPreview = source.loaded && target.loaded && !sourceAnimPath.empty();
        if (!canPreview) ImGui::BeginDisabled();
        if (ImGui::Button("Apply Retarget Preview"))
        {
            const auto iid = previewInstanceId();
            if (loadedPreviewMeshPath != target.meshPath)
            {
                services::events::animpreview::LoadAnimationPreviewMeshCommand mc;
                mc.instanceId = iid;
                mc.meshPath = target.meshPath;
                meshInPreview = events::EventDispatcher::instance().execute(mc);
                loadedPreviewMeshPath = target.meshPath;
            }
            if (meshInPreview)
            {
                services::events::animpreview::LoadRetargetedAnimationPreviewCommand rc;
                rc.instanceId = iid;
                rc.sourceAnimationPath = sourceAnimPath;
                rc.sourceMeshPath = source.meshPath;
                rc.sourceRig = source.rig;
                rc.targetRig = target.rig;
                rc.map = mapData;
                animInPreview = events::EventDispatcher::instance().execute(rc);
                if (animInPreview)
                {
                    services::events::animpreview::PlayAnimationCommand pc;
                    pc.instanceId = iid;
                    events::EventDispatcher::instance().execute(pc);
                    statusMessage = "Previewing retargeted clip.";
                }
                else
                {
                    statusMessage = "Retarget preview failed (check role mapping).";
                }
            }
        }
        if (!canPreview) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Play"))
        {
            services::events::animpreview::PlayAnimationCommand pc;
            pc.instanceId = previewInstanceId();
            events::EventDispatcher::instance().execute(pc);
        }
        ImGui::SameLine();
        if (ImGui::Button("Pause"))
        {
            services::events::animpreview::PauseAnimationCommand pc;
            pc.instanceId = previewInstanceId();
            events::EventDispatcher::instance().execute(pc);
        }

        services::events::animpreview::IsAnimationPlayingQuery playingQ;
        playingQ.instanceId = previewInstanceId();
        const bool playing = events::EventDispatcher::instance().query(playingQ);

        const auto iid = previewInstanceId();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        windows::animation::ViewportDrawContext ctx{
            size.x, size.y, meshInPreview, animInPreview, playing, camera.get(),
            evaluatedBones, selectedChannel, true, iid, isDraggingPreview, isDraggingPan, environment};
        viewport.draw(ctx);
        updateBonesFromService();
    }

    void RetargetingEditorWindow::draw()
    {
        if (!isOpen)
        {
            cleanUpPreview();
            return;
        }

        if (!triedLoadExisting)
        {
            if (!retargetPath.empty())
                loadExisting();
            triedLoadExisting = true;
        }

        if (!previewInitialized)
            initPreview();

        ImGui::SetNextWindowSize(editor::preview::initialWindowSize("Retargeting", ImVec2(1100, 640)),
                                 ImGuiCond_FirstUseEver);
        maximizer.preBegin();
        if (ImGui::Begin(windowTitle.c_str(), &isOpen, maximizer.windowFlags()))
        {
            const bool canSave = source.loaded && target.loaded;
            if (!canSave) ImGui::BeginDisabled();
            if (ImGui::Button("Save"))
                save();
            if (!canSave) ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Source clips retarget onto Target.");
            if (!statusMessage.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(statusMessage.c_str());
            }
            ImGui::SameLine();
            maximizer.drawButton();
            ImGui::Separator();

            const float mapW = ImGui::GetContentRegionAvail().x * 0.5f;
            ImGui::BeginChild("Mapping", ImVec2(mapW, 0), false);
            {
                const float colW = (mapW - 8.0f) * 0.5f;
                ImGui::BeginChild("SourceCol", ImVec2(colW, 0), true);
                drawSide("Source", source);
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("TargetCol", ImVec2(0, 0), true);
                drawSide("Target", target);
                ImGui::EndChild();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("PreviewCol", ImVec2(0, 0), true);
            drawPreview();
            ImGui::EndChild();
        }
        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("Retargeting", maximizer.effectiveSize());
            sizeSaved = true;
        }
    }
}
