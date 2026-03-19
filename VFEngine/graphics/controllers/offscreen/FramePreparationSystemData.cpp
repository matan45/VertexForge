#include "FramePreparationSystem.hpp"
#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/billboard/BillboardTypes.hpp"
#include "../../render/billboard/BillboardPipeline.hpp"
#include "../../render/text/TextTypes.hpp"
#include "../../render/text/TextPipeline.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "components/LightTextComponents.hpp"
#include "../../../services/providers/render/IDecalRenderProvider.hpp"
#include "threading/JobSystem.hpp"
#include <glm/gtc/matrix_inverse.hpp>

namespace controllers::offscreen
{
    std::vector<render::billboard::BillboardRenderData> FramePreparationSystem::gatherBillboardData(
        const FrameContext& ctx)
    {
        bool showEditorIcons = !ctx.playModeActive && ctx.showBillboardIcons;
        std::vector<render::billboard::BillboardRenderData> billboardDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::BillboardComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;

            const auto& billboard = view.get<components::BillboardComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (billboard.editorOnly && !showEditorIcons) continue;

            render::billboard::BillboardRenderData renderData;
            renderData.worldPosition = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.atlasIndex = billboard.getEffectiveAtlasIndex();
            renderData.size = billboard.size;
            renderData.sizeMode = static_cast<uint32_t>(billboard.sizeMode);
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.colorTint = billboard.colorTint;
            renderData.texturePath = billboard.textureRef.resolve();

            if (billboard.renderTextureSource != entt::null
                && registry.valid(billboard.renderTextureSource)
                && registry.all_of<components::RenderTextureComponent>(billboard.renderTextureSource))
            {
                const auto& rtt = registry.get<components::RenderTextureComponent>(billboard.renderTextureSource);
                if (rtt.textureId != rendertexture::INVALID_RENDER_TEXTURE_ID)
                    renderData.texturePath = "__rtt_" + std::to_string(rtt.textureId) + "__";
            }

            billboardDrawList.push_back(renderData);
        }

        return billboardDrawList;
    }

    std::vector<render::text::TextRenderData> FramePreparationSystem::gatherTextData(const FrameContext& ctx)
    {
        std::vector<render::text::TextRenderData> textDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::TextComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;

            const auto& textComp = view.get<components::TextComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (!textComp.fontRef.isValid() || textComp.text.empty()) continue;

            render::text::TextRenderData renderData;
            renderData.fontPath = textComp.fontRef.resolve();
            renderData.text = textComp.text;
            renderData.worldPosition = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.fontSize = textComp.fontSize;
            renderData.color = textComp.color;
            renderData.renderMode = 1;
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.lineSpacing = textComp.lineSpacing;
            renderData.letterSpacing = textComp.letterSpacing;
            renderData.maxWidth = textComp.maxWidth;

            textDrawList.push_back(std::move(renderData));
        }

        return textDrawList;
    }

    void FramePreparationSystem::prepareBillboards(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        renderHandler->initBillboardPipeline();
        if (!renderHandler->isBillboardPipelineInitialized())
        {
            renderHandler->setBillboardDrawList({});
            return;
        }
        renderHandler->setBillboardDrawList(gatherBillboardData(ctx));
    }

    void FramePreparationSystem::prepareText(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        renderHandler->initTextPipeline();
        if (!renderHandler->isTextPipelineInitialized())
        {
            renderHandler->setTextDrawList({});
            return;
        }
        renderHandler->setTextDrawList(gatherTextData(ctx));
    }

    void FramePreparationSystem::prepareSceneData(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        prepareMeshes(ctx);

        renderHandler->initBillboardPipeline();
        renderHandler->initTextPipeline();

        bool billboardReady = renderHandler->isBillboardPipelineInitialized();
        bool textReady = renderHandler->isTextPipelineInitialized();

        static bool storageAssured = false;
        if (!storageAssured)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            registry.storage<components::BillboardComponent>();
            registry.storage<components::WorldTransformComponent>();
            registry.storage<components::NameComponent>();
            registry.storage<components::RenderTextureComponent>();
            registry.storage<components::TextComponent>();
            registry.storage<components::TransformComponent>();
            registry.storage<components::MeshComponent>();
            registry.storage<components::DecalComponent>();
            storageAssured = true;
        }

        auto& jobs = threading::JobSystem::instance();

        auto billboardFuture = jobs.submit([&]() -> std::vector<render::billboard::BillboardRenderData> {
            return billboardReady ? gatherBillboardData(ctx) : std::vector<render::billboard::BillboardRenderData>{};
        }, threading::JobPriority::HIGH);

        auto textFuture = jobs.submit([&]() -> std::vector<render::text::TextRenderData> {
            return textReady ? gatherTextData(ctx) : std::vector<render::text::TextRenderData>{};
        }, threading::JobPriority::HIGH);

        renderHandler->setBillboardDrawList(billboardFuture.get());
        renderHandler->setTextDrawList(textFuture.get());

        prepareDecals(ctx);
    }

    void FramePreparationSystem::prepareDecals(const FrameContext& ctx)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::DecalComponent, components::WorldTransformComponent>();

        std::vector<services::DecalRenderData> decalDrawList;

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;

            const auto& decal = view.get<components::DecalComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            glm::mat4 worldMatrix = worldTransform.worldMatrix;
            glm::vec3 axisX = glm::normalize(glm::vec3(worldMatrix[0]));
            glm::vec3 axisY = glm::normalize(glm::vec3(worldMatrix[1]));
            glm::vec3 axisZ = glm::normalize(glm::vec3(worldMatrix[2]));

            glm::mat4 decalWorldMatrix = glm::mat4(1.0f);
            decalWorldMatrix[0] = glm::vec4(axisX * decal.halfExtents.x, 0.0f);
            decalWorldMatrix[1] = glm::vec4(axisY * decal.halfExtents.y, 0.0f);
            decalWorldMatrix[2] = glm::vec4(axisZ * decal.halfExtents.z, 0.0f);
            decalWorldMatrix[3] = worldMatrix[3];

            services::DecalRenderData renderData;
            renderData.worldMatrix = decalWorldMatrix;
            renderData.inverseWorldMatrix = glm::inverse(decalWorldMatrix);
            renderData.halfExtents = decal.halfExtents;
            renderData.albedoTexture = decal.albedoTextureRef.resolve();
            renderData.normalTexture = decal.normalTextureRef.resolve();
            renderData.ormTexture = decal.ormTextureRef.resolve();
            renderData.color = decal.color;
            renderData.angleFadeStart = decal.angleFadeStart;
            renderData.angleFadeEnd = decal.angleFadeEnd;
            renderData.edgeFalloff = decal.edgeFalloff;
            renderData.sortPriority = decal.sortPriority;
            renderData.modifyNormals = decal.modifyNormals;
            renderData.normalStrength = decal.normalStrength;

            decalDrawList.push_back(std::move(renderData));
        }

        ctx.renderHandler->setDecalDrawList(decalDrawList);
    }
}
