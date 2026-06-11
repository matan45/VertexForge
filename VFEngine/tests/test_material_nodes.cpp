#include <doctest.h>
#include <material/MaterialTypes.hpp>

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
}

TEST_CASE("every node type has a serialized name (no silent Unknown)") {
    // Remap is the last enum value; iterating to it covers the whole inventory
    const auto last = static_cast<int>(material::NodeType::Remap);
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

}
