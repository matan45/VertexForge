// CPU-only coverage for the render-texture component service get/set mapping
// (VK-1412 / VK-1413):
//   1. getRenderTextureData surfaces the component's *runtime* textureId as
//      RenderTextureData::runtimeTextureId (output-only field). The editor live
//      preview reads this to find the active render-texture in play mode; a
//      dropped mapping would silently break the preview.
//   2. setRenderTextureData round-trips every config field but must NOT clobber
//      the component's textureId — the runtime id is owned by the render adapter,
//      not the editor inspector, so an edit through the service must leave it
//      untouched (even if the incoming DTO carries a stray runtimeTextureId).
//
// All CPU-only: no Vulkan device and no GLFW window are created. The service
// operates on the EntityRegistry singleton + a scene::Entity, never on the
// graphics layer, so a default-constructed scene::SceneGraphSystem suffices.
// Tests must link ECSRegistry (per CLAUDE.md) so the registry singleton resolves
// to one instance — the existing billboard component test already relies on this.

#include <doctest.h>

#include <components/Components.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>

#include <impl/components/RenderTextureComponentService.hpp>
#include <data/EntityConversion.hpp>

#include <rendertexture/RenderTextureTypes.hpp>

#include <memory>

namespace
{
    // Create a fresh entity on the singleton registry. Freshly created entities
    // carry version 0, so the 32-bit-truncating toHandle/fromHandle conversion
    // round-trips exactly.
    scene::Entity makeEntity()
    {
        auto handle = scene::EntityRegistry::getRegistry().create();
        return scene::Entity(handle);
    }

    services::EntityHandle handleOf(const scene::Entity& e)
    {
        return services::internal::toHandle(e.getHandle());
    }
}

TEST_SUITE("RenderTextureComponentService")
{
    TEST_CASE("getRenderTextureData surfaces the component's runtime textureId")
    {
        scene::Entity entity = makeEntity();
        auto& comp = entity.addComponent<components::RenderTextureComponent>();

        // A live render-texture id, as the render adapter would assign in play mode.
        comp.textureId = 4242u;
        comp.width = 1024u;
        comp.height = 768u;
        comp.updateMode = rendertexture::UpdateMode::FixedInterval;
        comp.fixedIntervalSeconds = 0.25f;
        comp.clearColor = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
        comp.priority = 7u;
        comp.enabled = false;
        comp.renderShadows = true;
        comp.tonemap = false;

        services::RenderTextureComponentService service(std::make_shared<scene::SceneGraphSystem>());
        auto data = service.getRenderTextureData(handleOf(entity));

        REQUIRE(data.has_value());
        // The new output-only field is the focus of VK-1412/1413.
        CHECK(data->runtimeTextureId == 4242u);
        // Config fields are carried straight through.
        CHECK(data->width == 1024u);
        CHECK(data->height == 768u);
        CHECK(data->updateMode == static_cast<uint8_t>(rendertexture::UpdateMode::FixedInterval));
        CHECK(data->fixedIntervalSeconds == doctest::Approx(0.25f));
        CHECK(data->clearColor.r == doctest::Approx(0.1f));
        CHECK(data->clearColor.a == doctest::Approx(0.4f));
        CHECK(data->priority == 7u);
        CHECK(data->enabled == false);
        CHECK(data->renderShadows == true);
        // VK-1419: tonemap opt-out flag rides the DTO like renderShadows.
        CHECK(data->tonemap == false);
    }

    TEST_CASE("getRenderTextureData reports the default INVALID id when none is assigned")
    {
        // In edit mode (no live render texture) the component keeps the invalid
        // sentinel; the DTO must surface 0 so the editor preview shows nothing.
        scene::Entity entity = makeEntity();
        entity.addComponent<components::RenderTextureComponent>();

        services::RenderTextureComponentService service(std::make_shared<scene::SceneGraphSystem>());
        auto data = service.getRenderTextureData(handleOf(entity));

        REQUIRE(data.has_value());
        CHECK(data->runtimeTextureId == rendertexture::INVALID_RENDER_TEXTURE_ID);
        CHECK(data->runtimeTextureId == 0u);
    }

    TEST_CASE("setRenderTextureData writes config fields but never clobbers textureId")
    {
        scene::Entity entity = makeEntity();
        auto& comp = entity.addComponent<components::RenderTextureComponent>();
        // Pretend the render adapter already assigned a live id; an inspector edit
        // must not stomp it.
        comp.textureId = 9999u;

        services::RenderTextureData data;
        data.width = 256u;
        data.height = 128u;
        data.updateMode = static_cast<uint8_t>(rendertexture::UpdateMode::OnDemand);
        data.fixedIntervalSeconds = 0.5f;
        data.clearColor = glm::vec4(0.9f, 0.8f, 0.7f, 0.6f);
        data.priority = 3u;
        data.enabled = false;
        data.renderShadows = true;
        data.tonemap = false;
        // A stray runtime id on the inbound DTO must be ignored by the setter.
        data.runtimeTextureId = 1234u;

        services::RenderTextureComponentService service(std::make_shared<scene::SceneGraphSystem>());
        const bool ok = service.setRenderTextureData(handleOf(entity), data);
        REQUIRE(ok);

        const auto& after = entity.getComponent<components::RenderTextureComponent>();
        // textureId is owned by the runtime, not the inspector — must be untouched.
        CHECK(after.textureId == 9999u);
        // Config fields round-trip.
        CHECK(after.width == 256u);
        CHECK(after.height == 128u);
        CHECK(after.updateMode == rendertexture::UpdateMode::OnDemand);
        CHECK(after.fixedIntervalSeconds == doctest::Approx(0.5f));
        CHECK(after.clearColor.r == doctest::Approx(0.9f));
        CHECK(after.clearColor.a == doctest::Approx(0.6f));
        CHECK(after.priority == 3u);
        CHECK(after.enabled == false);
        CHECK(after.renderShadows == true);
        // VK-1419: tonemap opt-out round-trips through the setter (default true -> set false).
        CHECK(after.tonemap == false);
    }

    TEST_CASE("setRenderTextureData on a fresh component leaves textureId at INVALID")
    {
        // setRenderTextureData adds the component if missing; the freshly added
        // component must still carry the invalid sentinel afterwards (the setter
        // does not touch textureId at all).
        scene::Entity entity = makeEntity();

        services::RenderTextureData data;
        data.width = 64u;
        data.priority = 1u;

        services::RenderTextureComponentService service(std::make_shared<scene::SceneGraphSystem>());
        const bool ok = service.setRenderTextureData(handleOf(entity), data);
        REQUIRE(ok);

        REQUIRE(entity.hasComponent<components::RenderTextureComponent>());
        const auto& after = entity.getComponent<components::RenderTextureComponent>();
        CHECK(after.textureId == rendertexture::INVALID_RENDER_TEXTURE_ID);
        CHECK(after.width == 64u);
        CHECK(after.priority == 1u);
    }
}
