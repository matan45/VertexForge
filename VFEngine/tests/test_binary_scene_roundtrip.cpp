// VK-1538 AC#1 — the binary scene blob and the JSON scene must produce
// identical registries. This drives the REAL shipped pipeline:
//   editor save    -> SceneSerialization::saveScene (JSON, source of truth)
//   game export    -> BinarySceneSerialization::convertJsonToBinary (VFBS blob)
//   runtime load   -> loadSceneInto auto-detects the blob by magic sniff
// and asserts the JSON-loaded and blob-loaded scenes serialize identically.
//
// Oracle: createSnapshot() -> nlohmann::json, compared with operator== (a deep
// compare). Known limit: it compares SERIALIZED form, so a component that never
// serializes is invisible to it — but that gap is symmetric across both paths
// (a pre-existing serialization hole, not a blob-vs-JSON divergence), so it
// cannot hide a divergence this test is meant to catch.
//
// EntityRegistry is a process-wide singleton, so each load runs in its own scope
// and is snapshotted to a detached json before the next load clears the registry.
// CPU-only: SceneGraphSystem is constructible without a Vulkan device or window.

#include <doctest.h>

#include <serialization/SceneSerialization.hpp>
#include <serialization/BinarySceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <functional>
#include <iterator>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path roundtripTestRoot()
    {
        return fs::temp_directory_path() / "vf_binary_scene_roundtrip_tests";
    }

    void resetTestRoot()
    {
        std::error_code ec;
        fs::remove_all(roundtripTestRoot(), ec);
        fs::create_directories(roundtripTestRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }

    std::vector<uint8_t> readBytes(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>());
    }

    // Build a scene covering the AC#1 risk surface: nested children, active and
    // inactive entities, float-dense transforms, an explicit animated billboard,
    // a camera, and two lights with NO explicit billboard (exercises the
    // last-writer billboard-defaulting ladder: pointLight beats directionalLight).
    void buildRiskSurfaceScene(scene::SceneGraphSystem& graph)
    {
        scene::Entity& root = graph.GetRoot();

        scene::Entity unit("Unit");
        auto& unitTf = unit.getComponent<components::TransformComponent>();
        unitTf.position = glm::vec3(1.5f, -2.25f, 3.125f);
        unitTf.scale = glm::vec3(0.5f, 2.0f, 1.0f);
        unit.addComponent<components::PointLightComponent>();
        unit.addComponent<components::DirectionalLightComponent>();
        graph.addChild(root, unit);

        scene::Entity turret("Turret");
        turret.getComponent<components::NameComponent>().isActive = false; // inactive path
        auto& turretTf = turret.getComponent<components::TransformComponent>();
        turretTf.position = glm::vec3(-10.0f, 0.0f, 7.5f);
        auto& bb = turret.addOrReplaceComponent<components::BillboardComponent>();
        bb.iconType = components::BillboardIconType::Billboard;
        bb.editorOnly = false;
        bb.flipbookColumns = 4u;
        bb.flipbookRows = 8u;
        bb.scrollU = 0.5f;
        bb.worldMarker = true;
        graph.addChild(unit, turret); // grandchild -> two-level nesting

        scene::Entity marker("Marker");
        marker.getComponent<components::TransformComponent>().position = glm::vec3(0.0f, 100.0f, 0.0f);
        marker.addComponent<components::CameraComponent>();
        graph.addChild(root, marker);
    }
}

TEST_SUITE("BinarySceneRoundtrip")
{
    TEST_CASE("JSON-load and blob-load produce identical registries")
    {
        resetTestRoot();

        const fs::path jsonPath = roundtripTestRoot() / "Roundtrip.vfScene";
        const fs::path blobPath = roundtripTestRoot() / "Roundtrip.vfscene.bin";

        // Editor save (JSON), then export conversion (binary blob).
        {
            scene::SceneGraphSystem builder;
            buildRiskSurfaceScene(builder);
            REQUIRE(serialization::SceneSerialization::saveScene(builder, jsonPath.string()));
            builder.clearScene();
        }
        REQUIRE(serialization::BinarySceneSerialization::convertJsonToBinary(
            jsonPath.string(), blobPath.string(), /*sourceHash*/ 0u));

        // The blob really is binary (proves the runtime load below takes the
        // msgpack branch, not a silent JSON fallback).
        const std::vector<uint8_t> blobBytes = readBytes(blobPath);
        REQUIRE(serialization::BinarySceneSerialization::isBinaryScene(blobBytes));

        json snapFromJson;
        {
            scene::SceneGraphSystem g;
            REQUIRE(serialization::SceneSerialization::loadSceneInto(jsonPath.string(), g));
            snapFromJson = serialization::SceneSerialization::createSnapshot(g);
            g.clearScene();
        }

        json snapFromBlob;
        {
            scene::SceneGraphSystem g;
            REQUIRE(serialization::SceneSerialization::loadSceneInto(blobPath.string(), g));
            snapFromBlob = serialization::SceneSerialization::createSnapshot(g);
            g.clearScene();
        }

        // AC#1: identical registries (deep JSON compare over the whole scene).
        CHECK(snapFromJson == snapFromBlob);

        // Type-sensitive fields that drift SILENTLY (no error) if a msgpack type
        // changes: uuid must stay an unsigned int, isActive a bool. Assert against
        // the (equal) JSON snapshot so a failure here localizes the culprit.
        REQUIRE(snapFromJson.contains("root"));
        const json& rootJson = snapFromJson["root"];
        REQUIRE(rootJson.contains("uuid"));
        CHECK(rootJson["uuid"].is_number_unsigned());

        REQUIRE(rootJson.contains("children"));
        bool sawInactive = false;
        bool sawActive = false;
        std::function<void(const json&)> walk = [&](const json& e)
        {
            if (e.contains("uuid")) CHECK(e["uuid"].is_number_unsigned());
            if (e.contains("isActive"))
            {
                CHECK(e["isActive"].is_boolean());
                if (e["isActive"].get<bool>()) sawActive = true; else sawInactive = true;
            }
            if (e.contains("children") && e["children"].is_array())
                for (const json& c : e["children"]) walk(c);
        };
        walk(rootJson);
        CHECK(sawActive);
        CHECK(sawInactive); // the "Turret" grandchild
    }

    TEST_CASE("blob header carries the source hash and current format version")
    {
        resetTestRoot();

        const fs::path jsonPath = roundtripTestRoot() / "Header.vfScene";
        const fs::path blobPath = roundtripTestRoot() / "Header.vfscene.bin";

        {
            scene::SceneGraphSystem builder;
            buildRiskSurfaceScene(builder);
            REQUIRE(serialization::SceneSerialization::saveScene(builder, jsonPath.string()));
            builder.clearScene();
        }

        const uint64_t kSourceHash = 0xABCDEF1234567890ull;
        REQUIRE(serialization::BinarySceneSerialization::convertJsonToBinary(
            jsonPath.string(), blobPath.string(), kSourceHash));

        const std::vector<uint8_t> blobBytes = readBytes(blobPath);
        serialization::BinarySceneSerialization::Header header{};
        REQUIRE(serialization::BinarySceneSerialization::peekHeader(blobBytes, header));

        // AC#3 (unit level): the blob is self-describing — a mismatched hash or an
        // older version can be detected without decoding the payload, which is how
        // the exporter invalidates a stale _temp_scenes blob.
        CHECK(header.version == serialization::BinarySceneSerialization::FORMAT_VERSION);
        CHECK(header.sourceHash == kSourceHash);
        CHECK(header.entityCount == 4u); // root + Unit + Turret + Marker
    }
}
