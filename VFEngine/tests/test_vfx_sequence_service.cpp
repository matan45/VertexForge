#include <doctest.h>
#include <impl/components/VFXSequenceComponentService.hpp>
#include <events/EventDispatcher.hpp>
#include <events/scene/ComponentMediaEvents.hpp>
#include <data/DTOs.hpp>
#include <data/EntityHandle.hpp>
#include <data/EntityConversion.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>
#include <memory>

// ============================================================
// VK-1438: VFXSequenceComponentService is the DTO/command seam the editor inspector + Add popup,
// the viewport .vfVFXSequence drop, and copy/clone all ride. Drive it through the EventDispatcher
// (the real CQRS path, so this also exercises registerEventHandlers) and verify the full
// Add -> Has -> Set(incl. triggers) -> Get -> Remove cycle plus the auto-attached Particle billboard
// (mirrors VFXComponentService). The ImGui ViewPort/drawer that emit these commands are not
// CPU-testable; the command surface beneath them is.
// ============================================================

TEST_SUITE("VFXSequenceComponentService")
{
    TEST_CASE("Add/Has/Set/Get/Remove via the dispatcher, with auto-billboard attach/detach")
    {
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::VFXSequenceComponentService svc(sceneGraph);
        auto& dispatcher = events::EventDispatcher::instance();
        svc.registerEventHandlers(dispatcher);

        scene::Entity entity("SeqServiceEntity");
        const services::EntityHandle handle = services::internal::toHandle(entity.getHandle());

        // Add -> component present + auto-attached Particle billboard.
        {
            events::scene::AddVFXSequenceComponentCommand cmd;
            cmd.entity = handle;
            CHECK(dispatcher.execute(cmd) == true);
        }
        REQUIRE(entity.hasComponent<components::VFXSequenceComponent>());
        REQUIRE(entity.hasComponent<components::BillboardComponent>());
        CHECK(entity.getComponent<components::BillboardComponent>().iconType ==
              components::BillboardIconType::Particle);

        // Has query.
        {
            events::scene::HasVFXSequenceComponentQuery q;
            q.entity = handle;
            CHECK(dispatcher.query(q) == true);
        }

        // Set DTO (incl. one trigger).
        services::VFXSequenceData data;
        data.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xC0DEull));
        data.autoPlay = true;
        data.loop = true;
        data.socketName = "Muzzle";
        {
            services::VFXSequenceTriggerData td;
            td.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xC0DFull));
            td.eventName = "fire";
            td.socketName = "Barrel";
            data.triggers.push_back(td);
        }
        {
            events::scene::SetVFXSequenceDataCommand cmd;
            cmd.entity = handle;
            cmd.vfxSequenceData = data;
            CHECK(dispatcher.execute(cmd) == true);
        }

        // Get DTO back and compare every field.
        {
            events::scene::GetVFXSequenceDataQuery q;
            q.entity = handle;
            auto got = dispatcher.query(q);
            REQUIRE(got.has_value());
            CHECK(got->sequenceRef.getGUID() == data.sequenceRef.getGUID());
            CHECK(got->autoPlay == true);
            CHECK(got->loop == true);
            CHECK(got->socketName == "Muzzle");
            REQUIRE(got->triggers.size() == 1);
            CHECK(got->triggers[0].eventName == "fire");
            CHECK(got->triggers[0].sequenceRef.getGUID() == data.triggers[0].sequenceRef.getGUID());
            CHECK(got->triggers[0].socketName == "Barrel");
        }

        // The transient runtimeComboId is never written through the DTO path.
        CHECK(entity.getComponent<components::VFXSequenceComponent>().runtimeComboId == 0u);

        // Remove -> component gone + auto-detached billboard.
        {
            events::scene::RemoveVFXSequenceComponentCommand cmd;
            cmd.entity = handle;
            CHECK(dispatcher.execute(cmd) == true);
        }
        CHECK_FALSE(entity.hasComponent<components::VFXSequenceComponent>());
        CHECK_FALSE(entity.hasComponent<components::BillboardComponent>());

        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
    }

    TEST_CASE("setVFXSequenceData on an entity without the component emplaces it")
    {
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::VFXSequenceComponentService svc(sceneGraph);
        auto& dispatcher = events::EventDispatcher::instance();
        svc.registerEventHandlers(dispatcher);

        scene::Entity entity("SeqServiceEmplace");
        const services::EntityHandle handle = services::internal::toHandle(entity.getHandle());

        services::VFXSequenceData data;
        data.sequenceRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xABCDull));
        events::scene::SetVFXSequenceDataCommand cmd;
        cmd.entity = handle;
        cmd.vfxSequenceData = data;
        CHECK(dispatcher.execute(cmd) == true);

        REQUIRE(entity.hasComponent<components::VFXSequenceComponent>());
        CHECK(entity.getComponent<components::VFXSequenceComponent>().sequenceRef.getGUID() ==
              data.sequenceRef.getGUID());

        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
    }
}
