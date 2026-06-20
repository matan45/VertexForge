// CPU-only coverage for the billboard-animation feature (Phase 1-3):
//   1. BillboardComponent <-> services::BillboardData DTO mapping (both directions),
//      pinning that every animation field survives get/setBillboardData. A dropped
//      field in BillboardComponentService.cpp would silently break the editor's
//      animation controls; this is the regression net for that mapping.
//   2. setBillboardData's editorOnly early-return contract — the reason Phase 3
//      script natives mutate the registry component directly instead of routing
//      through the service.
//   3. BillboardInstanceData CPU vertex layout: the new animParams0/animParams1/
//      animStartTime attributes (locations 6/7/8) must not overlap colorTint
//      (location 5) and the 7 attribute descriptions must have monotonic,
//      non-overlapping, in-stride offsets. Mirrors the 80-byte GPU layout test.
//
// All CPU-only: no Vulkan device and no GLFW window are created. The service is
// constructed with a default scene::SceneGraphSystem (the get/setBillboardData
// methods operate on the EntityRegistry singleton + a scene::Entity, never on the
// graphics layer), and entities are created directly on that singleton registry.

#include <doctest.h>

#include <components/Components.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>

#include <impl/components/BillboardComponentService.hpp>
#include <data/EntityConversion.hpp>

#include <render/billboard/BillboardTypes.hpp>

#include <cstddef>
#include <memory>

namespace
{
    // Create a fresh entity on the singleton registry and return both the scene
    // wrapper and the services::EntityHandle the service expects. Freshly created
    // entities carry version 0, so the 32-bit-truncating toHandle/fromHandle
    // conversion round-trips exactly.
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

TEST_SUITE("BillboardComponentService")
{
    TEST_CASE("getBillboardData copies every animation field off the component")
    {
        scene::Entity entity = makeEntity();
        auto& comp = entity.addComponent<components::BillboardComponent>();
        // Must be a user billboard or getBillboardData returns nullopt.
        comp.editorOnly = false;

        // Distinct non-default values so a copy-paste mistake in the mapping
        // (e.g. assigning scrollU twice) is caught.
        comp.size = glm::vec2(3.0f, 7.0f);
        comp.colorTint = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
        comp.flipbookColumns = 6u;
        comp.flipbookRows = 5u;
        comp.flipbookFrameRate = 12.5f;
        comp.scrollU = 0.25f;
        comp.scrollV = -0.5f;
        comp.pulseAmplitude = 0.75f;
        comp.pulseFrequency = 3.0f;
        comp.spinSpeed = 1.5f;
        comp.animStartTime = 42.0f;
        comp.worldMarker = true;

        services::BillboardComponentService service(std::make_shared<scene::SceneGraphSystem>());
        auto data = service.getBillboardData(handleOf(entity));

        REQUIRE(data.has_value());
        CHECK(data->size.x == doctest::Approx(3.0f));
        CHECK(data->size.y == doctest::Approx(7.0f));
        CHECK(data->colorTint.r == doctest::Approx(0.1f));
        CHECK(data->colorTint.a == doctest::Approx(0.4f));
        CHECK(data->flipbookColumns == 6u);
        CHECK(data->flipbookRows == 5u);
        CHECK(data->flipbookFrameRate == doctest::Approx(12.5f));
        CHECK(data->scrollU == doctest::Approx(0.25f));
        CHECK(data->scrollV == doctest::Approx(-0.5f));
        CHECK(data->pulseAmplitude == doctest::Approx(0.75f));
        CHECK(data->pulseFrequency == doctest::Approx(3.0f));
        CHECK(data->spinSpeed == doctest::Approx(1.5f));
        CHECK(data->animStartTime == doctest::Approx(42.0f));
        CHECK(data->worldMarker == true);
    }

    TEST_CASE("setBillboardData writes every animation field onto a user billboard")
    {
        scene::Entity entity = makeEntity();
        auto& comp = entity.addComponent<components::BillboardComponent>();
        comp.editorOnly = false; // user billboard so set is not rejected

        services::BillboardData data;
        data.size = glm::vec2(9.0f, 11.0f);
        data.colorTint = glm::vec4(0.9f, 0.8f, 0.7f, 0.6f);
        data.flipbookColumns = 4u;
        data.flipbookRows = 8u;
        data.flipbookFrameRate = 24.0f;
        data.scrollU = -0.1f;
        data.scrollV = 0.2f;
        data.pulseAmplitude = 0.33f;
        data.pulseFrequency = 6.0f;
        data.spinSpeed = -2.0f;
        data.animStartTime = 100.0f;
        data.worldMarker = true;

        services::BillboardComponentService service(std::make_shared<scene::SceneGraphSystem>());
        const bool ok = service.setBillboardData(handleOf(entity), data);
        REQUIRE(ok);

        const auto& after = entity.getComponent<components::BillboardComponent>();
        CHECK(after.size.x == doctest::Approx(9.0f));
        CHECK(after.size.y == doctest::Approx(11.0f));
        CHECK(after.colorTint.r == doctest::Approx(0.9f));
        CHECK(after.colorTint.a == doctest::Approx(0.6f));
        CHECK(after.flipbookColumns == 4u);
        CHECK(after.flipbookRows == 8u);
        CHECK(after.flipbookFrameRate == doctest::Approx(24.0f));
        CHECK(after.scrollU == doctest::Approx(-0.1f));
        CHECK(after.scrollV == doctest::Approx(0.2f));
        CHECK(after.pulseAmplitude == doctest::Approx(0.33f));
        CHECK(after.pulseFrequency == doctest::Approx(6.0f));
        CHECK(after.spinSpeed == doctest::Approx(-2.0f));
        CHECK(after.animStartTime == doctest::Approx(100.0f));
        CHECK(after.worldMarker == true);
    }

    TEST_CASE("setBillboardData early-returns false for an editorOnly billboard and leaves it untouched")
    {
        // Pins WHY Phase 3 script natives mutate the registry component directly:
        // the service refuses to touch editor-icon (editorOnly) billboards.
        scene::Entity entity = makeEntity();
        auto& comp = entity.addComponent<components::BillboardComponent>();
        comp.editorOnly = true; // editor icon, not a user billboard
        comp.flipbookColumns = 1u;
        comp.spinSpeed = 0.0f;

        services::BillboardData data;
        data.flipbookColumns = 99u; // would be written if the guard were missing
        data.spinSpeed = 5.0f;

        services::BillboardComponentService service(std::make_shared<scene::SceneGraphSystem>());
        const bool ok = service.setBillboardData(handleOf(entity), data);

        CHECK_FALSE(ok);
        const auto& after = entity.getComponent<components::BillboardComponent>();
        CHECK(after.flipbookColumns == 1u); // untouched
        CHECK(after.spinSpeed == doctest::Approx(0.0f));
    }

    TEST_CASE("getBillboardData returns nullopt for an editorOnly billboard")
    {
        scene::Entity entity = makeEntity();
        auto& comp = entity.addComponent<components::BillboardComponent>();
        comp.editorOnly = true;

        services::BillboardComponentService service(std::make_shared<scene::SceneGraphSystem>());
        CHECK_FALSE(service.getBillboardData(handleOf(entity)).has_value());
    }
}

TEST_SUITE("BillboardInstanceDataLayout")
{
    using render::billboard::BillboardInstanceData;

    TEST_CASE("animation attributes do not overlap colorTint")
    {
        // colorTint is a vec4 at location 5; the animParams0 block (location 6)
        // must begin at or after colorTint's end. A struct reorder that places an
        // animation field inside colorTint's bytes would silently corrupt tints.
        const size_t colorTintBegin = offsetof(BillboardInstanceData, colorTint);
        const size_t colorTintEnd = colorTintBegin + sizeof(glm::vec4);

        CHECK(offsetof(BillboardInstanceData, animParams0) >= colorTintEnd);
        CHECK(offsetof(BillboardInstanceData, animParams1) >=
              offsetof(BillboardInstanceData, animParams0) + sizeof(glm::vec4));
        CHECK(offsetof(BillboardInstanceData, animStartTime) >=
              offsetof(BillboardInstanceData, animParams1) + sizeof(glm::vec4));
    }

    TEST_CASE("getAttributeDescriptions has 7 monotonic non-overlapping in-stride entries")
    {
        const auto attrs = BillboardInstanceData::getAttributeDescriptions();
        REQUIRE(attrs.size() == 7);

        const auto binding = BillboardInstanceData::getBindingDescription();
        const uint32_t stride = binding.stride;
        CHECK(stride == sizeof(BillboardInstanceData));

        // Locations are 2..8 and offsets strictly increase, each staying inside
        // the instance stride.
        for (size_t i = 0; i < attrs.size(); ++i)
        {
            CHECK(static_cast<uint32_t>(attrs[i].location) == static_cast<uint32_t>(i + 2));
            CHECK(static_cast<uint32_t>(attrs[i].binding) == 1u);
            CHECK(attrs[i].offset < stride);
            if (i > 0)
            {
                CHECK(attrs[i].offset > attrs[i - 1].offset);
            }
        }

        // The last three attributes are the animation block, sourced from the
        // matching struct members.
        CHECK(attrs[4].offset == offsetof(BillboardInstanceData, animParams0));
        CHECK(attrs[5].offset == offsetof(BillboardInstanceData, animParams1));
        CHECK(attrs[6].offset == offsetof(BillboardInstanceData, animStartTime));
    }
}
