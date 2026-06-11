#include <doctest.h>
#include <material/MaterialParameterSet.hpp>
#include <material/MaterialInstanceTypes.hpp>
#include <material/MaterialInstanceAsset.hpp>
#include <material/MaterialTypes.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetRef.hpp>
#include "render/material/MaterialPBRExtractor.hpp"
#include <filesystem>
#include <fstream>

// ============================================================
// Material instance overrides of named parameters (format 1.1)
// ============================================================

namespace
{
    material::ShaderNode makeParamScalar(uint32_t id, const std::string& name, float value)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::ConstantScalar;
        node.properties["value"] = value;
        node.properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
        node.properties[material::PARAM_NAME_PROPERTY] = name;
        return node;
    }

    material::ShaderNode makeParamColor(uint32_t id, const std::string& name, glm::vec4 value)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::ConstantColor;
        node.properties["value"] = value;
        node.properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
        node.properties[material::PARAM_NAME_PROPERTY] = name;
        return node;
    }

    material::ShaderNode makeParamTexture(uint32_t id, const std::string& name, int slot)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::TextureSample;
        node.properties["texturePath"] = std::string("Assets/parent.vfImage");
        node.properties["textureIndex"] = static_cast<float>(slot);
        node.properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
        node.properties[material::PARAM_NAME_PROPERTY] = name;
        return node;
    }

    void link(material::ShaderGraph& graph, uint32_t sourceNode, const std::string& sourcePin,
              uint32_t targetNode, const std::string& targetPin)
    {
        material::NodeLink l;
        l.id = graph.nextLinkId++;
        l.sourceNodeId = sourceNode;
        l.sourcePin = sourcePin;
        l.targetNodeId = targetNode;
        l.targetPin = targetPin;
        graph.links.push_back(l);
    }

    material::MaterialData makeParentWithScalarParam(const std::string& paramName, float defaultValue)
    {
        material::MaterialData parent;
        material::ShaderNode output;
        output.id = 1;
        output.type = material::NodeType::PBROutput;
        parent.graph.nodes.push_back(output);
        parent.graph.nodes.push_back(makeParamScalar(2, paramName, defaultValue));
        link(parent.graph, 2, "Value", 1, "Metallic");
        parent.graph.nextNodeId = 3;
        return parent;
    }

    asset::AssetRef makeRef()
    {
        return asset::AssetRef::fromGUID(asset::AssetGUID::generate());
    }
}

TEST_SUITE("MaterialInstanceParameters") {

// ---- resolution ----

TEST_CASE("resolveOverrides: instance value applies, stale and mismatched skipped") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeParamScalar(1, "Boost", 0.5f));
    graph.nodes.push_back(makeParamColor(2, "Tint", glm::vec4(1.0f)));
    auto set = material::collectParameters(graph);

    material::MaterialInstanceData instance;
    instance.parameterOverrides["Boost"] = 0.9f;                     // valid
    instance.parameterOverrides["Tint"] = 2.0f;                      // wrong type -> skipped
    instance.parameterOverrides["Removed"] = 1.0f;                   // stale name -> skipped

    auto resolved = material::resolveOverrides(set, &instance);
    REQUIRE(resolved.size() == 1);
    CHECK(std::get<float>(resolved.at("Boost")) == doctest::Approx(0.9f));
}

TEST_CASE("resolveOverrides: runtime wins over instance") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeParamScalar(1, "Boost", 0.5f));
    auto set = material::collectParameters(graph);

    material::MaterialInstanceData instance;
    instance.parameterOverrides["Boost"] = 0.7f;

    std::map<std::string, material::ParameterValue> runtime;
    runtime["Boost"] = 0.2f;

    auto resolved = material::resolveOverrides(set, &instance, &runtime);
    CHECK(std::get<float>(resolved.at("Boost")) == doctest::Approx(0.2f));
}

TEST_CASE("resolveTextureOverrides: name resolves to slot, legacy slot override wins") {
    material::ShaderGraph graph;
    graph.nodes.push_back(makeParamTexture(1, "Detail", 9));
    graph.nodes.push_back(makeParamTexture(2, "Albedo Map", 0));
    auto set = material::collectParameters(graph);

    auto detailRef = makeRef();
    auto albedoByNameRef = makeRef();
    auto albedoBySlotRef = makeRef();

    material::MaterialInstanceData instance;
    instance.textureParameterOverrides["Detail"] = detailRef;
    instance.textureParameterOverrides["Albedo Map"] = albedoByNameRef;
    instance.textureParameterOverrides["Gone"] = makeRef(); // stale name -> skipped
    // Legacy slot override on Albedo must beat the name-addressed one
    instance.textureOverrides[material::TextureSlot::Albedo] = albedoBySlotRef;

    auto resolved = material::resolveTextureOverrides(set, instance);
    REQUIRE(resolved.size() == 2);
    CHECK(resolved.at(static_cast<material::TextureSlot>(9)).getGUID() == detailRef.getGUID());
    CHECK(resolved.at(material::TextureSlot::Albedo).getGUID() == albedoBySlotRef.getGUID());
}

TEST_CASE("overrideValueForNode: flagged node with matching type only") {
    auto node = makeParamScalar(1, "Boost", 0.5f);

    std::map<std::string, material::ParameterValue> overrides;
    overrides["Boost"] = 0.75f;
    auto value = material::overrideValueForNode(node, overrides);
    REQUIRE(value.has_value());
    CHECK(std::get<float>(*value) == doctest::Approx(0.75f));

    overrides["Boost"] = glm::vec3(1.0f); // type mismatch
    CHECK_FALSE(material::overrideValueForNode(node, overrides).has_value());

    material::ShaderNode plain;
    plain.id = 2;
    plain.type = material::NodeType::ConstantScalar;
    plain.properties["value"] = 0.5f;
    overrides["Boost"] = 0.75f;
    CHECK_FALSE(material::overrideValueForNode(plain, overrides).has_value());
}

// ---- world visibility ----

TEST_CASE("isParameterWorldVisible: direct output connection") {
    material::MaterialData parent = makeParentWithScalarParam("Boost", 0.5f);
    CHECK(material::isParameterWorldVisible(parent.graph, 2));
}

TEST_CASE("isParameterWorldVisible: emission chain through Multiply") {
    material::MaterialData parent;
    material::ShaderNode output;
    output.id = 1;
    output.type = material::NodeType::PBROutput;
    parent.graph.nodes.push_back(output);
    parent.graph.nodes.push_back(makeParamScalar(2, "Pulse", 1.0f));
    material::ShaderNode mult;
    mult.id = 3;
    mult.type = material::NodeType::Multiply;
    parent.graph.nodes.push_back(mult);
    link(parent.graph, 2, "Value", 3, "A");
    link(parent.graph, 3, "Result", 1, "EmissionStrength");

    CHECK(material::isParameterWorldVisible(parent.graph, 2));
}

TEST_CASE("isParameterWorldVisible: dangling node is preview-only") {
    material::MaterialData parent;
    material::ShaderNode output;
    output.id = 1;
    output.type = material::NodeType::PBROutput;
    parent.graph.nodes.push_back(output);
    parent.graph.nodes.push_back(makeParamScalar(2, "Unused", 1.0f));

    CHECK_FALSE(material::isParameterWorldVisible(parent.graph, 2));
}

// ---- extractor integration ----

TEST_CASE("extractPBRFromInstance honors named parameter override feeding Metallic") {
    material::MaterialData parent = makeParentWithScalarParam("Metal Amount", 0.25f);

    material::MaterialInstanceData instance;
    auto base = render::mesh::MaterialPBRExtractor::extractPBRFromInstance(instance, parent);
    CHECK(base.metallic == doctest::Approx(0.25f));

    instance.parameterOverrides["Metal Amount"] = 0.95f;
    auto overridden = render::mesh::MaterialPBRExtractor::extractPBRFromInstance(instance, parent);
    CHECK(overridden.metallic == doctest::Approx(0.95f));

    // Fixed PBR override still wins over the named parameter
    instance.metallicOverride = 0.1f;
    auto fixedWins = render::mesh::MaterialPBRExtractor::extractPBRFromInstance(instance, parent);
    CHECK(fixedWins.metallic == doctest::Approx(0.1f));
}

// ---- serialization ----

TEST_CASE("MaterialInstanceAsset round-trips typed parameter overrides") {
    namespace fs = std::filesystem;
    fs::path tmpPath = fs::temp_directory_path() / "vf_test_param_instance.vfMatInstance";

    auto detailRef = makeRef();

    material::MaterialInstanceData instance;
    instance.uuid = "12345";
    instance.name = "ParamInstance";
    instance.parentMaterialRef = makeRef();
    instance.parameterOverrides["Boost"] = 0.9f;
    instance.parameterOverrides["Wind"] = glm::vec2(1.0f, 2.0f);
    instance.parameterOverrides["Tint"] = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
    instance.textureParameterOverrides["Detail"] = detailRef;

    REQUIRE(material::MaterialInstanceAsset::save(tmpPath.string(), instance));

    auto loaded = material::MaterialInstanceAsset::load(tmpPath.string());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->parameterOverrides.size() == 3);
    CHECK(std::get<float>(loaded->parameterOverrides.at("Boost")) == doctest::Approx(0.9f));
    CHECK(std::get<glm::vec2>(loaded->parameterOverrides.at("Wind")).y == doctest::Approx(2.0f));
    CHECK(std::get<glm::vec4>(loaded->parameterOverrides.at("Tint")).z == doctest::Approx(0.3f));
    REQUIRE(loaded->textureParameterOverrides.size() == 1);
    CHECK(loaded->textureParameterOverrides.at("Detail").getGUID() == detailRef.getGUID());

    fs::remove(tmpPath);
}

TEST_CASE("MaterialInstanceAsset: 1.0 file loads with empty override maps") {
    namespace fs = std::filesystem;
    fs::path tmpPath = fs::temp_directory_path() / "vf_test_param_instance_v10.vfMatInstance";

    {
        material::MaterialInstanceData seed;
        seed.uuid = "777";
        seed.name = "Legacy";
        seed.parentMaterialRef = makeRef();
        REQUIRE(material::MaterialInstanceAsset::save(tmpPath.string(), seed));
    }
    // Rewrite the version field to 1.0 to mimic a pre-parameter file
    {
        std::ifstream in(tmpPath);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        auto pos = content.find("\"1.1\"");
        REQUIRE(pos != std::string::npos);
        content.replace(pos, 5, "\"1.0\"");
        std::ofstream out(tmpPath);
        out << content;
    }

    auto loaded = material::MaterialInstanceAsset::load(tmpPath.string());
    REQUIRE(loaded.has_value());
    CHECK(loaded->parameterOverrides.empty());
    CHECK(loaded->textureParameterOverrides.empty());
    CHECK_FALSE(loaded->hasOverrides());

    fs::remove(tmpPath);
}

}
