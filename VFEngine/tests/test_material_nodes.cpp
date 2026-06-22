#include <doctest.h>
#include <nlohmann/json.hpp>
#include <material/MaterialTypes.hpp>
#include <material/MaterialAsset.hpp>
#include <render/FlipbookMath.hpp>
#include <graph/nodes/UtilityNodes.hpp>

// ============================================================
// Utility shader-graph node types (format 1.1 additions)
// ============================================================

TEST_SUITE("MaterialNodeTypes") {

TEST_CASE("new utility node types round-trip through string serialization") {
    const material::NodeType newTypes[] = {
        material::NodeType::WorldPosition,
        material::NodeType::Panner,
        material::NodeType::UVTransform,
        material::NodeType::Remap,
        material::NodeType::Flipbook,
        material::NodeType::Rotator,
        material::NodeType::CustomRotator,
    };

    for (material::NodeType type : newTypes) {
        std::string str = material::nodeTypeToString(type);
        CHECK(str != "Unknown");
        CHECK(material::stringToNodeType(str) == type);
    }

    CHECK(material::nodeTypeToString(material::NodeType::WorldPosition) == "WorldPosition");
    CHECK(material::nodeTypeToString(material::NodeType::Panner) == "Panner");
    CHECK(material::nodeTypeToString(material::NodeType::UVTransform) == "UVTransform");
    CHECK(material::nodeTypeToString(material::NodeType::Remap) == "Remap");
    CHECK(material::nodeTypeToString(material::NodeType::Flipbook) == "Flipbook");
    CHECK(material::nodeTypeToString(material::NodeType::Rotator) == "Rotator");
    CHECK(material::nodeTypeToString(material::NodeType::CustomRotator) == "CustomRotator");
}

TEST_CASE("every node type has a serialized name (no silent Unknown)") {
    // CustomRotator is the last enum value; iterating to it covers the whole inventory
    const auto last = static_cast<int>(material::NodeType::CustomRotator);
    for (int i = 0; i <= last; ++i) {
        auto type = static_cast<material::NodeType>(i);
        std::string str = material::nodeTypeToString(type);
        CHECK_MESSAGE(str != "Unknown", "enum value ", i, " has no string mapping");
        CHECK(material::stringToNodeType(str) == type);
    }
}

TEST_CASE("unknown node-type strings fall back to ConstantScalar") {
    CHECK(material::stringToNodeType("SomeFutureNode") == material::NodeType::ConstantScalar);
    CHECK(material::stringToNodeType("") == material::NodeType::ConstantScalar);
}

// ============================================================
// VK-1429 — Flipbook / Rotator UV-animation nodes
// ============================================================

TEST_CASE("Flipbook/Rotator float + vec2 properties survive serialize/deserialize") {
    using material::MaterialAsset;

    // Counts/fps exceed [0,1] — they must NOT be clamped by serialization (the clamp lives only
    // in the generic property-panel widget, which is bypassed by the dedicated UI block).
    const std::pair<std::string, float> floatProps[] = {
        {"columns", 4.0f}, {"rows", 4.0f}, {"framesPerSecond", 30.0f},
        {"loop", 0.0f}, {"totalFrames", 12.0f}, {"rotationSpeed", 2.5f},
    };
    for (const auto& [key, value] : floatProps) {
        material::NodeProperty prop = value;
        auto json = MaterialAsset::serializeProperty(prop);
        auto back = MaterialAsset::deserializeProperty(json, key);
        REQUIRE(std::holds_alternative<float>(back));
        CHECK(std::get<float>(back) == doctest::Approx(value));
    }

    // Rotator "center" (vec2) round-trips too.
    material::NodeProperty center = glm::vec2(0.5f, 0.5f);
    auto centerBack = MaterialAsset::deserializeProperty(MaterialAsset::serializeProperty(center), "center");
    REQUIRE(std::holds_alternative<glm::vec2>(centerBack));
    CHECK(std::get<glm::vec2>(centerBack).x == doctest::Approx(0.5f));
    CHECK(std::get<glm::vec2>(centerBack).y == doctest::Approx(0.5f));
}

TEST_CASE("Flipbook cell selection matches computeFlipbookFrame") {
    // fps=30, 4x4, loop, time=0.1 -> frame 3 -> col 3, row 0
    auto f = render::computeFlipbookFrame(0.1f, 30.0f, 4, 4, true);
    CHECK(f.uvScale.x == doctest::Approx(0.25f));
    CHECK(f.uvScale.y == doctest::Approx(0.25f));
    CHECK(f.uvOffset.x == doctest::Approx(0.75f)); // col 3 * 0.25
    CHECK(f.uvOffset.y == doctest::Approx(0.0f));  // row 0

    // play-once holds the last tile: time well past the end -> frame 15 -> col 3, row 3
    auto last = render::computeFlipbookFrame(100.0f, 30.0f, 4, 4, false);
    CHECK(last.uvOffset.x == doctest::Approx(0.75f));
    CHECK(last.uvOffset.y == doctest::Approx(0.75f));
}

TEST_CASE("FlipbookNode generateCode emits canonical flipbook constants") {
    editor::graph::FlipbookNode node;
    node.setProperty("columns", 4.0f);
    node.setProperty("rows", 4.0f);
    node.setProperty("framesPerSecond", 30.0f);
    node.setProperty("loop", 1.0f);

    std::string code = node.generateCode("node_7_", {});
    CHECK(code.find("mod(camera.u_Time * 30.000000, 16.000000)") != std::string::npos);
    CHECK(code.find("mod(node_7_frame, 4.000000)") != std::string::npos);
    CHECK(code.find("floor(node_7_frame / 4.000000)") != std::string::npos);
    CHECK(code.find("vec2(1.0 / 4.000000, 1.0 / 4.000000)") != std::string::npos);
    CHECK(code.find("node_7_UV") != std::string::npos);
}

TEST_CASE("FlipbookNode static early-out when no animation") {
    editor::graph::FlipbookNode node;
    node.setProperty("framesPerSecond", 0.0f); // no animation -> pass UV straight through
    std::string code = node.generateCode("node_3_", {});
    CHECK(code.find("vec2 node_3_UV = fragTexCoord;") != std::string::npos);
    CHECK(code.find("mod(") == std::string::npos);
}

TEST_CASE("RotatorNode emits runtime cos/sin (animation not frozen)") {
    editor::graph::RotatorNode node;
    std::string code = node.generateCode("node_2_", {});
    // cos/sin MUST be GLSL calls on the runtime angle, never host-baked literals.
    CHECK(code.find("cos(node_2_Angle)") != std::string::npos);
    CHECK(code.find("sin(node_2_Angle)") != std::string::npos);
    CHECK(code.find("camera.u_Time") != std::string::npos); // unwired Time falls back to u_Time
    CHECK(code.find("6.28318530718") != std::string::npos); // rotations/sec -> radians
}

TEST_CASE("CustomRotatorNode uses pin-driven angle/center, runtime cos/sin") {
    editor::graph::CustomRotatorNode node;
    std::string code = node.generateCode("node_5_", {});
    CHECK(code.find("cos(") != std::string::npos);
    CHECK(code.find("sin(") != std::string::npos);
    // unwired pins fall back to their defaults
    CHECK(code.find("vec2(0.5)") != std::string::npos);   // default center
    CHECK(code.find("node_5_UV") != std::string::npos);
}

}
