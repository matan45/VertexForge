#pragma once
#include "MaterialTypes.hpp"
#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace material
{
    // Node-property convention for exposed parameters (see MaterialTypes.hpp / ShaderNode):
    // a graph node becomes a named material parameter when it carries
    //   "isParameter"   (float, != 0)
    //   "parameterName" (non-empty string)
    // Optional UI range hints: "parameterMin" / "parameterMax" (floats, default 0/1).
    // Value-typed nodes (ConstantScalar/Vec2/Vec3/Color) map to members of a std140
    // uniform block; TextureSample/OrmSample nodes become named texture parameters
    // that resolve onto their existing texture slot.
    constexpr const char* PARAM_FLAG_PROPERTY = "isParameter";
    constexpr const char* PARAM_NAME_PROPERTY = "parameterName";
    constexpr const char* PARAM_MIN_PROPERTY = "parameterMin";
    constexpr const char* PARAM_MAX_PROPERTY = "parameterMax";

    // Descriptor slot for the generated parameter UBO — shared between the shader graph
    // compiler (GLSL emission) and the renderer (set layout + binding).
    inline constexpr uint32_t PARAMETER_DESCRIPTOR_SET = 2;
    inline constexpr uint32_t PARAMETER_DESCRIPTOR_BINDING = 0;

    struct ParameterDesc
    {
        std::string name;
        std::string glslName;
        ParameterType type = ParameterType::Scalar;
        ParameterValue defaultValue = 0.0f;
        float min = 0.0f;
        float max = 1.0f;
        uint32_t byteOffset = 0;
        uint32_t sourceNodeId = 0;
    };

    struct TextureParameterDesc
    {
        std::string name;
        int slot = 0;
        uint32_t sourceNodeId = 0;
        std::string defaultTexturePath;
    };

    struct MaterialParameterSet
    {
        std::vector<ParameterDesc> values;
        std::vector<TextureParameterDesc> textures;
        // Every flagged value node id -> the glsl member it reads (duplicate names of the
        // same type share one member, so the compiler can map any flagged node directly).
        std::map<uint32_t, std::string> nodeToGlslName;
        uint32_t uniformBlockSize = 0;

        bool hasValueParameters() const { return !values.empty(); }
        bool empty() const { return values.empty() && textures.empty(); }

        const ParameterDesc* find(std::string_view name) const;
        const TextureParameterDesc* findTexture(std::string_view name) const;
    };

    bool isParameterNode(const ShaderNode& node);
    std::string parameterNameOf(const ShaderNode& node);

    // Turns an arbitrary display name into a valid GLSL identifier ("My Tint" -> "My_Tint").
    std::string sanitizeGlslIdentifier(const std::string& name);

    // Scans the graph for flagged nodes and builds the parameter set with a deterministic
    // (name-sorted) std140 layout. Duplicate names with mismatched types are dropped with
    // a warning; only the first (lowest node id) definition wins.
    MaterialParameterSet collectParameters(const ShaderGraph& graph);

    // Emits the full GLSL uniform block declaration, e.g.
    //   layout(std140, set = 2, binding = 0) uniform MaterialParameterBlock { ... } uParams;
    // Returns an empty string when the set has no value parameters.
    std::string emitGlslUniformBlock(const MaterialParameterSet& set, uint32_t setIndex, uint32_t binding);

    // Writes default values (with optional per-name overrides) into a std140 byte block.
    // out must be at least uniformBlockSize bytes. Overrides with a mismatched value type
    // are ignored.
    void writeStd140(const MaterialParameterSet& set,
                     const std::map<std::string, ParameterValue>& overrides,
                     std::span<std::byte> out);
}
