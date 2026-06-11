#include <doctest.h>
#include <components/Components.hpp>
#include <material/MaterialParameterSet.hpp>
#include <material/MaterialInstanceTypes.hpp>
#include <serialization/SceneSerialization.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include "render/material/MaterialPBRExtractor.hpp"
#include "impl/components/MaterialComponentService.hpp"
#include "data/EntityConversion.hpp"
#include <nlohmann/json.hpp>

// ============================================================
// Per-entity runtime material parameter overrides (Phase 3)
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

    material::MaterialData makeParentWithScalarParam(const std::string& paramName, float defaultValue)
    {
        material::MaterialData parent;
        material::ShaderNode output;
        output.id = 1;
        output.type = material::NodeType::PBROutput;
        parent.graph.nodes.push_back(output);
        parent.graph.nodes.push_back(makeParamScalar(2, paramName, defaultValue));

        material::NodeLink link;
        link.id = parent.graph.nextLinkId++;
        link.sourceNodeId = 2;
        link.sourcePin = "Value";
        link.targetNodeId = 1;
        link.targetPin = "Roughness";
        parent.graph.links.push_back(link);
        parent.graph.nextNodeId = 3;
        return parent;
    }
}

TEST_SUITE("MaterialRuntimeOverrides") {

// ---- scene serialization ----

TEST_CASE("parameter override serialization round-trips typed values") {
    std::map<std::string, material::ParameterValue> overrides;
    overrides["Boost"] = 0.4f;
    overrides["Wind"] = glm::vec2(1.0f, -2.0f);
    overrides["Offset"] = glm::vec3(0.1f, 0.2f, 0.3f);
    overrides["Tint"] = glm::vec4(0.9f, 0.8f, 0.7f, 0.6f);

    nlohmann::json j = serialization::SceneSerialization::serializeParameterOverrides(overrides);

    std::map<std::string, material::ParameterValue> loaded;
    serialization::SceneSerialization::deserializeParameterOverrides(j, loaded);

    REQUIRE(loaded.size() == 4);
    CHECK(std::get<float>(loaded.at("Boost")) == doctest::Approx(0.4f));
    CHECK(std::get<glm::vec2>(loaded.at("Wind")).y == doctest::Approx(-2.0f));
    CHECK(std::get<glm::vec3>(loaded.at("Offset")).z == doctest::Approx(0.3f));
    CHECK(std::get<glm::vec4>(loaded.at("Tint")).w == doctest::Approx(0.6f));
}

TEST_CASE("parameter override deserialization accepts legacy bare floats") {
    nlohmann::json j = {{"OldParam", 0.55f}};

    std::map<std::string, material::ParameterValue> loaded;
    serialization::SceneSerialization::deserializeParameterOverrides(j, loaded);

    REQUIRE(loaded.size() == 1);
    CHECK(std::get<float>(loaded.at("OldParam")) == doctest::Approx(0.55f));
}

// ---- component service ----

TEST_CASE("MaterialComponentService set/get/clear runtime parameter") {
    scene::Entity entity("RuntimeOverrideEntity");
    entity.addComponent<components::MaterialComponent>();

    services::MaterialComponentService service(nullptr);
    services::EntityHandle handle = services::internal::toHandle(entity.getHandle());

    // No override yet
    CHECK_FALSE(service.getMaterialParameter(handle, "Tint").has_value());

    // Set typed values
    CHECK(service.setMaterialParameter(handle, "Tint", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)));
    CHECK(service.setMaterialParameter(handle, "Boost", 0.8f));

    auto tint = service.getMaterialParameter(handle, "Tint");
    REQUIRE(tint.has_value());
    CHECK(std::get<glm::vec4>(*tint).r == doctest::Approx(1.0f));

    auto boost = service.getMaterialParameter(handle, "Boost");
    REQUIRE(boost.has_value());
    CHECK(std::get<float>(*boost) == doctest::Approx(0.8f));

    // Clear one
    CHECK(service.clearMaterialParameter(handle, "Boost"));
    CHECK_FALSE(service.getMaterialParameter(handle, "Boost").has_value());
    CHECK(service.getMaterialParameter(handle, "Tint").has_value());

    // Clearing a missing override fails
    CHECK_FALSE(service.clearMaterialParameter(handle, "Boost"));

    // Empty name clears everything
    CHECK(service.setMaterialParameter(handle, "Boost", 0.1f));
    CHECK(service.clearMaterialParameter(handle, ""));
    CHECK_FALSE(service.getMaterialParameter(handle, "Tint").has_value());
    CHECK_FALSE(service.getMaterialParameter(handle, "Boost").has_value());

    scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
}

TEST_CASE("MaterialComponentService rejects entities without MaterialComponent") {
    scene::Entity entity("NoMaterialEntity");
    services::MaterialComponentService service(nullptr);
    services::EntityHandle handle = services::internal::toHandle(entity.getHandle());

    CHECK_FALSE(service.setMaterialParameter(handle, "Tint", 1.0f));
    CHECK_FALSE(service.clearMaterialParameter(handle, "Tint"));
    CHECK_FALSE(service.getMaterialParameter(handle, "Tint").has_value());

    scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
}

// ---- extraction precedence ----

TEST_CASE("extraction precedence: runtime > instance > default") {
    material::MaterialData parent = makeParentWithScalarParam("Roughness Boost", 0.5f);

    material::MaterialInstanceData instance;

    // Default
    auto base = render::mesh::MaterialPBRExtractor::extractPBRFromInstance(instance, parent);
    CHECK(base.roughness == doctest::Approx(0.5f));

    // Instance override
    instance.parameterOverrides["Roughness Boost"] = 0.7f;
    auto inst = render::mesh::MaterialPBRExtractor::extractPBRFromInstance(instance, parent);
    CHECK(inst.roughness == doctest::Approx(0.7f));

    // Runtime override wins over instance
    render::mesh::MaterialPBRExtractor::ParameterOverrides runtime;
    runtime["Roughness Boost"] = 0.9f;
    auto rt = render::mesh::MaterialPBRExtractor::extractPBRFromInstance(instance, parent, &runtime);
    CHECK(rt.roughness == doctest::Approx(0.9f));
}

TEST_CASE("runtime overrides apply to plain materials via the resolved set") {
    material::MaterialData parent = makeParentWithScalarParam("Roughness Boost", 0.5f);

    material::MaterialParameterSet set = material::collectParameters(parent.graph);
    render::mesh::MaterialPBRExtractor::ParameterOverrides runtime;
    runtime["Roughness Boost"] = 0.25f;
    runtime["Unknown"] = 1.0f; // not a parameter — must be dropped

    auto resolved = material::resolveOverrides(set, nullptr, &runtime);
    REQUIRE(resolved.size() == 1);

    auto pbr = render::mesh::MaterialPBRExtractor::extractPBRFromMaterial(parent, &resolved);
    CHECK(pbr.roughness == doctest::Approx(0.25f));
}

}
