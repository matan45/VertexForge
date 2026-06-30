#pragma once

#include "MaterialParameterSet.hpp"
#include "MaterialTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace material
{
    inline constexpr uint32_t MATERIAL_IR_VERSION = 1;
    inline constexpr uint32_t MATERIAL_IR_COMPILER_VERSION = 1;

    struct MaterialIRAssetRef
    {
        std::string guid;
        std::string path;

        bool empty() const { return guid.empty() && path.empty(); }
    };

    enum class MaterialIRPropertyType : uint8_t
    {
        Scalar,
        Vec2,
        Vec3,
        Vec4,
        String,
        AssetReference
    };

    struct MaterialIRProperty
    {
        std::string name;
        MaterialIRPropertyType type = MaterialIRPropertyType::Scalar;
        NodeProperty value = 0.0f;
        MaterialIRAssetRef assetRef;
    };

    struct MaterialIRPin
    {
        std::string stableId;
        std::string name;
        PinType type = PinType::Float;
        PinKind kind = PinKind::Input;
        std::optional<ParameterValue> defaultValue;
    };

    struct MaterialIRNode
    {
        std::string stableId;
        uint32_t sourceNodeId = 0;
        NodeType op = NodeType::ConstantScalar;
        std::string displayName;
        std::vector<MaterialIRProperty> properties;
        std::vector<MaterialIRPin> inputs;
        std::vector<MaterialIRPin> outputs;
    };

    struct MaterialIRLink
    {
        std::string stableId;
        std::string sourceNodeId;
        std::string targetNodeId;
        std::string sourcePinId;
        std::string targetPinId;
        std::string sourcePinName;
        std::string targetPinName;
    };

    struct MaterialStaticParameter
    {
        std::string name;
        bool value = false;
    };

    struct MaterialIR
    {
        uint32_t version = MATERIAL_IR_VERSION;
        uint32_t compilerVersion = MATERIAL_IR_COMPILER_VERSION;
        MaterialDomain domain = MaterialDomain::Surface;
        ShadingModel shadingModel = ShadingModel::DefaultLit;
        BlendMode blendMode = BlendMode::Opaque;
        float opacity = 1.0f;
        float alphaCutoff = 0.5f;
        std::vector<MaterialStaticParameter> staticParameters;
        std::vector<MaterialIRNode> nodes;
        std::vector<MaterialIRLink> links;
        MaterialParameterSet parameters;
        bool gpuDrivenSupported = true;
        std::string gpuDrivenFallbackReason;
    };

    class MaterialIRBuilder
    {
    public:
        static MaterialIR fromMaterialData(const MaterialData& material);
        static uint64_t computeHash(const MaterialIR& ir);
        static std::string computeHashString(const MaterialIR& ir);
        static std::string computeHashString(const MaterialData& material);
        static std::string formatHash(uint64_t hash);

    private:
        static bool isGraphGPUDrivenSupported(
            const ShaderGraph& graph,
            std::string& fallbackReason);
    };
}
