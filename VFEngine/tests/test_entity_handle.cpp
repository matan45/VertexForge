#include <doctest.h>
#include <data/EntityHandle.hpp>
#include <unordered_set>

// ============================================================
// EntityHandle and ComponentTypeId tests
// ============================================================

TEST_SUITE("EntityHandle") {

TEST_CASE("EntityHandle::invalid() returns invalid handle") {
    auto handle = services::EntityHandle::invalid();
    CHECK_FALSE(handle.isValid());
    CHECK(handle.id == services::EntityHandle::INVALID_ID);
}

TEST_CASE("Default-constructed handle is invalid") {
    services::EntityHandle handle;
    CHECK(handle.id == services::EntityHandle::INVALID_ID);
    CHECK_FALSE(handle.isValid());
}

TEST_CASE("Handle with explicit id is valid") {
    services::EntityHandle handle{0};
    CHECK(handle.isValid());
    CHECK(handle.id == 0);
}

TEST_CASE("Handle with nonzero id is valid") {
    services::EntityHandle handle{42};
    CHECK(handle.isValid());
    CHECK(handle.id == 42);
}

TEST_CASE("Equality: same id") {
    services::EntityHandle a{10};
    services::EntityHandle b{10};
    CHECK(a == b);
    CHECK_FALSE(a != b);
}

TEST_CASE("Equality: different id") {
    services::EntityHandle a{10};
    services::EntityHandle b{20};
    CHECK_FALSE(a == b);
    CHECK(a != b);
}

TEST_CASE("Less-than ordering") {
    services::EntityHandle a{5};
    services::EntityHandle b{10};
    CHECK(a < b);
    CHECK_FALSE(b < a);
    CHECK_FALSE(a < a);
}

TEST_CASE("Hash functor: different handles produce different hashes") {
    services::EntityHandle::Hash hasher;

    services::EntityHandle a{0};
    services::EntityHandle b{1};
    services::EntityHandle c{999};

    // While hash collisions are theoretically possible,
    // for these simple cases they should differ
    CHECK(hasher(a) != hasher(b));
    CHECK(hasher(b) != hasher(c));
    CHECK(hasher(a) != hasher(c));
}

TEST_CASE("Hash functor: same handle produces same hash") {
    services::EntityHandle::Hash hasher;
    services::EntityHandle a{42};
    services::EntityHandle b{42};
    CHECK(hasher(a) == hasher(b));
}

TEST_CASE("EntityHandle works in unordered_set") {
    std::unordered_set<services::EntityHandle, services::EntityHandle::Hash> set;
    set.insert(services::EntityHandle{1});
    set.insert(services::EntityHandle{2});
    set.insert(services::EntityHandle{1}); // duplicate

    CHECK(set.size() == 2);
    CHECK(set.count(services::EntityHandle{1}) == 1);
    CHECK(set.count(services::EntityHandle{3}) == 0);
}

// ---- ComponentTypeId ----

TEST_CASE("ComponentTypeId: None == 0") {
    CHECK(static_cast<uint32_t>(services::ComponentTypeId::None) == 0);
}

TEST_CASE("ComponentTypeId: Transform != None") {
    CHECK(services::ComponentTypeId::Transform != services::ComponentTypeId::None);
}

TEST_CASE("ComponentTypeId: distinct values for common types") {
    CHECK(services::ComponentTypeId::Camera != services::ComponentTypeId::Transform);
    CHECK(services::ComponentTypeId::Mesh != services::ComponentTypeId::Material);
    CHECK(services::ComponentTypeId::DirectionalLight != services::ComponentTypeId::PointLight);
}

} // TEST_SUITE
