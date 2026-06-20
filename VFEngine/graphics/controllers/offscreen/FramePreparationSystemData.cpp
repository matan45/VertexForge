#include "FramePreparationSystem.hpp"
#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/billboard/BillboardTypes.hpp"
#include "../../render/billboard/BillboardPipeline.hpp"
#include "../../render/gpudriven/billboard/BillboardGPUTypes.hpp"
#include "render/FlipbookMath.hpp"
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

            // worldMarker billboards render exclusively through the GPU mesh-shader
            // path (prepareGPUBillboards); skip them here to avoid double-rendering.
            if (billboard.worldMarker) continue;

            if (billboard.editorOnly && !showEditorIcons) continue;

            render::billboard::BillboardRenderData renderData;
            renderData.worldPosition = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.atlasIndex = billboard.getEffectiveAtlasIndex();
            renderData.size = billboard.size;
            renderData.sizeMode = static_cast<uint32_t>(billboard.sizeMode);
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.colorTint = billboard.colorTint;
            renderData.texturePath = billboard.textureRef.resolve();
            renderData.flipbookColumns = billboard.flipbookColumns;
            renderData.flipbookRows = billboard.flipbookRows;
            renderData.flipbookFrameRate = billboard.flipbookFrameRate;
            renderData.scrollU = billboard.scrollU;
            renderData.scrollV = billboard.scrollV;
            renderData.pulseAmplitude = billboard.pulseAmplitude;
            renderData.pulseFrequency = billboard.pulseFrequency;
            renderData.spinSpeed = billboard.spinSpeed;
            renderData.animStartTime = billboard.animStartTime;
            renderData.loopAnimation = billboard.loopAnimation;
            renderData.worldMarker = billboard.worldMarker;

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
            renderData.fontStyle = textComp.fontStyle;

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
        prepareGPUBillboards(ctx);
    }

    void FramePreparationSystem::prepareGPUBillboards(const FrameContext& ctx)
    {
        // worldMarker billboards are routed through the GPU mesh-shader path
        // (frustum-culled, bindless, animated). Markers always fully face the
        // camera (spherical) so health bars / icons read correctly.
        std::vector<render::gpudriven::BillboardInstanceGPU> gpuInstances;
        std::vector<std::string> gpuTexturePaths;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::BillboardComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& billboard = view.get<components::BillboardComponent>(entity);
            // editorOnly==true is the "hidden" state for markers (Billboard::setVisible(false)).
            if (!billboard.worldMarker || billboard.editorOnly) continue;
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            render::gpudriven::BillboardInstanceGPU inst{};
            inst.positionAndScale = glm::vec4(glm::vec3(worldTransform.worldMatrix[3]), 1.0f);
            inst.colorTint = billboard.colorTint;
            inst.entityId = static_cast<uint32_t>(entity);
            inst.size = billboard.size;

            const int cols = static_cast<int>(billboard.flipbookColumns);
            const int rows = static_cast<int>(billboard.flipbookRows);
            const bool hasFlipbook = (cols * rows > 1) && billboard.flipbookFrameRate > 0.0f;
            const bool hasScroll = billboard.scrollU != 0.0f || billboard.scrollV != 0.0f;
            const bool hasSpin = billboard.spinSpeed != 0.0f;
            const bool animated = hasFlipbook || hasScroll || hasSpin;

            inst.flags = 0u; // spherical (full camera-facing) markers
            if (animated)
            {
                inst.flags |= render::gpudriven::FLAG_ANIMATED;
                if (billboard.loopAnimation)
                    inst.flags |= render::gpudriven::FLAG_LOOP;
                // xy = scroll speed, z = animStartTime (anchors play-once and the
                // shader's t origin so CPU/GPU agree), w unused.
                inst.atlasUVRect = glm::vec4(billboard.scrollU, billboard.scrollV,
                                             billboard.animStartTime, 0.0f);
                inst.rotation = billboard.spinSpeed; // rad/sec, time-driven in shader
                inst.flipbookColsRows = render::gpudriven::encodeFlipbookColsRows(
                    billboard.flipbookColumns, billboard.flipbookRows);
                inst.flipbookFrameRate = billboard.flipbookFrameRate;
            }
            else
            {
                inst.atlasUVRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f); // full static UV rect
                inst.rotation = 0.0f;
                inst.flipbookColsRows = render::gpudriven::encodeFlipbookColsRows(1, 1);
                inst.flipbookFrameRate = 0.0f;
            }

            // bindlessTextureIndex is resolved inside the renderer from this path.
            // Render-texture-sourced markers aren't supported on the GPU path yet
            // (they use a separate per-frame RTT view); fall back to default texture.
            std::string texturePath = billboard.textureRef.resolve();
            if (billboard.renderTextureSource != entt::null)
            {
                texturePath.clear();
            }

            gpuInstances.push_back(inst);
            gpuTexturePaths.push_back(std::move(texturePath));
        }

        ctx.renderHandler->setBillboardRenderingEnabled(!gpuInstances.empty());
        ctx.renderHandler->updateBillboards(std::move(gpuInstances), gpuTexturePaths);
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

        // Phase 2: GPU mesh-shader billboards (worldMarker). Kept on the main thread
        // because updateBillboards performs a synchronous Vulkan upload + bindless
        // texture registration, which must not run concurrently with frame submit.
        if (billboardReady)
        {
            prepareGPUBillboards(ctx);
        }
        else
        {
            renderHandler->setBillboardRenderingEnabled(false);
        }

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
            renderData.shape = static_cast<uint32_t>(decal.shape);
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
