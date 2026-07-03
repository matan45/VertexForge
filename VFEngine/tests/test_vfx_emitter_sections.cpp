#include <doctest.h>
#include <vfx/VFXTypes.hpp>
#include <vfx/VFXEmitterSections.hpp>
#include <vfx/VFXAsset.hpp>
#include <algorithm>
#include <string>
#include <vector>

// VFX emitter property-panel "module" registry: which sections are shown is stored
// per-emitter in VFXNode::enabledSections; pre-existing assets (no saved list) are
// migrated by auto-detecting which sections hold non-default values.

using namespace vfx;

namespace
{
    bool has(const std::vector<std::string>& v, const char* id)
    {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
}

TEST_CASE("EmitterSections: a fully-default emitter auto-detects to no modules (Core only)")
{
    VFXData data = VFXAsset::createDefault("t");
    const VFXNode* emitter = data.graph.findEmitterNode();
    REQUIRE(emitter != nullptr);

    VFXNode node = *emitter; // all properties at their defaults
    autoDetectEnabledSections(node);
    CHECK(node.enabledSections.empty());
}

TEST_CASE("EmitterSections: auto-detect shows only the configured sections")
{
    VFXData data = VFXAsset::createDefault("t");
    const VFXNode* emitter = data.graph.findEmitterNode();
    REQUIRE(emitter != nullptr);

    VFXNode node = *emitter;
    node.properties["flipbookFrameRate"].value = 12.0f; // configure flipbook
    node.properties["distortionEnabled"].value = true;  // enable distortion

    autoDetectEnabledSections(node);

    CHECK(has(node.enabledSections, "flipbook"));
    CHECK(has(node.enabledSections, "distortion"));
    CHECK_FALSE(has(node.enabledSections, "ribbon"));
    CHECK_FALSE(has(node.enabledSections, "events"));
    CHECK_FALSE(has(node.enabledSections, "collision"));
    CHECK_FALSE(has(node.enabledSections, "spawnVariance"));
}

TEST_CASE("EmitterSections: sectionEnabled membership")
{
    VFXNode node;
    node.enabledSections = {"flipbook", "lighting"};
    CHECK(sectionEnabled(node, "flipbook"));
    CHECK(sectionEnabled(node, "lighting"));
    CHECK_FALSE(sectionEnabled(node, "ribbon"));
    CHECK_FALSE(sectionEnabled(node, "core")); // core is never a registry section
}

TEST_CASE("EmitterSections: sectionHasContent detects per-section non-default values")
{
    VFXNode node; // empty properties -> everything at default

    CHECK_FALSE(sectionHasContent(node, "flipbook"));
    CHECK_FALSE(sectionHasContent(node, "uvScroll"));
    CHECK_FALSE(sectionHasContent(node, "collision"));

    node.properties["uvScrollSpeedU"].value = 0.5f;
    CHECK(sectionHasContent(node, "uvScroll"));

    node.properties["flipbookColumns"].value = 4;
    CHECK(sectionHasContent(node, "flipbook"));

    node.properties["collisionEnabled"].value = true;
    CHECK(sectionHasContent(node, "collision"));

    // A section whose keys are absent stays "no content".
    CHECK_FALSE(sectionHasContent(node, "ribbon"));
}

TEST_CASE("EmitterSections: registry ids are unique and exclude core")
{
    std::vector<std::string> ids;
    for (const auto& s : kEmitterSections)
    {
        CHECK(std::string(s.id) != "core");
        CHECK(std::find(ids.begin(), ids.end(), s.id) == ids.end()); // no duplicates
        ids.emplace_back(s.id);
    }
    CHECK(ids.size() == kEmitterSections.size());
}
