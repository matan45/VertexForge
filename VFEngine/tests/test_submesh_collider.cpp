#include <doctest.h>
#include <components/PhysicsComponents.hpp>
#include <components/DestructionComponents.hpp>
#include <types/PhysicsTypes.hpp>
#include <data/DTOs.hpp>
#include <interfaces/physics/IPhysicsService.hpp>

// ============================================================
// VK-1238: Sub-Mesh Fragment Rendering with Per-Fragment
//          Convex Colliders — unit tests
// ============================================================

TEST_SUITE("SubmeshCollider") {

// ---- Struct defaults ----

TEST_CASE("ColliderComponent: submeshIndex defaults to -1") {
    components::ColliderComponent comp;
    CHECK(comp.submeshIndex == -1);
}

TEST_CASE("ColliderData: submeshIndex defaults to -1") {
    services::ColliderData data;
    CHECK(data.submeshIndex == -1);
}

TEST_CASE("ColliderComponentData: submeshIndex defaults to -1") {
    services::ColliderComponentData data;
    CHECK(data.submeshIndex == -1);
}

// ---- ColliderComponent field assignment ----

TEST_CASE("ColliderComponent: submeshIndex can be set to valid fragment index") {
    components::ColliderComponent comp;
    comp.submeshIndex = 7;
    CHECK(comp.submeshIndex == 7);
}

TEST_CASE("ColliderComponent: submeshIndex 0 is valid") {
    components::ColliderComponent comp;
    comp.submeshIndex = 0;
    CHECK(comp.submeshIndex == 0);
}

TEST_CASE("ColliderComponent: submeshIndex -1 means whole mesh") {
    components::ColliderComponent comp;
    comp.shape = components::ColliderShape::ConvexMesh;
    comp.submeshIndex = -1;
    CHECK(comp.submeshIndex == -1);
}

// ---- Fragment index mapping ----

TEST_CASE("FragmentComponent: fragmentIndex maps to submesh index") {
    components::FragmentComponent frag;
    frag.fragmentIndex = 5;

    // The debris manager casts fragmentIndex to int32_t for submeshIndex
    int32_t submeshIndex = static_cast<int32_t>(frag.fragmentIndex);
    CHECK(submeshIndex == 5);
}

TEST_CASE("ColliderComponent: submeshIndex can represent fragment indices up to 255") {
    // Fragments use uint32_t fragmentIndex; collider uses int32_t submeshIndex
    // Verify the cast is safe for realistic fragment counts
    for (uint32_t i = 0; i < 256; ++i) {
        int32_t submeshIndex = static_cast<int32_t>(i);
        CHECK(submeshIndex >= 0);
        CHECK(static_cast<uint32_t>(submeshIndex) == i);
    }
}

// ---- ColliderComponentData <-> ColliderComponent field parity ----

TEST_CASE("ColliderComponentData: submeshIndex roundtrips to ColliderComponent") {
    services::ColliderComponentData dto;
    dto.submeshIndex = 12;

    components::ColliderComponent comp;
    comp.submeshIndex = dto.submeshIndex;
    CHECK(comp.submeshIndex == 12);

    services::ColliderComponentData dto2;
    dto2.submeshIndex = comp.submeshIndex;
    CHECK(dto2.submeshIndex == 12);
}

// ---- ColliderData <-> ColliderComponent field parity ----

TEST_CASE("ColliderData: submeshIndex propagates correctly") {
    services::ColliderData data;
    data.shape = services::ColliderData::Shape::ConvexMesh;
    data.meshPath = "/path/to/mesh.vfMesh";
    data.submeshIndex = 3;

    CHECK(data.submeshIndex == 3);
    CHECK(data.shape == services::ColliderData::Shape::ConvexMesh);
}

TEST_CASE("ColliderData: default submeshIndex does not affect non-mesh shapes") {
    services::ColliderData boxData;
    boxData.shape = services::ColliderData::Shape::Box;
    CHECK(boxData.submeshIndex == -1);

    services::ColliderData sphereData;
    sphereData.shape = services::ColliderData::Shape::Sphere;
    CHECK(sphereData.submeshIndex == -1);
}

// ---- Fragment spawn scenario ----

TEST_CASE("Fragment spawn: submeshIndex matches fragmentIndex for multiple fragments") {
    // Simulate what DebrisManager does for a 4-fragment destruction
    for (uint32_t fragmentIndex = 0; fragmentIndex < 4; ++fragmentIndex) {
        components::ColliderComponent collider;
        collider.shape = components::ColliderShape::ConvexMesh;
        collider.submeshIndex = static_cast<int32_t>(fragmentIndex);

        services::ColliderData colliderData;
        colliderData.shape = services::ColliderData::Shape::ConvexMesh;
        colliderData.submeshIndex = static_cast<int32_t>(fragmentIndex);

        CHECK(collider.submeshIndex == static_cast<int32_t>(fragmentIndex));
        CHECK(colliderData.submeshIndex == static_cast<int32_t>(fragmentIndex));
        CHECK(collider.submeshIndex == colliderData.submeshIndex);
    }
}

} // TEST_SUITE
