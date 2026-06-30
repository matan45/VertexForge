#include <doctest.h>
#include <material/MaterialAsset.hpp>
#include <material/MaterialIR.hpp>
#include <material/MaterialRuntimeData.hpp>
#include <algorithm>
#include <filesystem>

namespace
{
    material::ShaderNode makeOutput(uint32_t id)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::PBROutput;
        node.name = "PBR Output";
        node.position = glm::vec2(300.0f, 200.0f);
        return node;
    }

    material::ShaderNode makeColor(uint32_t id, const glm::vec4& value)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::ConstantColor;
        node.name = "Tint";
        node.position = glm::vec2(0.0f);
        node.properties["value"] = value;
        return node;
    }

    material::ShaderNode makeScalar(uint32_t id, float value)
    {
        material::ShaderNode node;
        node.id = id;
        node.type = material::NodeType::ConstantScalar;
        node.name = "Scalar";
        node.properties["value"] = value;
        return node;
    }

    material::NodeLink link(uint32_t id, uint32_t source, const char* sourcePin,
                            uint32_t target, const char* targetPin)
    {
        material::NodeLink l;
        l.id = id;
        l.sourceNodeId = source;
        l.sourcePin = sourcePin;
        l.targetNodeId = target;
        l.targetPin = targetPin;
        return l;
    }

    material::MaterialData makeSimpleMaterial()
    {
        material::MaterialData mat;
        mat.uuid = "test";
        mat.name = "IR Test";
        mat.graph.nodes = {
            makeOutput(1),
            makeColor(2, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f))
        };
        mat.graph.links = {
            link(1, 2, "RGBA", 1, "Albedo")
        };
        mat.graph.nextNodeId = 3;
        mat.graph.nextLinkId = 2;
        return mat;
    }
}

TEST_SUITE("MaterialIR")
{
    TEST_CASE("MaterialIR hash ignores editor positions and node storage order")
    {
        material::MaterialData a = makeSimpleMaterial();
        material::MaterialData b = a;
        b.graph.nodes[0].position = glm::vec2(-1000.0f, 5.0f);
        b.graph.nodes[1].position = glm::vec2(900.0f, 40.0f);
        std::reverse(b.graph.nodes.begin(), b.graph.nodes.end());

        CHECK(material::MaterialIRBuilder::computeHashString(a) ==
              material::MaterialIRBuilder::computeHashString(b));
    }

    TEST_CASE("MaterialIR hash changes for semantic material changes")
    {
        material::MaterialData a = makeSimpleMaterial();
        material::MaterialData b = a;
        b.graph.nodes[1].properties["value"] = glm::vec4(1.0f, 0.5f, 0.75f, 1.0f);

        CHECK(material::MaterialIRBuilder::computeHashString(a) !=
              material::MaterialIRBuilder::computeHashString(b));

        b = a;
        b.staticParameters["UseDetail"] = true;
        CHECK(material::MaterialIRBuilder::computeHashString(a) !=
              material::MaterialIRBuilder::computeHashString(b));
    }

    TEST_CASE("MaterialIR records explicit GPU-driven fallback for unsupported generated paths")
    {
        material::MaterialData mat = makeSimpleMaterial();

        material::ShaderNode add;
        add.id = 3;
        add.type = material::NodeType::Add;
        add.name = "Add";
        mat.graph.nodes.push_back(makeScalar(4, 0.25f));
        mat.graph.nodes.push_back(makeScalar(5, 0.25f));
        mat.graph.nodes.push_back(add);
        mat.graph.links = {
            link(1, 4, "Value", 3, "A"),
            link(2, 5, "Value", 3, "B"),
            link(3, 3, "Value", 1, "Albedo")
        };

        material::MaterialIR ir = material::MaterialIRBuilder::fromMaterialData(mat);
        CHECK_FALSE(ir.gpuDrivenSupported);
        CHECK_FALSE(ir.gpuDrivenFallbackReason.empty());
    }

    TEST_CASE("MaterialIR keeps CPU-evaluable emission strength GPU-driven compatible")
    {
        material::MaterialData mat;
        mat.uuid = "emission";
        mat.name = "Emission";
        material::ShaderNode sinNode;
        sinNode.id = 3;
        sinNode.type = material::NodeType::Sin;
        sinNode.name = "Pulse";

        mat.graph.nodes = {
            makeOutput(1),
            makeScalar(2, 2.0f),
            sinNode
        };
        mat.graph.links = {
            link(1, 2, "Value", 3, "Value"),
            link(2, 3, "Value", 1, "EmissionStrength")
        };

        material::MaterialIR ir = material::MaterialIRBuilder::fromMaterialData(mat);
        CHECK(ir.gpuDrivenSupported);
        CHECK(ir.gpuDrivenFallbackReason.empty());
    }

    TEST_CASE("MaterialRuntimeData builds parameter block and shader-map identity")
    {
        material::MaterialData mat = makeSimpleMaterial();
        mat.cachedVertexShader = "#type vertex\nvoid main() {}";
        mat.cachedFragmentShader = "#type fragment\nvoid main() {}";
        mat.graph.nodes[1].properties[material::PARAM_FLAG_PROPERTY] = 1.0f;
        mat.graph.nodes[1].properties[material::PARAM_NAME_PROPERTY] = std::string("Tint");

        material::MaterialRuntimeData runtime =
            material::MaterialRuntimeDataBuilder::fromMaterialData(mat);

        CHECK_FALSE(runtime.irHash.empty());
        CHECK(runtime.shaderMap.hasGeneratedShader);
        CHECK_FALSE(runtime.shaderMap.shaderMapKey.empty());
        CHECK(runtime.parameterLayout.hasValueParameters());
        CHECK(runtime.defaultParameterBlock.size() == runtime.parameterLayout.uniformBlockSize);
    }

    TEST_CASE("MaterialAsset round-trip preserves cooked IR metadata")
    {
        namespace fs = std::filesystem;
        fs::path tmpPath = fs::temp_directory_path() / "vf_test_material_ir.vfMat";

        material::MaterialData mat = makeSimpleMaterial();
        mat.blendMode = material::BlendMode::Additive;
        REQUIRE(material::MaterialAsset::save(tmpPath.string(), mat));

        auto loaded = material::MaterialAsset::load(tmpPath.string());
        REQUIRE(loaded.has_value());
        CHECK_FALSE(loaded->irHash.empty());
        CHECK(loaded->shaderMapKey ==
              material::MaterialRuntimeDataBuilder::fromMaterialData(mat).shaderMap.shaderMapKey);
        CHECK(loaded->blendMode == material::BlendMode::Additive);

        std::error_code ec;
        fs::remove(tmpPath, ec);
    }
}
