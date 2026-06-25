#include "print/Log.hpp"
#include "PrefabAuthoringPanels.hpp"
#include "PrefabPreviewWindow.hpp"
#include "PrefabRigEditDetail.hpp"  // prefabdetail:: NaN guards / timings / save badge
#include "PrefabRigBonePick.hpp"    // Phase 1b screen-space bone-pick math
#include "PrefabRigValidation.hpp"  // prefabrigval::childHasDroppedTranslation
#include "../../camera/OrbitCamera.hpp"
#include "MeshSocketWriter.hpp"
#include "MeshIKChainWriter.hpp"
#include "imgui.h"
#include "ImGuizmo.h"
#include "events/EventDispatcher.hpp"
#include "events/render/PrefabRigPreviewEvents.hpp"
#include "events/physics/SocketEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp" // GetEntityQuery / SetTransformCommand
#include "math/TransformUtils.hpp"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace windows
{
    glm::mat4 PrefabAuthoringPanels::partWorldLive(int part) const
    {
        if (part < 0) return glm::mat4(1.0f);
        services::events::prefabrigpreview::GetPrefabRigPartWorldQuery q;
        q.instanceId = ctx.instanceId;
        q.part = static_cast<size_t>(part);
        return events::EventDispatcher::instance().query(q);
    }

    void PrefabAuthoringPanels::drawGizmos()
    {
        // Exactly ONE gizmo is drawn per frame (gated by gizmoMode) so they never fight over the
        // mouse. Bone-socket + IK modes have no viewport gizmo (bone sockets ride the live pose;
        // IK is panel-driven), so only Transform and StaticSocket draw here.
        switch (ctx.gizmoMode)
        {
        case GizmoMode::Transform:    drawTransformGizmo(); break;
        case GizmoMode::StaticSocket: drawSocketGizmo();    break;
        case GizmoMode::BoneSocket:
        case GizmoMode::IK:           break;
        }
    }

    void PrefabAuthoringPanels::drawTransformGizmo()
    {
        // VK-1433 Phase 4c — TRS gizmo on the selected part's SOURCE ENTITY transform (Q5). Dragging
        // the gizmo edits the entity's OWN local transform (SetTransformCommand), so the change is
        // persisted by Save Prefab. Root part -> moves the whole rig; a socket-attached child's
        // translation is dropped at instantiation (the Transform panel warns about this), but
        // rotation/scale carry — consistent with attachChildRotation/attachChildScale.
        if (ctx.selectedPart < 0 || ctx.selectedPart >= static_cast<int>(ctx.partEntities.size())) return;

        ImVec2 imgMin = ImGui::GetItemRectMin();
        ImVec2 imgSz = ImGui::GetItemRectSize();
        if (imgSz.x <= 0.0f || imgSz.y <= 0.0f) return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSz.x, imgSz.y);

        // Undo Vulkan Y-flip for ImGuizmo (expects OpenGL-style projection).
        glm::mat4 view = w.camera->getViewMatrix();
        glm::mat4 proj = w.camera->getProjectionMatrix();
        proj[1][1] *= -1.0f;

        // The assembly composes the live partWorld from the part's parent chain folded together with
        // the part's OWN local transform. Treat that own-local matrix as the editable factor: recover
        // its containing frame `base = liveWorld * inverse(compose(ownLocal))`, then a manipulated
        // world maps back to the new own-local via `inverse(base) * newWorld`. This is the same
        // base/inverse(base) pattern the old preview gizmo used, but the editable factor is now the
        // persisted entity transform rather than the retired transient preview offset.
        const services::TransformData ownLocal = partSourceLocalTransform(ctx.selectedPart);
        const glm::mat4 ownLocalMat = math::composeMatrix(ownLocal.position, ownLocal.rotation, ownLocal.scale);
        const glm::mat4 liveWorld = partWorldLive(ctx.selectedPart);

        // NaN guard (see the anonymous-namespace helpers): a degenerate (near-zero) scale makes
        // ownLocalMat singular, so glm::inverse() below is inf/NaN; a pre-existing NaN could also be in
        // liveWorld. Either way the base/inverse(base) chain would persist a non-finite transform via
        // SetTransformCommand and feed it back next frame (sticky NaN). Skip the gizmo this frame if the
        // own-local frame is non-invertible / non-finite — the inspector numeric fields (forward
        // compose, no inverse) still work to recover a usable scale.
        constexpr float kMinInvertibleScale = 1e-4f;
        if (!prefabdetail::isFiniteMat(liveWorld) || prefabdetail::minAbsComponent(ownLocal.scale) < kMinInvertibleScale)
            return;
        const glm::mat4 base = liveWorld * glm::inverse(ownLocalMat);
        if (!prefabdetail::isFiniteMat(base))
            return;

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

            // Final NaN backstop: never persist a non-finite transform (decompose can still degenerate
            // at a gimbal-lock / shear edge even with a valid scale). Drop this manipulation frame.
            if (prefabdetail::isFiniteVec(d.position) && prefabdetail::isFiniteVec(d.rotation) && prefabdetail::isFiniteVec(d.scale))
            {
                services::TransformData t;
                t.position = d.position;
                t.rotation = d.rotation; // Euler XYZ degrees == TransformComponent schema
                t.scale = d.scale;

                events::scene::SetTransformCommand cmd;
                cmd.entity = ctx.partEntities[ctx.selectedPart];
                cmd.transform = t;
                events::EventDispatcher::instance().execute(cmd);

                // The SetTransformCommand publishes TransformChangedNotification synchronously, which
                // sets transformsDirty -> draw()'s end-of-frame block applies the CHEAP transform-only
                // sync (no full mesh reload). No rebuildRigFromSandbox() here: that reloaded the entire
                // rig from disk every drag frame (the frame drop + load-log spam this fixes).
                ctx.dirty = true;
            }
        }

        if (!usingGizmo && transformGizmoEditActive)
        {
            transformGizmoEditActive = false;
            // after = the entity transform as it stands now (post-drag). No-op-gated inside.
            w.undoCoordinator.pushTransformUndo(ctx.partEntities[ctx.selectedPart], transformGizmoBefore,
                              partSourceLocalTransform(ctx.selectedPart));
        }
    }

    void PrefabAuthoringPanels::drawSocketGizmo()
    {
        // Only when authoring a STATIC part's socket (skeletal sockets ride bones; no gizmo).
        if (ctx.selectedPart < 0 || partIsSkeletal(ctx.selectedPart)) return;
        if (ctx.selectedSocketIndex < 0 || ctx.selectedSocketIndex >= static_cast<int>(ctx.editSockets.size())) return;

        ImVec2 imgMin = ImGui::GetItemRectMin();
        ImVec2 imgSz = ImGui::GetItemRectSize();
        if (imgSz.x <= 0.0f || imgSz.y <= 0.0f) return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSz.x, imgSz.y);

        // Undo Vulkan Y-flip for ImGuizmo (expects OpenGL-style projection).
        glm::mat4 view = w.camera->getViewMatrix();
        glm::mat4 proj = w.camera->getProjectionMatrix();
        proj[1][1] *= -1.0f;

        // VK-1433 F1 fix: anchor at the part's LIVE composed world (where the part actually renders
        // after the attachment chain), not the turntable-local matrix. The gizmo still edits the
        // part's OWN socket offset, so we transform between socket-local and world through partWorld.
        auto& socket = ctx.editSockets[ctx.selectedSocketIndex];
        glm::mat4 model = partWorldLive(ctx.selectedPart);
        glm::mat4 objectMatrix = model * socket.getLocalOffsetMatrix();

        // Undo bracket on the gizmo drag (ImGuizmo drags do NOT register as ImGui items, so the
        // socket-panel IsAnyItemActive bracket does not catch them). Snapshot on the IsUsing() rising
        // edge, push a socket undo on release.
        const bool usingGizmo = ImGuizmo::IsUsing();
        if (usingGizmo && !gizmoEditActive)
        {
            gizmoEditActive = true;
            gizmoEditBefore = w.undoCoordinator.snapshotSockets();
        }

        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 socketGizmoOp, ImGuizmo::LOCAL, glm::value_ptr(objectMatrix)))
        {
            glm::mat4 localMatrix = glm::inverse(model) * objectMatrix;
            float translation[3], rotation[3], scale[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localMatrix), translation, rotation, scale);
            socket.localPosition = glm::vec3(translation[0], translation[1], translation[2]);
            socket.localRotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
            pushEditSocketsForPart(ctx.selectedPart); // live: next update() re-resolves
        }

        if (!usingGizmo && gizmoEditActive)
        {
            gizmoEditActive = false;
            w.undoCoordinator.pushSocketUndo(std::move(gizmoEditBefore));
        }
    }

    void PrefabAuthoringPanels::tryBonePick(const ImVec2& viewportMin, const ImVec2& viewportSize)
    {
        // Guard: only meaningful for a skeletal part with the skeleton overlay visible (the joints the
        // user is clicking are the overlay's markers). The caller already gated on the arm flag + a
        // non-gizmo click inside the Image.
        if (ctx.selectedPart < 0 || !partIsSkeletal(ctx.selectedPart) || !w.environment.showSkeleton)
            return;

        // World-space joints for the part (queried across the preview boundary; entt-free).
        services::events::prefabrigpreview::GetPrefabRigJointWorldsQuery jq;
        jq.instanceId = ctx.instanceId;
        jq.part = static_cast<size_t>(ctx.selectedPart);
        const std::vector<services::PrefabRigJoint> joints =
            events::EventDispatcher::instance().query(jq);
        if (joints.empty())
            return;

        std::vector<glm::vec3> jointWorlds;
        jointWorlds.reserve(joints.size());
        for (const services::PrefabRigJoint& j : joints)
            jointWorlds.push_back(j.world);

        // Same camera matrices the gizmo path uses (the helper applies the Vulkan-Y flip itself).
        const glm::mat4 view = w.camera->getViewMatrix();
        const glm::mat4 proj = w.camera->getProjectionMatrix();

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
        prefabrigedit::PrefabRigEditSnapshot before = w.undoCoordinator.snapshotSockets();

        // Prefill the new bone socket: ride the picked bone, identity local offset (the user fine-tunes
        // it with the existing gizmo/fields). De-dup the default name against the live sockets.
        animator::SocketDefinition s;
        s.targetBoneName = hitJoint.boneName;
        s.boneIndex = hitJoint.boneIndex;
        const std::string baseName =
            (hitJoint.boneName.empty() ? std::string("bone") : hitJoint.boneName) + "_socket";
        std::string candidate = baseName;
        for (int suffix = 2; animator::indexOfSocket(ctx.editSockets, candidate) >= 0; ++suffix)
            candidate = baseName + "_" + std::to_string(suffix);
        s.name = candidate;

        ctx.editSockets.push_back(std::move(s));
        ctx.selectedSocketIndex = static_cast<int>(ctx.editSockets.size()) - 1;
        pushEditSocketsForPart(ctx.selectedPart); // live: next update() re-resolves with the new socket

        w.undoCoordinator.pushSocketUndo(std::move(before));

        ctx.bonePickArmed = false; // one pick per arm — make the disarm obvious in the panel
    }

    // ----------------------------------------------------------------------
    // Authoring
    // ----------------------------------------------------------------------
    bool PrefabAuthoringPanels::partIsSkeletal(int part) const
    {
        if (part < 0 || part >= static_cast<int>(ctx.rigDesc.parts.size())) return false;
        return !ctx.rigDesc.parts[part].animatorPath.empty();
    }

    const std::string& PrefabAuthoringPanels::partMeshPath(int part) const
    {
        static const std::string empty;
        if (part < 0 || part >= static_cast<int>(ctx.rigDesc.parts.size())) return empty;
        return ctx.rigDesc.parts[part].meshPath;
    }

    void PrefabAuthoringPanels::pullEditSocketsForPart(int part)
    {
        ctx.editSockets.clear();
        ctx.selectedSocketIndex = -1;
        ctx.bonePickArmed = false; // an armed pick targets the previously-selected part; clear on switch
        if (part < 0) return;

        services::events::prefabrigpreview::GetPrefabRigSocketsQuery query;
        query.instanceId = ctx.instanceId;
        query.part = static_cast<size_t>(part);
        ctx.editSockets = events::EventDispatcher::instance().query(query);
    }

    void PrefabAuthoringPanels::pushEditSocketsForPart(int part)
    {
        if (part < 0) return;
        services::events::prefabrigpreview::SetPrefabRigSocketsCommand cmd;
        cmd.instanceId = ctx.instanceId;
        cmd.part = static_cast<size_t>(part);
        cmd.sockets = ctx.editSockets;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PrefabAuthoringPanels::pullEditChains()
    {
        services::events::prefabrigpreview::GetPrefabRigChainsQuery query;
        query.instanceId = ctx.instanceId;
        ctx.editChains = events::EventDispatcher::instance().query(query);
    }

    void PrefabAuthoringPanels::pushEditChains()
    {
        services::events::prefabrigpreview::SetPrefabRigChainsCommand cmd;
        cmd.instanceId = ctx.instanceId;
        cmd.chains = ctx.editChains;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PrefabAuthoringPanels::selectPart(int part)
    {
        if (part < 0 || part >= static_cast<int>(ctx.rigDesc.parts.size()))
        {
            ctx.selectedPart = -1;
            pullEditSocketsForPart(-1);
            return;
        }
        ctx.selectedPart = part;
        pullEditSocketsForPart(ctx.selectedPart);
    }

    services::TransformData PrefabAuthoringPanels::partSourceLocalTransform(int part) const
    {
        services::TransformData t; // identity defaults
        if (part < 0 || part >= static_cast<int>(ctx.partEntities.size())) return t;
        events::scene::GetEntityQuery q;
        q.entity = ctx.partEntities[part];
        auto data = events::EventDispatcher::instance().query(q);
        if (data.has_value()) return data->localTransform;
        return t;
    }

    void PrefabAuthoringPanels::drawAuthoringPanel()
    {
        if (!ctx.previewBuilt)
        {
            ImGui::TextDisabled("Authoring available once the rig is built.");
            return;
        }

        // The authoring panels below act on the part selected in the Hierarchy tree (the tree click
        // keeps selectedPart in sync via partForEntity). The old top-of-panel "Part" combo was a
        // redundant second selector — removed; selection now lives only in the hierarchy.

        // VK-1433 gizmo-mode toolbar — exactly one viewport gizmo is active (never fight).
        drawGizmoModeToolbar();

        ImGui::Separator();

        if (ImGui::BeginTabBar("##authoringTabs"))
        {
            if (ImGui::BeginTabItem("Transform"))
            {
                if (ImGui::IsItemActivated()) ctx.gizmoMode = GizmoMode::Transform;
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
                if (ImGui::IsItemActivated()) ctx.gizmoMode = GizmoMode::BoneSocket;
                drawBoneSocketPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Static Socket"))
            {
                if (ImGui::IsItemActivated()) ctx.gizmoMode = GizmoMode::StaticSocket;
                drawStaticSocketPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("IK"))
            {
                if (ImGui::IsItemActivated()) ctx.gizmoMode = GizmoMode::IK;
                drawIKPanel();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void PrefabAuthoringPanels::drawGizmoModeToolbar()
    {
        ImGui::TextDisabled("Gizmo:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Transform", ctx.gizmoMode == GizmoMode::Transform))
            ctx.gizmoMode = GizmoMode::Transform;
        ImGui::SameLine();
        if (ImGui::RadioButton("Bone##gm", ctx.gizmoMode == GizmoMode::BoneSocket))
            ctx.gizmoMode = GizmoMode::BoneSocket;
        ImGui::SameLine();
        if (ImGui::RadioButton("Static##gm", ctx.gizmoMode == GizmoMode::StaticSocket))
            ctx.gizmoMode = GizmoMode::StaticSocket;
        ImGui::SameLine();
        if (ImGui::RadioButton("IK##gm", ctx.gizmoMode == GizmoMode::IK))
            ctx.gizmoMode = GizmoMode::IK;
    }

    void PrefabAuthoringPanels::drawTransformPanel()
    {
        if (ctx.selectedPart < 0)
        {
            ImGui::TextDisabled("Select a part to transform.");
            return;
        }

        const bool isRoot = (ctx.selectedPart >= 0 && ctx.selectedPart < static_cast<int>(ctx.rigDesc.parts.size()))
                                ? ctx.rigDesc.parts[ctx.selectedPart].parentPartIndex < 0
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
            const int parentPart = (ctx.selectedPart < static_cast<int>(ctx.rigDesc.parts.size()))
                                       ? ctx.rigDesc.parts[ctx.selectedPart].parentPartIndex : -1;
            const glm::vec3 srcPos = partSourceLocalTransform(ctx.selectedPart).position;
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
                    zeroSourceTranslationForPart(ctx.selectedPart);
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

    void PrefabAuthoringPanels::zeroSourceTranslationForPart(int part)
    {
        // VK-1433 Phase 4c — zero the part's source ENTITY position (SetTransformCommand) and
        // re-derive the rig so the preview matches and the warning clears. The change is persisted by
        // Save Prefab (the entity is the source of truth — the old JSON round-trip is retired).
        if (part < 0 || part >= static_cast<int>(ctx.partEntities.size())) return;

        const services::TransformData before = partSourceLocalTransform(part);
        services::TransformData t = before;
        t.position = glm::vec3(0.0f);

        events::scene::SetTransformCommand cmd;
        cmd.entity = ctx.partEntities[part];
        cmd.transform = t;
        events::EventDispatcher::instance().execute(cmd);

        ctx.dirty = true;
        w.sandboxController.rebuildRigFromSandbox();

        // VK-1433 Phase 4d — make the bake undoable too (same entity-transform replay path as the
        // Transform gizmo). No-op-gated, so a part already at zero translation pushes nothing.
        w.undoCoordinator.pushTransformUndo(ctx.partEntities[part], before, t);

        vfLogInfo("Prefab preview: zeroed part {} source entity translation", part);
    }

    void PrefabAuthoringPanels::drawStatePicker()
    {
        // Play / pause.
        services::events::prefabrigpreview::IsPrefabRigPausedQuery pausedQuery;
        pausedQuery.instanceId = ctx.instanceId;
        bool paused = events::EventDispatcher::instance().query(pausedQuery);

        if (ImGui::Button(paused ? "Play" : "Pause"))
        {
            if (paused)
            {
                services::events::prefabrigpreview::PlayPrefabRigCommand cmd;
                cmd.instanceId = ctx.instanceId;
                events::EventDispatcher::instance().execute(cmd);
            }
            else
            {
                services::events::prefabrigpreview::PausePrefabRigCommand cmd;
                cmd.instanceId = ctx.instanceId;
                events::EventDispatcher::instance().execute(cmd);
            }
        }

        // VK-1433 frame-by-frame scrub (needs a skeletal part selected).
        drawFrameScrub();

        ImGui::Separator();

        if (ctx.selectedPart < 0)
        {
            ImGui::TextDisabled("Select a part.");
            return;
        }
        if (!partIsSkeletal(ctx.selectedPart))
        {
            ImGui::TextDisabled("Static part — no animator states.");
            return;
        }

        services::events::prefabrigpreview::GetPrefabRigStatesQuery statesQuery;
        statesQuery.instanceId = ctx.instanceId;
        statesQuery.part = static_cast<size_t>(ctx.selectedPart);
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
                    cmd.instanceId = ctx.instanceId;
                    cmd.part = static_cast<size_t>(ctx.selectedPart);
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
            cmd.instanceId = ctx.instanceId;
            cmd.part = static_cast<size_t>(ctx.selectedPart);
            cmd.name = paramName;
            cmd.value = boolVal;
            events::EventDispatcher::instance().execute(cmd);
        }
        ImGui::SameLine();
        ImGui::Checkbox("##boolVal", &boolVal);

        if (ImGui::Button("Set Float"))
        {
            services::events::prefabrigpreview::SetPrefabRigFloatCommand cmd;
            cmd.instanceId = ctx.instanceId;
            cmd.part = static_cast<size_t>(ctx.selectedPart);
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
            cmd.instanceId = ctx.instanceId;
            cmd.part = static_cast<size_t>(ctx.selectedPart);
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
            cmd.instanceId = ctx.instanceId;
            cmd.part = static_cast<size_t>(ctx.selectedPart);
            cmd.name = paramName;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void PrefabAuthoringPanels::drawFrameScrub()
    {
        if (ctx.selectedPart < 0 || !partIsSkeletal(ctx.selectedPart))
            return; // scrub applies to a skeletal part's animator

        ImGui::Spacing();
        ImGui::TextDisabled("Frame scrub (part %d)", ctx.selectedPart);

        // Step ±1 frame even while paused (stepFrame advances the layer stack by one source frame).
        if (ImGui::Button("|< Prev"))
        {
            services::events::prefabrigpreview::StepPrefabRigFrameCommand cmd;
            cmd.instanceId = ctx.instanceId;
            cmd.part = static_cast<size_t>(ctx.selectedPart);
            cmd.frames = -1;
            events::EventDispatcher::instance().execute(cmd);
        }
        ImGui::SameLine();
        if (ImGui::Button("Next >|"))
        {
            services::events::prefabrigpreview::StepPrefabRigFrameCommand cmd;
            cmd.instanceId = ctx.instanceId;
            cmd.part = static_cast<size_t>(ctx.selectedPart);
            cmd.frames = 1;
            events::EventDispatcher::instance().execute(cmd);
        }

        // Absolute scrub slider: read the current normalized time, seek on edit.
        services::events::prefabrigpreview::GetPrefabRigNormalizedTimeQuery q;
        q.instanceId = ctx.instanceId;
        q.part = static_cast<size_t>(ctx.selectedPart);
        float normalized = events::EventDispatcher::instance().query(q);

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##prefabScrub", &normalized, 0.0f, 1.0f, "t = %.3f"))
        {
            services::events::prefabrigpreview::SetPrefabRigNormalizedTimeCommand cmd;
            cmd.instanceId = ctx.instanceId;
            cmd.part = static_cast<size_t>(ctx.selectedPart);
            cmd.t = normalized;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void PrefabAuthoringPanels::drawBoneSocketPanel()
    {
        if (ctx.selectedPart < 0 || !partIsSkeletal(ctx.selectedPart))
        {
            ImGui::TextDisabled("Select a skeletal part.");
            return;
        }

        // Undo bracket: snapshot the sockets BEFORE the controls run while no edit session is in
        // flight; the matching push at the end of the panel coalesces a completed drag into ONE entry.
        if (!socketEditActive)
            socketEditBefore = w.undoCoordinator.snapshotSockets();

        // VK-1433 Phase 1b — bone-pick socket creation. Arm, then click a skeleton joint in the
        // viewport to prefill a new bone socket on the picked bone (identity offset; fine-tune below
        // or with the existing fields, then "Save Sockets to Mesh"). Requires the Skeleton overlay so
        // the user can see the joints they are clicking.
        if (!w.environment.showSkeleton)
        {
            ctx.bonePickArmed = false; // can't aim at joints that aren't drawn
            ImGui::BeginDisabled();
            ImGui::Button("Pick bone (click joint)");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(enable the Skeleton overlay)");
        }
        else if (!ctx.bonePickArmed)
        {
            if (ImGui::Button("Pick bone (click joint)"))
                ctx.bonePickArmed = true;
        }
        else
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Click a joint in the viewport...");
            ImGui::SameLine();
            if (ImGui::Button("Cancel##bonePick"))
                ctx.bonePickArmed = false;
        }
        ImGui::Separator();

        ImGui::TextDisabled("Bone sockets (%zu)", ctx.editSockets.size());
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(ctx.editSockets.size()); ++i)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (ctx.selectedSocketIndex == i) flags |= ImGuiTreeNodeFlags_Selected;
            bool open = ImGui::TreeNodeEx(ctx.editSockets[i].name.c_str(), flags);
            if (ImGui::IsItemClicked()) ctx.selectedSocketIndex = i;
            if (open) ImGui::TreePop();
        }

        if (ctx.selectedSocketIndex >= 0 && ctx.selectedSocketIndex < static_cast<int>(ctx.editSockets.size()))
        {
            auto& socket = ctx.editSockets[ctx.selectedSocketIndex];
            ImGui::Separator();
            ImGui::Text("Target bone: %s", socket.targetBoneName.c_str());

            float pos[3] = {socket.localPosition.x, socket.localPosition.y, socket.localPosition.z};
            if (ImGui::DragFloat3("Local Position##bone", pos, 0.01f))
            {
                socket.localPosition = glm::vec3(pos[0], pos[1], pos[2]);
                pushEditSocketsForPart(ctx.selectedPart);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("0##bonePos"))
            {
                socket.localPosition = glm::vec3(0.0f); // a zero-offset socket lands exactly on its joint
                pushEditSocketsForPart(ctx.selectedPart);
            }
            glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(socket.localRotation));
            float rot[3] = {eulerDeg.x, eulerDeg.y, eulerDeg.z};
            if (ImGui::DragFloat3("Rotation##bone", rot, 0.5f))
            {
                socket.localRotation = glm::quat(glm::radians(glm::vec3(rot[0], rot[1], rot[2])));
                pushEditSocketsForPart(ctx.selectedPart);
            }
        }

        ImGui::Separator();
        if (socketSaveTimer > 0.0f) socketSaveTimer -= ImGui::GetIO().DeltaTime;
        bool canSave = !partMeshPath(ctx.selectedPart).empty();
        if (!canSave) ImGui::BeginDisabled();
        if (ImGui::Button("Save Sockets to Mesh"))
        {
            socketSaveSuccess =
                types::MeshSocketWriter::saveSocketsToMesh(partMeshPath(ctx.selectedPart), ctx.editSockets);
            socketSaveTimer = prefabdetail::kSaveFeedbackSeconds;
            if (socketSaveSuccess)
            {
                events::socket::SocketDataSavedNotification notif;
                notif.meshPath = partMeshPath(ctx.selectedPart);
                events::EventDispatcher::instance().publish(notif);
            }
        }
        if (!canSave) ImGui::EndDisabled();
        prefabdetail::drawSaveBadge(socketSaveTimer, socketSaveSuccess);

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
            w.undoCoordinator.pushSocketUndo(std::move(socketEditBefore));
        }
    }

    void PrefabAuthoringPanels::drawStaticSocketPanel()
    {
        if (ctx.selectedPart < 0 || partIsSkeletal(ctx.selectedPart))
        {
            ImGui::TextDisabled("Select a static part (e.g. weapon).");
            return;
        }

        // Undo bracket (see drawBoneSocketPanel): snapshot before edits while no session active.
        if (!socketEditActive)
            socketEditBefore = w.undoCoordinator.snapshotSockets();

        // Create.
        static char newName[128] = "";
        ImGui::InputText("Name##newStatic", newName, sizeof(newName));
        ImGui::SameLine();
        if (ImGui::Button("Add") && std::strlen(newName) > 0)
        {
            animator::SocketDefinition s;
            s.name = newName;
            ctx.editSockets.push_back(s);
            ctx.selectedSocketIndex = static_cast<int>(ctx.editSockets.size()) - 1;
            newName[0] = '\0';
            pushEditSocketsForPart(ctx.selectedPart);
        }

        ImGui::TextDisabled("Static sockets (%zu)", ctx.editSockets.size());
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(ctx.editSockets.size()); ++i)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (ctx.selectedSocketIndex == i) flags |= ImGuiTreeNodeFlags_Selected;
            bool open = ImGui::TreeNodeEx(ctx.editSockets[i].name.c_str(), flags);
            if (ImGui::IsItemClicked()) ctx.selectedSocketIndex = i;
            if (open) ImGui::TreePop();
        }

        if (ctx.selectedSocketIndex >= 0 && ctx.selectedSocketIndex < static_cast<int>(ctx.editSockets.size()))
        {
            auto& socket = ctx.editSockets[ctx.selectedSocketIndex];
            ImGui::Separator();

            float pos[3] = {socket.localPosition.x, socket.localPosition.y, socket.localPosition.z};
            if (ImGui::DragFloat3("Local Position##static", pos, 0.01f))
            {
                socket.localPosition = glm::vec3(pos[0], pos[1], pos[2]);
                pushEditSocketsForPart(ctx.selectedPart);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("0##staticPos"))
            {
                socket.localPosition = glm::vec3(0.0f); // a zero-offset socket lands at the part origin
                pushEditSocketsForPart(ctx.selectedPart);
            }
            glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(socket.localRotation));
            float rot[3] = {eulerDeg.x, eulerDeg.y, eulerDeg.z};
            if (ImGui::DragFloat3("Rotation##static", rot, 0.5f))
            {
                socket.localRotation = glm::quat(glm::radians(glm::vec3(rot[0], rot[1], rot[2])));
                pushEditSocketsForPart(ctx.selectedPart);
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
                ctx.editSockets.erase(ctx.editSockets.begin() + ctx.selectedSocketIndex);
                ctx.selectedSocketIndex = -1;
                pushEditSocketsForPart(ctx.selectedPart);
            }
        }

        ImGui::Separator();
        if (socketSaveTimer > 0.0f) socketSaveTimer -= ImGui::GetIO().DeltaTime;
        bool canSave = !partMeshPath(ctx.selectedPart).empty() && !ctx.editSockets.empty();
        if (!canSave) ImGui::BeginDisabled();
        if (ImGui::Button("Save Sockets to Mesh##static"))
        {
            socketSaveSuccess =
                types::MeshSocketWriter::saveSocketsToMesh(partMeshPath(ctx.selectedPart), ctx.editSockets);
            socketSaveTimer = prefabdetail::kSaveFeedbackSeconds;
            if (socketSaveSuccess)
            {
                events::socket::SocketDataSavedNotification notif;
                notif.meshPath = partMeshPath(ctx.selectedPart);
                events::EventDispatcher::instance().publish(notif);
            }
        }
        if (!canSave) ImGui::EndDisabled();
        prefabdetail::drawSaveBadge(socketSaveTimer, socketSaveSuccess);

        // Close the undo bracket (see drawBoneSocketPanel).
        const bool anyItemActive = ImGui::IsAnyItemActive();
        if (anyItemActive)
        {
            socketEditActive = true;
        }
        else if (socketEditActive)
        {
            socketEditActive = false;
            w.undoCoordinator.pushSocketUndo(std::move(socketEditBefore));
        }
    }

    void PrefabAuthoringPanels::drawIKPanel()
    {
        if (!ctx.chainsLoaded)
        {
            // Pull the controller's chain copies once (so an empty prefab doesn't re-query
            // every frame). Subsequent edits stay in editChains and are pushed back.
            pullEditChains();
            ctx.chainsLoaded = true;
        }

        if (ctx.editChains.empty())
        {
            ImGui::TextDisabled("No IK chains in this prefab.");
            return;
        }

        // Undo bracket: snapshot the chains BEFORE the controls run while no edit session is active.
        if (!chainEditActive)
            chainEditBefore = w.undoCoordinator.snapshotChains();

        bool configChanged = false;  // weight/enabled — cheap live push, no rebuild
        bool bindingChanged = false; // target part/socket — needs a re-resolve (rebuild)

        int clickedChain = -1;       // per-chain selection (wires the dead selectedChainIndex)

        for (int i = 0; i < static_cast<int>(ctx.editChains.size()); ++i)
        {
            auto& chain = ctx.editChains[i];
            ImGui::PushID(i);

            // Per-chain selection: clicking a chain header makes it the active chain. The detail
            // editor (weight/enabled/binding) only shows for the selected chain, so a multi-chain
            // rig is no longer an undifferentiated wall of controls.
            ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_DefaultOpen;
            if (i == ctx.selectedChainIndex) treeFlags |= ImGuiTreeNodeFlags_Selected;
            const bool chainOpen = ImGui::TreeNodeEx(chain.chainName.c_str(), treeFlags);
            if (ImGui::IsItemClicked()) clickedChain = i;

            if (chainOpen)
            {
                ImGui::Text("Tip: %s", chain.tipBoneName.c_str());

                const bool isSelectedChain = (i == ctx.selectedChainIndex);
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
                if (i < static_cast<int>(ctx.rigDesc.ik.size()))
                {
                    auto& ik = ctx.rigDesc.ik[i];
                    ImGui::Separator();
                    ImGui::TextDisabled("Target binding (transient)");

                    const char* partPreview = (ik.targetPartIndex >= 0 &&
                                               ik.targetPartIndex < static_cast<int>(ctx.rigDesc.parts.size()))
                        ? ctx.rigDesc.parts[ik.targetPartIndex].meshPath.c_str()
                        : "None";
                    if (ImGui::BeginCombo("Target part", partPreview))
                    {
                        for (int p = 0; p < static_cast<int>(ctx.rigDesc.parts.size()); ++p)
                        {
                            if (p == ik.bodyPartIndex) continue;
                            std::string label = std::filesystem::path(ctx.rigDesc.parts[p].meshPath).filename().string();
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
                        q.instanceId = ctx.instanceId;
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
            ctx.selectedChainIndex = clickedChain;

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
            w.sandboxController.buildPreviewFromDesc(); // re-assembles with the new rigDesc.ik bindings
            pushEditChains();            // restore live chain configs onto the fresh assembly
            if (ctx.selectedPart >= 0 && !ctx.editSockets.empty())
            {
                pushEditSocketsForPart(ctx.selectedPart);
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
        int bodyPart = (!ctx.rigDesc.ik.empty()) ? ctx.rigDesc.ik.front().bodyPartIndex : -1;
        bool canSave = bodyPart >= 0 && !partMeshPath(bodyPart).empty();
        if (!canSave) ImGui::BeginDisabled();
        if (ImGui::Button("Save IK Chains to Mesh"))
        {
            ikSaveSuccess = types::MeshIKChainWriter::saveIKChainsToMesh(partMeshPath(bodyPart), ctx.editChains);
            ikSaveTimer = prefabdetail::kSaveFeedbackSeconds;
        }
        if (!canSave) ImGui::EndDisabled();
        prefabdetail::drawSaveBadge(ikSaveTimer, ikSaveSuccess);

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
            w.undoCoordinator.pushChainUndo(std::move(chainEditBefore));
        }
    }
}
