#include "EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include <imgui.h>

namespace windows
{
    EntityDetailsPanel::EntityDetailsPanel()
    {
        subscribeToEvents();
    }

    EntityDetailsPanel::~EntityDetailsPanel()
    {
        events::EventDispatcher::instance().unsubscribe(sceneClearedToken);
    }

    void EntityDetailsPanel::subscribeToEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                onSceneCleared();
            });
    }

    void EntityDetailsPanel::onSceneCleared()
    {
        materialDrawer.clearCache();
        audio2DDrawer.clearHandles();
        audio3DDrawer.clearHandles();
    }

    void EntityDetailsPanel::draw()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetSelectedEntityQuery selectedQuery;
        auto selectedHandle = dispatcher.query(selectedQuery).value_or(services::EntityHandle::invalid());

        if (ImGui::Begin("Details"))
        {
            if (selectedHandle.isValid())
            {
                drawDetails(selectedHandle);
            }
        }
        ImGui::End();
    }

    void EntityDetailsPanel::drawDetails(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = handle;
        auto entityDataOpt = dispatcher.query(entityQuery);

        if (!entityDataOpt.has_value())
            return;

        drawEntityName(handle, entityDataOpt->name);
        drawEntityActiveCheckbox(handle, entityDataOpt->isActive, entityDataOpt->isEffectivelyActive);
        ImGui::Separator();

        transformDrawer.draw(handle);
        bool hasCamera = cameraDrawer.draw(handle);
        iblDrawer.draw(handle);
        navmeshRootDrawer.draw(handle);
        worldSectorDrawer.draw(handle);
        bool hasMesh = meshDrawer.draw(handle);

        if (hasMesh)
            materialDrawer.draw(handle);

        bool hasAudio2D = audio2DDrawer.draw(handle);
        bool hasAudio3D = audio3DDrawer.draw(handle);
        bool hasScript = scriptDrawer.draw(handle);
        
        bool hasCollider = colliderDrawer.draw(handle);
        bool hasRigidBody = rigidBodyDrawer.draw(handle);
        bool hasDestructible = destructibleDrawer.draw(handle);
        bool hasPhysicsAnimation = physicsAnimationDrawer.draw(handle);
        bool hasVFX = vfxDrawer.draw(handle);
        bool hasBillboard = billboardDrawer.draw(handle);
        bool hasText = textDrawer.draw(handle);

        bool hasDirectionalLight = directionalLightDrawer.draw(handle);
        bool hasPointLight = pointLightDrawer.draw(handle);
        bool hasSpotLight = spotLightDrawer.draw(handle);

        bool hasSocketAttachment = socketAttachmentDrawer.draw(handle);
        bool hasNavmeshAgent = navmeshAgentDrawer.draw(handle);
        bool hasOffMeshLink = offMeshLinkDrawer.draw(handle);
        bool hasNavmeshObstacle = navmeshObstacleDrawer.draw(handle);
        bool hasNavmeshModifierVolume = navmeshModifierVolumeDrawer.draw(handle);
        bool hasNavInvoker = navInvokerDrawer.draw(handle);
        bool hasVolumetricNavVolume = volumetricVolumeDrawer.draw(handle);
        bool hasVolumetricAgent = volumetricAgentDrawer.draw(handle);
        bool hasController = controllerDrawer.draw(handle);
        bool hasRenderTexture = renderTextureDrawer.draw(handle);
        bool hasIK = ikDrawer.draw(handle);
        bool hasBehaviorTree = behaviorTreeDrawer.draw(handle);
        bool hasDecal = decalDrawer.draw(handle);
        bool hasReverbZone = reverbZoneDrawer.draw(handle);
        bool hasFogVolume = fogVolumeDrawer.draw(handle);

        // Terrain components (read-only display)
        terrainDrawer.draw(handle);
        terrainTileDrawer.draw(handle);

        // Ocean component
        oceanDrawer.draw(handle);

        // UI components
        bool hasUICanvas = uiCanvasDrawer.draw(handle);
        bool hasUIRect = uiRectDrawer.draw(handle);
        bool hasUIImage = uiImageDrawer.draw(handle);
        bool hasUILabel = uiLabelDrawer.draw(handle);
        bool hasUIScroll = uiScrollDrawer.draw(handle);
        bool hasUILayoutGroup = uiLayoutGroupDrawer.draw(handle);
        bool hasUIButton = uiButtonDrawer.draw(handle);
        bool hasUITextInput = uiTextInputDrawer.draw(handle);
        bool hasUICheckbox = uiCheckboxDrawer.draw(handle);
        bool hasUIDropdown = uiDropdownDrawer.draw(handle);
        bool hasUITabs = uiTabsDrawer.draw(handle);
        bool hasUISlider = uiSliderDrawer.draw(handle);
        bool hasUIProgressBar = uiProgressBarDrawer.draw(handle);
        bool hasUIAnimation = uiAnimationDrawer.draw(handle);
        bool hasUIMask = uiMaskDrawer.draw(handle);
        bool hasUIDraggable = uiDraggableDrawer.draw(handle);
        bool hasUIDropTarget = uiDropTargetDrawer.draw(handle);

        // Plugin meta components
        metaComponentDrawer.draw(handle);

        addComponentPopup.draw({handle, hasCamera, hasMesh, hasAudio2D, hasAudio3D, hasScript,
                                hasCollider, hasRigidBody, hasPhysicsAnimation, hasVFX, hasBillboard,
                                hasText, hasDirectionalLight, hasPointLight, hasSpotLight,
                                hasUICanvas, hasUIRect, hasUIImage, hasUILabel,
                                hasUIScroll, hasUILayoutGroup, hasUIButton, hasUITextInput,
                                hasUICheckbox, hasUIDropdown, hasUITabs, hasUISlider,
                                hasUIProgressBar, hasSocketAttachment, hasNavmeshAgent,
                                hasOffMeshLink, hasNavmeshObstacle, hasNavmeshModifierVolume,
                                hasRenderTexture, hasController, hasIK, hasBehaviorTree,
                                hasDecal, hasReverbZone, hasFogVolume, hasUIAnimation, hasUIMask,
                                hasUIDraggable, hasUIDropTarget, hasNavInvoker,
                                hasVolumetricNavVolume, hasVolumetricAgent,
                                hasDestructible});
    }

    void EntityDetailsPanel::drawEntityName(services::EntityHandle handle, const std::string& currentName)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        char buffer[256];
        std::strncpy(buffer, currentName.c_str(), sizeof(buffer));
        buffer[sizeof(buffer) - 1] = '\0';
        if (ImGui::InputText("Name", buffer, sizeof(buffer)))
        {
            events::scene::SetEntityNameCommand cmd;
            cmd.entity = handle;
            cmd.newName = buffer;
            dispatcher.execute(cmd);
        }
    }

    void EntityDetailsPanel::drawEntityActiveCheckbox(services::EntityHandle handle, bool isActive, bool isEffectivelyActive)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        bool parentInactive = isActive && !isEffectivelyActive;
        if (parentInactive)
        {
            ImGui::BeginDisabled();
            bool disabled = false;
            ImGui::Checkbox("Active", &disabled);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Inactive because a parent entity is inactive");
            }
        }
        else
        {
            if (ImGui::Checkbox("Active", &isActive))
            {
                events::scene::SetEntityActiveCommand cmd;
                cmd.entity = handle;
                cmd.isActive = isActive;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("When disabled, the entity and all its components are inactive");
            }
        }
    }

    void EntityDetailsPanel::pushComponentHeaderStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    }

    void EntityDetailsPanel::popComponentHeaderStyle()
    {
        ImGui::PopStyleColor(3);
    }

    void EntityDetailsPanel::pushRemoveButtonStyle()
    {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    }

    void EntityDetailsPanel::popRemoveButtonStyle()
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
}
