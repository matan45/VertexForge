#include <doctest.h>
#include <data/AsyncLoadingTypes.hpp>

// ============================================================
// AsyncLoadingTypes tests - LoadingProgress state machine
// ============================================================

TEST_SUITE("AsyncLoadingTypes") {

// ---- LoadingProgress state tests ----

TEST_CASE("LoadingProgress: default state is Idle") {
    services::LoadingProgress p;
    CHECK(p.state == services::LoadingState::Idle);
    CHECK(p.progress == doctest::Approx(0.0f));
    CHECK_FALSE(p.isLoading());
    CHECK_FALSE(p.isDone());
}

TEST_CASE("LoadingProgress: Pending state") {
    services::LoadingProgress p;
    p.state = services::LoadingState::Pending;
    CHECK(p.isLoading());
    CHECK_FALSE(p.isDone());
}

TEST_CASE("LoadingProgress: Loading state") {
    services::LoadingProgress p;
    p.state = services::LoadingState::Loading;
    CHECK(p.isLoading());
    CHECK_FALSE(p.isDone());
}

TEST_CASE("LoadingProgress: GPUUploadPending state") {
    services::LoadingProgress p;
    p.state = services::LoadingState::GPUUploadPending;
    CHECK(p.isLoading());
    CHECK_FALSE(p.isDone());
}

TEST_CASE("LoadingProgress: Complete state") {
    services::LoadingProgress p;
    p.state = services::LoadingState::Complete;
    CHECK_FALSE(p.isLoading());
    CHECK(p.isDone());
}

TEST_CASE("LoadingProgress: Error state") {
    services::LoadingProgress p;
    p.state = services::LoadingState::Error;
    p.errorMessage = "File not found";
    CHECK_FALSE(p.isLoading());
    CHECK(p.isDone());
    CHECK(p.errorMessage == "File not found");
}

TEST_CASE("LoadingProgress: Cancelled state") {
    services::LoadingProgress p;
    p.state = services::LoadingState::Cancelled;
    CHECK_FALSE(p.isLoading());
    CHECK(p.isDone());
}

// ---- MeshLoadingProgress ----

TEST_CASE("MeshLoadingProgress: inherits from LoadingProgress") {
    services::MeshLoadingProgress mp;
    CHECK(mp.state == services::LoadingState::Idle);
    CHECK_FALSE(mp.isLoading());
    CHECK_FALSE(mp.isDone());
}

TEST_CASE("MeshLoadingProgress: has bytesLoaded and totalBytes") {
    services::MeshLoadingProgress mp;
    CHECK(mp.bytesLoaded == 0);
    CHECK(mp.totalBytes == 0);

    mp.bytesLoaded = 512;
    mp.totalBytes = 1024;
    mp.state = services::LoadingState::Loading;
    mp.progress = 0.5f;

    CHECK(mp.bytesLoaded == 512);
    CHECK(mp.totalBytes == 1024);
    CHECK(mp.isLoading());
    CHECK(mp.progress == doctest::Approx(0.5f));
}

// ---- TextureLoadingProgress ----

TEST_CASE("TextureLoadingProgress: has width, height, isHDR fields") {
    services::TextureLoadingProgress tp;
    CHECK(tp.width == 0);
    CHECK(tp.height == 0);
    CHECK_FALSE(tp.isHDR);
    CHECK(tp.state == services::LoadingState::Idle);
}

TEST_CASE("TextureLoadingProgress: set texture metadata") {
    services::TextureLoadingProgress tp;
    tp.width = 2048;
    tp.height = 1024;
    tp.isHDR = true;
    tp.state = services::LoadingState::Complete;

    CHECK(tp.width == 2048);
    CHECK(tp.height == 1024);
    CHECK(tp.isHDR);
    CHECK(tp.isDone());
}

// ---- LoadingState enum coverage ----

TEST_CASE("LoadingState: all states are distinct") {
    CHECK(services::LoadingState::Idle != services::LoadingState::Pending);
    CHECK(services::LoadingState::Pending != services::LoadingState::Loading);
    CHECK(services::LoadingState::Loading != services::LoadingState::GPUUploadPending);
    CHECK(services::LoadingState::GPUUploadPending != services::LoadingState::Complete);
    CHECK(services::LoadingState::Complete != services::LoadingState::Error);
    CHECK(services::LoadingState::Error != services::LoadingState::Cancelled);
}

} // TEST_SUITE
