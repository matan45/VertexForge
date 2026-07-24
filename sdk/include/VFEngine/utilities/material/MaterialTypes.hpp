#pragma once
#include "ToonProfile.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <cctype>

namespace material
{
    constexpr const char* MATERIAL_FORMAT_VERSION = "1.1";

    constexpr int MAX_MATERIAL_TEXTURES = 16;

    enum class TextureSlot : uint8_t
    {
        Albedo = 0,
        Normal = 1,
        ORM = 2,
        Metallic = 3,
        Roughness = 4,
        AO = 5,
        Emission = 6,
        Height = 7,

        Count = 16
    };

    constexpr int toIndex(TextureSlot slot)
    {
        return static_cast<int>(slot);
    }

    enum class ParameterType : uint8_t
    {
        Scalar,
        Vec2,
        Vec3,
        Vec4,
        Color
    };

    using ParameterValue = std::variant<float, glm::vec2, glm::vec3, glm::vec4>;

    enum class MaterialDomain : uint8_t
    {
        Surface
    };

    inline std::string materialDomainToString(MaterialDomain domain)
    {
        switch (domain)
        {
        case MaterialDomain::Surface: return "surface";
        default: return "surface";
        }
    }

    inline MaterialDomain stringToMaterialDomain(const std::string& str)
    {
        if (str == "surface") return MaterialDomain::Surface;
        return MaterialDomain::Surface;
    }

    enum class ShadingModel : uint8_t
    {
        DefaultLit,
        Unlit,
        Toon
    };

    inline std::string shadingModelToString(ShadingModel model)
    {
        switch (model)
        {
        case ShadingModel::DefaultLit: return "defaultLit";
        case ShadingModel::Unlit: return "unlit";
        case ShadingModel::Toon: return "toon";
        default: return "defaultLit";
        }
    }

    inline ShadingModel stringToShadingModel(const std::string& str)
    {
        if (str == "unlit") return ShadingModel::Unlit;
        if (str == "toon") return ShadingModel::Toon;
        return ShadingModel::DefaultLit;
    }

    struct MaterialParameter
    {
        ParameterType type = ParameterType::Scalar;
        std::string name;
        ParameterValue value;
        float min = 0.0f;
        float max = 1.0f;

        MaterialParameter() = default;

        MaterialParameter(const std::string& paramName, float val, float minVal = 0.0f, float maxVal = 1.0f)
            : type(ParameterType::Scalar), name(paramName), value(val), min(minVal), max(maxVal)
        {
        }

        MaterialParameter(const std::string& paramName, const glm::vec2& val)
            : type(ParameterType::Vec2), name(paramName), value(val)
        {
        }

        MaterialParameter(const std::string& paramName, const glm::vec3& val, bool isColor = false)
            : type(isColor ? ParameterType::Color : ParameterType::Vec3), name(paramName), value(glm::vec4(val, 1.0f))
        {
        }

        MaterialParameter(const std::string& paramName, const glm::vec4& val)
            : type(ParameterType::Vec4), name(paramName), value(val)
        {
        }
    };

    enum class PinType : uint8_t
    {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Texture2D
    };

    inline std::string pinTypeToString(PinType type)
    {
        switch (type)
        {
        case PinType::Float: return "Float";
        case PinType::Vec2: return "Vec2";
        case PinType::Vec3: return "Vec3";
        case PinType::Vec4: return "Vec4";
        case PinType::Texture2D: return "Texture2D";
        default: return "Unknown";
        }
    }

    enum class PinKind : uint8_t
    {
        Input,
        Output
    };

    struct NodePin
    {
        uint32_t id = 0;
        std::string name;
        PinType type = PinType::Float;
        PinKind kind = PinKind::Input;
        std::optional<ParameterValue> defaultValue;
    };

    enum class NodeType : uint8_t
    {
        PBROutput,

        ConstantScalar,
        ConstantVec2,
        ConstantVec3,
        ConstantColor,

        Add,
        Subtract,
        Multiply,
        Divide,
        Power,
        Lerp,
        Clamp,
        Saturate,
        OneMinus,
        Abs,
        Floor,
        Ceil,
        Fract,
        Sin,
        Cos,
        Dot,
        Cross,
        Normalize,
        Length,

        MixColor,

        MakeVec2,
        MakeVec3,
        MakeVec4,
        SplitVec2,
        SplitVec3,
        SplitVec4,
        Fresnel,

        VertexNormal,
        VertexUV,
        Time,

        TextureSample,
        OrmSample,

        FloatToVec2,
        FloatToVec3,
        FloatToVec4,
        Vec2ToFloat,
        Vec3ToFloat,
        Vec4ToFloat,
        Vec2ToVec3,
        Vec2ToVec4,
        Vec3ToVec2,
        Vec3ToVec4,
        Vec4ToVec2,
        Vec4ToVec3,

        // Utility nodes (format 1.1) — appended; the string-serialized format keeps
        // older files loading on newer builds (and degrades to ConstantScalar on older)
        WorldPosition,
        Panner,
        UVTransform,
        Remap,
        Flipbook,
        Rotator,
        CustomRotator,
    };

    inline std::string nodeTypeToString(NodeType type)
    {
        switch (type)
        {
        case NodeType::PBROutput: return "PBROutput";
        case NodeType::ConstantScalar: return "ConstantScalar";
        case NodeType::ConstantVec2: return "ConstantVec2";
        case NodeType::ConstantVec3: return "ConstantVec3";
        case NodeType::ConstantColor: return "ConstantColor";
        case NodeType::Add: return "Add";
        case NodeType::Subtract: return "Subtract";
        case NodeType::Multiply: return "Multiply";
        case NodeType::Divide: return "Divide";
        case NodeType::Power: return "Power";
        case NodeType::Lerp: return "Lerp";
        case NodeType::Clamp: return "Clamp";
        case NodeType::Saturate: return "Saturate";
        case NodeType::OneMinus: return "OneMinus";
        case NodeType::Abs: return "Abs";
        case NodeType::Floor: return "Floor";
        case NodeType::Ceil: return "Ceil";
        case NodeType::Fract: return "Fract";
        case NodeType::Sin: return "Sin";
        case NodeType::Cos: return "Cos";
        case NodeType::Dot: return "Dot";
        case NodeType::Cross: return "Cross";
        case NodeType::Normalize: return "Normalize";
        case NodeType::Length: return "Length";
        case NodeType::MakeVec2: return "MakeVec2";
        case NodeType::MakeVec3: return "MakeVec3";
        case NodeType::MakeVec4: return "MakeVec4";
        case NodeType::SplitVec2: return "SplitVec2";
        case NodeType::SplitVec3: return "SplitVec3";
        case NodeType::SplitVec4: return "SplitVec4";
        case NodeType::Fresnel: return "Fresnel";
        case NodeType::VertexNormal: return "VertexNormal";
        case NodeType::VertexUV: return "VertexUV";
        case NodeType::Time: return "Time";
        case NodeType::TextureSample: return "TextureSample";
        case NodeType::OrmSample: return "OrmSample";
        case NodeType::MixColor: return "MixColor";
        case NodeType::FloatToVec2: return "FloatToVec2";
        case NodeType::FloatToVec3: return "FloatToVec3";
        case NodeType::FloatToVec4: return "FloatToVec4";
        case NodeType::Vec2ToFloat: return "Vec2ToFloat";
        case NodeType::Vec3ToFloat: return "Vec3ToFloat";
        case NodeType::Vec4ToFloat: return "Vec4ToFloat";
        case NodeType::Vec2ToVec3: return "Vec2ToVec3";
        case NodeType::Vec2ToVec4: return "Vec2ToVec4";
        case NodeType::Vec3ToVec2: return "Vec3ToVec2";
        case NodeType::Vec3ToVec4: return "Vec3ToVec4";
        case NodeType::Vec4ToVec2: return "Vec4ToVec2";
        case NodeType::Vec4ToVec3: return "Vec4ToVec3";
        case NodeType::WorldPosition: return "WorldPosition";
        case NodeType::Panner: return "Panner";
        case NodeType::UVTransform: return "UVTransform";
        case NodeType::Remap: return "Remap";
        case NodeType::Flipbook: return "Flipbook";
        case NodeType::Rotator: return "Rotator";
        case NodeType::CustomRotator: return "CustomRotator";
        default: return "Unknown";
        }
    }

    inline NodeType stringToNodeType(const std::string& str)
    {
        if (str == "PBROutput") return NodeType::PBROutput;
        if (str == "ConstantScalar") return NodeType::ConstantScalar;
        if (str == "ConstantVec2") return NodeType::ConstantVec2;
        if (str == "ConstantVec3") return NodeType::ConstantVec3;
        if (str == "ConstantColor") return NodeType::ConstantColor;
        if (str == "Add") return NodeType::Add;
        if (str == "Subtract") return NodeType::Subtract;
        if (str == "Multiply") return NodeType::Multiply;
        if (str == "Divide") return NodeType::Divide;
        if (str == "Power") return NodeType::Power;
        if (str == "Lerp") return NodeType::Lerp;
        if (str == "Clamp") return NodeType::Clamp;
        if (str == "Saturate") return NodeType::Saturate;
        if (str == "OneMinus") return NodeType::OneMinus;
        if (str == "Abs") return NodeType::Abs;
        if (str == "Floor") return NodeType::Floor;
        if (str == "Ceil") return NodeType::Ceil;
        if (str == "Fract") return NodeType::Fract;
        if (str == "Sin") return NodeType::Sin;
        if (str == "Cos") return NodeType::Cos;
        if (str == "Dot") return NodeType::Dot;
        if (str == "Cross") return NodeType::Cross;
        if (str == "Normalize") return NodeType::Normalize;
        if (str == "Length") return NodeType::Length;
        if (str == "MakeVec2") return NodeType::MakeVec2;
        if (str == "MakeVec3") return NodeType::MakeVec3;
        if (str == "MakeVec4") return NodeType::MakeVec4;
        if (str == "SplitVec2") return NodeType::SplitVec2;
        if (str == "SplitVec3") return NodeType::SplitVec3;
        if (str == "SplitVec4") return NodeType::SplitVec4;
        if (str == "Fresnel") return NodeType::Fresnel;
        if (str == "VertexNormal") return NodeType::VertexNormal;
        if (str == "VertexUV") return NodeType::VertexUV;
        if (str == "Time") return NodeType::Time;
        if (str == "TextureSample") return NodeType::TextureSample;
        if (str == "OrmSample") return NodeType::OrmSample;
        if (str == "MixColor") return NodeType::MixColor;
        if (str == "FloatToVec2") return NodeType::FloatToVec2;
        if (str == "FloatToVec3") return NodeType::FloatToVec3;
        if (str == "FloatToVec4") return NodeType::FloatToVec4;
        if (str == "Vec2ToFloat") return NodeType::Vec2ToFloat;
        if (str == "Vec3ToFloat") return NodeType::Vec3ToFloat;
        if (str == "Vec4ToFloat") return NodeType::Vec4ToFloat;
        if (str == "Vec2ToVec3") return NodeType::Vec2ToVec3;
        if (str == "Vec2ToVec4") return NodeType::Vec2ToVec4;
        if (str == "Vec3ToVec2") return NodeType::Vec3ToVec2;
        if (str == "Vec3ToVec4") return NodeType::Vec3ToVec4;
        if (str == "Vec4ToVec2") return NodeType::Vec4ToVec2;
        if (str == "Vec4ToVec3") return NodeType::Vec4ToVec3;
        if (str == "WorldPosition") return NodeType::WorldPosition;
        if (str == "Panner") return NodeType::Panner;
        if (str == "UVTransform") return NodeType::UVTransform;
        if (str == "Remap") return NodeType::Remap;
        if (str == "Flipbook") return NodeType::Flipbook;
        if (str == "Rotator") return NodeType::Rotator;
        if (str == "CustomRotator") return NodeType::CustomRotator;
        return NodeType::ConstantScalar;
    }

    using NodeProperty = std::variant<float, glm::vec2, glm::vec3, glm::vec4, std::string>;

    // Exposed-parameter convention (format 1.1): constant and texture-sample nodes carrying
    // properties "isParameter" (float != 0) + "parameterName" (string) become named material
    // parameters. See MaterialParameterSet.hpp for collection/layout helpers.
    struct ShaderNode
    {
        uint32_t id = 0;
        NodeType type = NodeType::ConstantScalar;
        glm::vec2 position{0.0f};
        std::string name;

        std::map<std::string, NodeProperty> properties;

        std::vector<NodePin> inputs;
        std::vector<NodePin> outputs;
    };

    struct NodeLink
    {
        uint32_t id = 0;
        uint32_t sourceNodeId = 0;
        uint32_t targetNodeId = 0;
        std::string sourcePin;
        std::string targetPin;
    };

    struct ShaderGraph
    {
        std::vector<ShaderNode> nodes;
        std::vector<NodeLink> links;
        uint32_t nextNodeId = 1;
        uint32_t nextLinkId = 1;
        uint32_t nextPinId = 1;

        const ShaderNode* findOutputNode() const
        {
            for (const auto& node : nodes)
            {
                if (node.type == NodeType::PBROutput)
                {
                    return &node;
                }
            }
            return nullptr;
        }

        ShaderNode* findNode(uint32_t nodeId)
        {
            for (auto& node : nodes)
            {
                if (node.id == nodeId)
                {
                    return &node;
                }
            }
            return nullptr;
        }

        const ShaderNode* findNode(uint32_t nodeId) const
        {
            for (const auto& node : nodes)
            {
                if (node.id == nodeId)
                {
                    return &node;
                }
            }
            return nullptr;
        }
    };

    enum class BlendMode : uint8_t
    {
        Opaque,
        Masked,
        Translucent,
        Additive,
        Multiply
    };

    inline std::string blendModeToString(BlendMode mode)
    {
        switch (mode)
        {
        case BlendMode::Opaque: return "opaque";
        case BlendMode::Masked: return "masked";
        case BlendMode::Translucent: return "translucent";
        case BlendMode::Additive: return "additive";
        case BlendMode::Multiply: return "multiply";
        default: return "opaque";
        }
    }

    inline BlendMode stringToBlendMode(const std::string& str)
    {
        if (str == "masked") return BlendMode::Masked;
        if (str == "translucent") return BlendMode::Translucent;
        if (str == "additive") return BlendMode::Additive;
        if (str == "multiply") return BlendMode::Multiply;
        return BlendMode::Opaque;
    }

    inline bool isTransparentBlendMode(BlendMode mode)
    {
        return mode == BlendMode::Translucent ||
               mode == BlendMode::Additive ||
               mode == BlendMode::Multiply;
    }

    struct MaterialData
    {
        std::string uuid;
        std::string name;
        MaterialDomain domain = MaterialDomain::Surface;
        ShadingModel shadingModel = ShadingModel::DefaultLit;
        BlendMode blendMode = BlendMode::Opaque;
        float opacity = 1.0f;
        float alphaCutoff = 0.5f;
        std::map<std::string, bool> staticParameters;

        // Toon shading (VK-1493). `toonProfile` is a path to a reusable
        // `.vfToonProfile` asset — serialized only when shadingModel == Toon so
        // non-toon `.vfMat` files stay byte-identical. `toonProfileValues` is a
        // runtime-only resolved snapshot (NOT serialized) used by the material
        // preview UBO path; the authoritative values live in the referenced asset.
        std::string toonProfile;
        ToonProfile toonProfileValues;

        // Foliage wind (VK-1580). When `receiveWind` is true, this material's meshes sway
        // in the GPU-driven mesh path (painted/placed foliage trees & bushes) using the
        // shared, GLOBAL grass WindSystem — direction/strength/gust are scene-global, so
        // there are no per-material wind parameters. Serialized only when `receiveWind` is
        // true so non-foliage `.vfMat` files stay byte-identical.
        bool receiveWind = false;

        ShaderGraph graph;

        std::map<std::string, MaterialParameter> parameters;

        std::string cachedVertexShader;
        std::string cachedFragmentShader;
        std::string irHash;
        std::string shaderMapKey;
        bool gpuDrivenSupported = true;
        std::string gpuDrivenFallbackReason;

        bool needsRecompile = true;
    };

    inline bool endsWithIgnoreCase(std::string_view value, std::string_view suffix)
    {
        if (value.size() < suffix.size()) return false;
        const size_t offset = value.size() - suffix.size();
        for (size_t i = 0; i < suffix.size(); ++i)
        {
            unsigned char a = static_cast<unsigned char>(value[offset + i]);
            unsigned char b = static_cast<unsigned char>(suffix[i]);
            if (std::tolower(a) != std::tolower(b)) return false;
        }
        return true;
    }
}
