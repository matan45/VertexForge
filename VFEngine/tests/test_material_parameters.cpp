#include <doctest.h>
#include <material/MaterialParameterSet.hpp>
#include <material/MaterialTypes.hpp>
#include <material/MaterialAsset.hpp>
#include <cstring>
#include <filesystem>
#include <vector>

// ============================================================
// Material named-parameter foundation (format 1.1)
// ============================================================

namespace
{
    material::ShaderNode makeScalarNode(uint32_t id, const std::string& paramName, float value)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::ConstantScalar;
        node.name = "Constant";
        node.properties["value"] = value;
        if (!paramName.empty())
        {
            node.properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
            node.properties[material::PARAM_NAME_PROPERTY] = paramName;
        }
        return node;
    }

    material::ShaderNode makeColorNode(uint32_t id, const std::string& paramName, glm::vec4 value)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::ConstantColor;
        node.name = "Color";
        node.properties["value"] = value;
        if (!paramName.empty())
        {
            node.properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
            node.properties[material::PARAM_NAME_PROPERTY] = paramName;
        }
        return node;
    }

    material::ShaderNode makeVec3Node(uint32_t id, const std::string& paramName, glm::vec3 value)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::ConstantVec3;
        node.name = "Vector3";
        node.properties["value"] = value;
        if (!paramName.empty())
        {
            node.properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
            node.properties[material::PARAM_NAME_PROPERTY] = paramName;
        }
        return node;
    }

    material::ShaderNode makeTextureNode(uint32_t id, const std::string& paramName,
                                         int slot, const std::string& path)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::TextureSample;
        node.name = "Texture Sample";
        node.properties["texturePath"] = path;
        node.properties["textureIndex"] = static_cast<float>(slot);
        if (!paramName.empty())
        {
            node.properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
            node.properties[material::PARAM_NAME_PROPERTY] = paramName;
        }
        return node;
    }
}

TEST_SUITE("MaterialParameters") {

// ---- collection ----

TEST_CASE("collectParameters: unflagged nodes are ignored") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(1, "", 0.5f));
    graph.nodes.push_back(makeColorNode(2, "", glm::vec4(1.0f)));

    auto set = material::collectParameters(graph);
    CHECK(set.empty());
    CHECK_FALSE(set.hasValueParameters());
    CHECK(set.uniformBlockSize == 0);
}

TEST_CASE("collectParameters: flagged nodes become parameters") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(1, "Roughness Boost", 0.25f));
    graph.nodes.push_back(makeColorNode(2, "Tint", glm::vec4(1.0f, 0.5f, 0.25f, 1.0f)));
    graph.nodes.push_back(makeScalarNode(3, "", 0.7f));

    auto set = material::collectParameters(graph);
    REQUIRE(set.values.size() == 2);
    CHECK(set.find("Roughness Boost") != nullptr);
    CHECK(set.find("Tint") != nullptr);
    CHECK(set.find("Missing") == nullptr);
    CHECK(set.find("Tint")->type == material::ParameterType::Color);
    CHECK(set.find("Roughness Boost")->type == material::ParameterType::Scalar);
    CHECK(std::get<float>(set.find("Roughness Boost")->defaultValue) == doctest::Approx(0.25f));
}

TEST_CASE("collectParameters: deterministic name-sorted order") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(5, "Zeta", 1.0f));
    graph.nodes.push_back(makeScalarNode(2, "Alpha", 2.0f));
    graph.nodes.push_back(makeScalarNode(9, "Mid", 3.0f));

    auto set = material::collectParameters(graph);
    REQUIRE(set.values.size() == 3);
    CHECK(set.values[0].name == "Alpha");
    CHECK(set.values[1].name == "Mid");
    CHECK(set.values[2].name == "Zeta");
}

TEST_CASE("collectParameters: duplicate name same type shares one member") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(1, "Shared", 0.1f));
    graph.nodes.push_back(makeScalarNode(2, "Shared", 0.9f));

    auto set = material::collectParameters(graph);
    REQUIRE(set.values.size() == 1);
    // First definition (lowest node id) wins the default
    CHECK(std::get<float>(set.values[0].defaultValue) == doctest::Approx(0.1f));
    // Both nodes resolve to the same uniform member
    REQUIRE(set.nodeToGlslName.count(1) == 1);
    REQUIRE(set.nodeToGlslName.count(2) == 1);
    CHECK(set.nodeToGlslName.at(1) == set.nodeToGlslName.at(2));
}

TEST_CASE("collectParameters: duplicate name different type is dropped") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(1, "Clash", 0.5f));
    graph.nodes.push_back(makeColorNode(2, "Clash", glm::vec4(1.0f)));

    auto set = material::collectParameters(graph);
    REQUIRE(set.values.size() == 1);
    CHECK(set.values[0].type == material::ParameterType::Scalar);
    CHECK(set.nodeToGlslName.count(2) == 0);
}

TEST_CASE("collectParameters: texture parameters carry slot and path") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeTextureNode(1, "Detail Map", 9, "Assets/detail.vfImage"));
    graph.nodes.push_back(makeTextureNode(2, "", 0, "Assets/albedo.vfImage"));

    auto set = material::collectParameters(graph);
    REQUIRE(set.textures.size() == 1);
    CHECK(set.textures[0].name == "Detail Map");
    CHECK(set.textures[0].slot == 9);
    CHECK(set.textures[0].defaultTexturePath == "Assets/detail.vfImage");
    CHECK(set.findTexture("Detail Map") != nullptr);
    CHECK(set.findTexture("Albedo") == nullptr);
    // Texture params don't contribute to the uniform block
    CHECK_FALSE(set.hasValueParameters());
}

TEST_CASE("collectParameters: whitespace-only name is not a parameter") {
    material::ShaderGraph graph;
    auto node = makeScalarNode(1, "", 0.5f);
    node.properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
    node.properties[material::PARAM_NAME_PROPERTY] = std::string("   ");
    graph.nodes.push_back(node);

    auto set = material::collectParameters(graph);
    CHECK(set.empty());
}

// ---- identifier sanitization ----

TEST_CASE("sanitizeGlslIdentifier") {
    CHECK(material::sanitizeGlslIdentifier("Tint") == "Tint");
    CHECK(material::sanitizeGlslIdentifier("My Tint") == "My_Tint");
    CHECK(material::sanitizeGlslIdentifier("2sided") == "_2sided");
    CHECK(material::sanitizeGlslIdentifier("a-b.c") == "a_b_c");
    CHECK(material::sanitizeGlslIdentifier("") == "param");
    CHECK(material::sanitizeGlslIdentifier("###") == "___");
    CHECK(material::sanitizeGlslIdentifier("float") == "floatParam");
}

TEST_CASE("collectParameters: colliding sanitized names get suffixes") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(1, "My Tint", 0.1f));
    graph.nodes.push_back(makeScalarNode(2, "My-Tint", 0.2f));

    auto set = material::collectParameters(graph);
    REQUIRE(set.values.size() == 2);
    CHECK(set.values[0].glslName != set.values[1].glslName);
}

// ---- std140 layout ----

TEST_CASE("std140 layout: float,vec3,float,vec2,vec4 sequence") {
    material::ShaderGraph graph;
    // Names chosen so sort order is: A(float), B(vec3), C(float), D(vec2), E(vec4)
    graph.nodes.push_back(makeScalarNode(1, "A", 1.0f));
    graph.nodes.push_back(makeVec3Node(2, "B", glm::vec3(1.0f)));
    graph.nodes.push_back(makeScalarNode(3, "C", 2.0f));
    {
        material::ShaderNode node;
        node.id = 4;
        node.type = material::NodeType::ConstantVec2;
        node.properties["value"] = glm::vec2(3.0f, 4.0f);
        node.properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
        node.properties[material::PARAM_NAME_PROPERTY] = std::string("D");
        graph.nodes.push_back(node);
    }
    graph.nodes.push_back(makeColorNode(5, "E", glm::vec4(0.5f)));

    auto set = material::collectParameters(graph);
    REQUIRE(set.values.size() == 5);
    CHECK(set.values[0].byteOffset == 0);   // A: float @ 0
    CHECK(set.values[1].byteOffset == 16);  // B: vec3 aligns to 16
    CHECK(set.values[2].byteOffset == 28);  // C: float fits right after vec3's 12 bytes
    CHECK(set.values[3].byteOffset == 32);  // D: vec2 aligns to 8
    CHECK(set.values[4].byteOffset == 48);  // E: vec4 aligns to 16
    CHECK(set.uniformBlockSize == 64);      // rounded to 16
}

TEST_CASE("std140 layout: single float rounds block to 16") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(1, "Only", 1.0f));

    auto set = material::collectParameters(graph);
    CHECK(set.uniformBlockSize == 16);
}

// ---- writeStd140 ----

TEST_CASE("writeStd140: defaults are byte-exact") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(1, "A", 0.75f));
    graph.nodes.push_back(makeColorNode(2, "B", glm::vec4(0.1f, 0.2f, 0.3f, 0.4f)));

    auto set = material::collectParameters(graph);
    REQUIRE(set.uniformBlockSize == 32);

    std::vector<std::byte> block(set.uniformBlockSize);
    material::writeStd140(set, {}, block);

    float a;
    std::memcpy(&a, block.data() + set.find("A")->byteOffset, sizeof(float));
    CHECK(a == doctest::Approx(0.75f));

    glm::vec4 b;
    std::memcpy(&b, block.data() + set.find("B")->byteOffset, sizeof(glm::vec4));
    CHECK(b.x == doctest::Approx(0.1f));
    CHECK(b.w == doctest::Approx(0.4f));
}

TEST_CASE("writeStd140: override replaces default, mismatched type ignored") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(1, "A", 0.75f));
    graph.nodes.push_back(makeColorNode(2, "B", glm::vec4(1.0f)));

    auto set = material::collectParameters(graph);
    std::vector<std::byte> block(set.uniformBlockSize);

    std::map<std::string, material::ParameterValue> overrides;
    overrides["A"] = 0.25f;
    overrides["B"] = 3.0f; // wrong type for a Color — must keep default
    material::writeStd140(set, overrides, block);

    float a;
    std::memcpy(&a, block.data() + set.find("A")->byteOffset, sizeof(float));
    CHECK(a == doctest::Approx(0.25f));

    glm::vec4 b;
    std::memcpy(&b, block.data() + set.find("B")->byteOffset, sizeof(glm::vec4));
    CHECK(b.x == doctest::Approx(1.0f));
}

TEST_CASE("writeStd140: undersized buffer is rejected without writing") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeColorNode(1, "B", glm::vec4(1.0f)));

    auto set = material::collectParameters(graph);
    std::vector<std::byte> block(4, std::byte{0xAB});
    material::writeStd140(set, {}, block);
    CHECK(block[0] == std::byte{0xAB});
}

// ---- GLSL emission ----

TEST_CASE("emitGlslUniformBlock: declaration shape") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeScalarNode(1, "Boost", 1.0f));
    graph.nodes.push_back(makeVec3Node(2, "Tint Color", glm::vec3(1.0f)));

    auto set = material::collectParameters(graph);
    std::string code = material::emitGlslUniformBlock(
        set, material::PARAMETER_DESCRIPTOR_SET, material::PARAMETER_DESCRIPTOR_BINDING);

    CHECK(code.find("layout(std140, set = 2, binding = 0) uniform MaterialParameterBlock {") != std::string::npos);
    CHECK(code.find("float Boost;") != std::string::npos);
    CHECK(code.find("vec3 Tint_Color;") != std::string::npos);
    CHECK(code.find("} uParams;") != std::string::npos);
    // Members must appear in layout (sorted) order
    CHECK(code.find("float Boost;") < code.find("vec3 Tint_Color;"));
}

TEST_CASE("emitGlslUniformBlock: empty set emits nothing") {
    material::MaterialParameterSet set;
    CHECK(material::emitGlslUniformBlock(set, 2, 0).empty());
}

// ---- asset round-trip ----

TEST_CASE("MaterialAsset round-trip preserves parameter flags and writes 1.1") {
    namespace fs = std::filesystem;
    fs::path tmpPath = fs::temp_directory_path() / "vf_test_param_material.vfMat";

    material::MaterialData mat = material::MaterialAsset::createDefault("ParamTest");
    auto scalarNode = makeScalarNode(mat.graph.nextNodeId++, "Exposed Scalar", 0.33f);
    mat.graph.nodes.push_back(scalarNode);

    REQUIRE(material::MaterialAsset::save(tmpPath.string(), mat));

    auto loaded = material::MaterialAsset::load(tmpPath.string());
    REQUIRE(loaded.has_value());

    auto set = material::collectParameters(loaded->graph);
    REQUIRE(set.values.size() == 1);
    CHECK(set.values[0].name == "Exposed Scalar");
    CHECK(std::get<float>(set.values[0].defaultValue) == doctest::Approx(0.33f));

    fs::remove(tmpPath);
}

}
