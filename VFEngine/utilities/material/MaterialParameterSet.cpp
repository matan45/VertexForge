#include "MaterialParameterSet.hpp"
#include "../print/Log.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace material
{
    namespace
    {
        struct TypeLayout
        {
            uint32_t alignment;
            uint32_t size;
        };

        TypeLayout std140Layout(ParameterType type)
        {
            switch (type)
            {
            case ParameterType::Scalar: return {4, 4};
            case ParameterType::Vec2: return {8, 8};
            case ParameterType::Vec3: return {16, 12};
            case ParameterType::Vec4:
            case ParameterType::Color: return {16, 16};
            default: return {4, 4};
            }
        }

        const char* glslTypeName(ParameterType type)
        {
            switch (type)
            {
            case ParameterType::Scalar: return "float";
            case ParameterType::Vec2: return "vec2";
            case ParameterType::Vec3: return "vec3";
            case ParameterType::Vec4:
            case ParameterType::Color: return "vec4";
            default: return "float";
            }
        }

        uint32_t alignUp(uint32_t value, uint32_t alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        bool valueMatchesType(const ParameterValue& value, ParameterType type)
        {
            switch (type)
            {
            case ParameterType::Scalar: return std::holds_alternative<float>(value);
            case ParameterType::Vec2: return std::holds_alternative<glm::vec2>(value);
            case ParameterType::Vec3: return std::holds_alternative<glm::vec3>(value);
            case ParameterType::Vec4:
            case ParameterType::Color: return std::holds_alternative<glm::vec4>(value);
            default: return false;
            }
        }

        bool isReservedGlslWord(const std::string& name)
        {
            static const std::array<const char*, 14> reserved = {
                "float", "int", "uint", "bool", "double",
                "vec2", "vec3", "vec4", "mat2", "mat3", "mat4",
                "sampler2D", "true", "false"
            };
            return std::any_of(reserved.begin(), reserved.end(),
                               [&](const char* word) { return name == word; });
        }

        float floatProperty(const ShaderNode& node, const char* key, float fallback)
        {
            auto it = node.properties.find(key);
            if (it != node.properties.end())
            {
                if (const float* value = std::get_if<float>(&it->second))
                {
                    return *value;
                }
            }
            return fallback;
        }

        std::string stringProperty(const ShaderNode& node, const char* key)
        {
            auto it = node.properties.find(key);
            if (it != node.properties.end())
            {
                if (const std::string* value = std::get_if<std::string>(&it->second))
                {
                    return *value;
                }
            }
            return {};
        }

        // Returns true for node types whose "value" property maps to a uniform member.
        bool isValueParameterType(NodeType type)
        {
            return type == NodeType::ConstantScalar ||
                   type == NodeType::ConstantVec2 ||
                   type == NodeType::ConstantVec3 ||
                   type == NodeType::ConstantColor;
        }

        bool isTextureParameterType(NodeType type)
        {
            return type == NodeType::TextureSample || type == NodeType::OrmSample;
        }

        ParameterType parameterTypeForNode(const ShaderNode& node)
        {
            switch (node.type)
            {
            case NodeType::ConstantScalar: return ParameterType::Scalar;
            case NodeType::ConstantVec2: return ParameterType::Vec2;
            case NodeType::ConstantVec3: return ParameterType::Vec3;
            case NodeType::ConstantColor: return ParameterType::Color;
            default: return ParameterType::Scalar;
            }
        }

        ParameterValue defaultValueForNode(const ShaderNode& node)
        {
            auto it = node.properties.find("value");
            if (it != node.properties.end())
            {
                if (const float* f = std::get_if<float>(&it->second)) return *f;
                if (const glm::vec2* v2 = std::get_if<glm::vec2>(&it->second)) return *v2;
                if (const glm::vec3* v3 = std::get_if<glm::vec3>(&it->second)) return *v3;
                if (const glm::vec4* v4 = std::get_if<glm::vec4>(&it->second)) return *v4;
            }
            switch (parameterTypeForNode(node))
            {
            case ParameterType::Scalar: return 0.0f;
            case ParameterType::Vec2: return glm::vec2(0.0f);
            case ParameterType::Vec3: return glm::vec3(0.0f);
            default: return glm::vec4(1.0f);
            }
        }
    }

    const ParameterDesc* MaterialParameterSet::find(std::string_view name) const
    {
        for (const auto& desc : values)
        {
            if (desc.name == name) return &desc;
        }
        return nullptr;
    }

    const TextureParameterDesc* MaterialParameterSet::findTexture(std::string_view name) const
    {
        for (const auto& desc : textures)
        {
            if (desc.name == name) return &desc;
        }
        return nullptr;
    }

    bool isParameterNode(const ShaderNode& node)
    {
        if (floatProperty(node, PARAM_FLAG_PROPERTY, 0.0f) == 0.0f) return false;
        return !parameterNameOf(node).empty();
    }

    std::string parameterNameOf(const ShaderNode& node)
    {
        std::string name = stringProperty(node, PARAM_NAME_PROPERTY);
        // Trim surrounding whitespace so " Tint " and "Tint" are the same parameter
        size_t begin = name.find_first_not_of(" \t");
        if (begin == std::string::npos) return {};
        size_t end = name.find_last_not_of(" \t");
        return name.substr(begin, end - begin + 1);
    }

    std::string sanitizeGlslIdentifier(const std::string& name)
    {
        std::string result;
        result.reserve(name.size());
        for (char c : name)
        {
            const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                               (c >= '0' && c <= '9') || c == '_';
            result += valid ? c : '_';
        }
        if (result.empty())
        {
            result = "param";
        }
        if (result[0] >= '0' && result[0] <= '9')
        {
            result.insert(result.begin(), '_');
        }
        if (isReservedGlslWord(result))
        {
            result += "Param";
        }
        return result;
    }

    MaterialParameterSet collectParameters(const ShaderGraph& graph)
    {
        MaterialParameterSet set;

        struct ValueEntry
        {
            ParameterDesc desc;
            std::vector<uint32_t> nodeIds;
        };
        std::vector<ValueEntry> entries;

        // Iterate in node-id order so "first definition wins" is deterministic
        std::vector<const ShaderNode*> sortedNodes;
        sortedNodes.reserve(graph.nodes.size());
        for (const auto& node : graph.nodes) sortedNodes.push_back(&node);
        std::sort(sortedNodes.begin(), sortedNodes.end(),
                  [](const ShaderNode* a, const ShaderNode* b) { return a->id < b->id; });

        for (const ShaderNode* node : sortedNodes)
        {
            if (!isParameterNode(*node)) continue;

            std::string name = parameterNameOf(*node);

            if (isValueParameterType(node->type))
            {
                ParameterType type = parameterTypeForNode(*node);
                auto existing = std::find_if(entries.begin(), entries.end(),
                                             [&](const ValueEntry& e) { return e.desc.name == name; });
                if (existing != entries.end())
                {
                    if (existing->desc.type == type)
                    {
                        existing->nodeIds.push_back(node->id);
                    }
                    else
                    {
                        vfLogWarning("Material parameter '{}' redefined with a different type on node {}; ignoring",
                                     name, node->id);
                    }
                    continue;
                }

                ValueEntry entry;
                entry.desc.name = name;
                entry.desc.type = type;
                entry.desc.defaultValue = defaultValueForNode(*node);
                entry.desc.min = floatProperty(*node, PARAM_MIN_PROPERTY, 0.0f);
                entry.desc.max = floatProperty(*node, PARAM_MAX_PROPERTY, 1.0f);
                entry.desc.sourceNodeId = node->id;
                entry.nodeIds.push_back(node->id);
                entries.push_back(std::move(entry));
            }
            else if (isTextureParameterType(node->type))
            {
                auto existing = std::find_if(set.textures.begin(), set.textures.end(),
                                             [&](const TextureParameterDesc& t) { return t.name == name; });
                if (existing != set.textures.end())
                {
                    vfLogWarning("Material texture parameter '{}' redefined on node {}; ignoring", name, node->id);
                    continue;
                }

                TextureParameterDesc desc;
                desc.name = name;
                desc.slot = static_cast<int>(floatProperty(*node, "textureIndex", 0.0f));
                desc.slot = std::clamp(desc.slot, 0, MAX_MATERIAL_TEXTURES - 1);
                desc.sourceNodeId = node->id;
                desc.defaultTexturePath = stringProperty(*node, "texturePath");
                set.textures.push_back(std::move(desc));
            }
        }

        // Deterministic layout: members sorted by display name
        std::sort(entries.begin(), entries.end(),
                  [](const ValueEntry& a, const ValueEntry& b) { return a.desc.name < b.desc.name; });

        std::vector<std::string> usedGlslNames;
        uint32_t cursor = 0;
        for (auto& entry : entries)
        {
            std::string glslName = sanitizeGlslIdentifier(entry.desc.name);
            std::string candidate = glslName;
            int suffix = 2;
            while (std::find(usedGlslNames.begin(), usedGlslNames.end(), candidate) != usedGlslNames.end())
            {
                candidate = glslName + "_" + std::to_string(suffix++);
            }
            usedGlslNames.push_back(candidate);
            entry.desc.glslName = candidate;

            TypeLayout layout = std140Layout(entry.desc.type);
            entry.desc.byteOffset = alignUp(cursor, layout.alignment);
            cursor = entry.desc.byteOffset + layout.size;

            for (uint32_t nodeId : entry.nodeIds)
            {
                set.nodeToGlslName[nodeId] = entry.desc.glslName;
            }
            set.values.push_back(std::move(entry.desc));
        }

        set.uniformBlockSize = set.values.empty() ? 0 : alignUp(cursor, 16);
        return set;
    }

    std::string emitGlslUniformBlock(const MaterialParameterSet& set, uint32_t setIndex, uint32_t binding)
    {
        if (!set.hasValueParameters()) return {};

        std::string code;
        code += "layout(std140, set = " + std::to_string(setIndex) +
                ", binding = " + std::to_string(binding) + ") uniform MaterialParameterBlock {\n";
        for (const auto& desc : set.values)
        {
            code += "    ";
            code += glslTypeName(desc.type);
            code += " ";
            code += desc.glslName;
            code += ";\n";
        }
        code += "} uParams;\n";
        return code;
    }

    std::optional<ParameterValue> overrideValueForNode(
        const ShaderNode& node,
        const std::map<std::string, ParameterValue>& overrides)
    {
        if (overrides.empty() || !isParameterNode(node)) return std::nullopt;

        auto it = overrides.find(parameterNameOf(node));
        if (it == overrides.end()) return std::nullopt;

        if (isValueParameterType(node.type) &&
            valueMatchesType(it->second, parameterTypeForNode(node)))
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::map<std::string, ParameterValue> resolveOverrides(
        const MaterialParameterSet& parentSet,
        const MaterialInstanceData* instance,
        const std::map<std::string, ParameterValue>* runtimeOverrides)
    {
        std::map<std::string, ParameterValue> resolved;

        auto apply = [&](const std::map<std::string, ParameterValue>& source)
        {
            for (const auto& [name, value] : source)
            {
                const ParameterDesc* desc = parentSet.find(name);
                if (!desc) continue; // stale name — kept on disk, skipped here
                if (!valueMatchesType(value, desc->type)) continue;
                resolved[name] = value;
            }
        };

        if (instance)
        {
            apply(instance->parameterOverrides);
        }
        if (runtimeOverrides)
        {
            apply(*runtimeOverrides);
        }
        return resolved;
    }

    std::map<TextureSlot, asset::AssetRef> resolveTextureOverrides(
        const MaterialParameterSet& parentSet,
        const MaterialInstanceData& instance)
    {
        std::map<TextureSlot, asset::AssetRef> resolved;

        for (const auto& [name, ref] : instance.textureParameterOverrides)
        {
            if (!ref.isValid()) continue;
            const TextureParameterDesc* desc = parentSet.findTexture(name);
            if (!desc) continue; // stale name
            resolved[static_cast<TextureSlot>(desc->slot)] = ref;
        }

        // Legacy slot-addressed overrides win over name-addressed ones
        for (const auto& [slot, ref] : instance.textureOverrides)
        {
            if (ref.isValid())
            {
                resolved[slot] = ref;
            }
        }
        return resolved;
    }

    bool isParameterWorldVisible(const ShaderGraph& graph, uint32_t sourceNodeId)
    {
        const ShaderNode* outputNode = graph.findOutputNode();
        if (!outputNode) return false;

        // Direct connection to any PBROutput pin — getConnectedValue folds these
        for (const auto& link : graph.links)
        {
            if (link.sourceNodeId == sourceNodeId && link.targetNodeId == outputNode->id)
            {
                return true;
            }
        }

        // Reaches EmissionStrength through the CPU-evaluable chain (evaluateFloatValue)
        auto isEvaluableNode = [](NodeType type)
        {
            return type == NodeType::Sin || type == NodeType::Cos ||
                   type == NodeType::Add || type == NodeType::Multiply ||
                   type == NodeType::ConstantScalar || type == NodeType::Time;
        };

        std::vector<uint32_t> frontier{sourceNodeId};
        std::vector<uint32_t> visited;
        size_t guard = 0;
        while (!frontier.empty() && guard++ < graph.nodes.size() * 4 + 16)
        {
            uint32_t current = frontier.back();
            frontier.pop_back();
            if (std::find(visited.begin(), visited.end(), current) != visited.end()) continue;
            visited.push_back(current);

            for (const auto& link : graph.links)
            {
                if (link.sourceNodeId != current) continue;
                if (link.targetNodeId == outputNode->id && link.targetPin == "EmissionStrength")
                {
                    return true;
                }
                const ShaderNode* target = graph.findNode(link.targetNodeId);
                if (target && isEvaluableNode(target->type))
                {
                    frontier.push_back(link.targetNodeId);
                }
            }
        }
        return false;
    }

    void writeStd140(const MaterialParameterSet& set,
                     const std::map<std::string, ParameterValue>& overrides,
                     std::span<std::byte> out)
    {
        if (out.size() < set.uniformBlockSize)
        {
            vfLogWarning("writeStd140: output buffer too small ({} < {})", out.size(), set.uniformBlockSize);
            return;
        }

        for (const auto& desc : set.values)
        {
            ParameterValue value = desc.defaultValue;
            auto overrideIt = overrides.find(desc.name);
            if (overrideIt != overrides.end())
            {
                if (valueMatchesType(overrideIt->second, desc.type))
                {
                    value = overrideIt->second;
                }
                else
                {
                    vfLogWarning("Material parameter override '{}' has mismatched type; using default", desc.name);
                }
            }

            std::byte* dst = out.data() + desc.byteOffset;
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>)
                {
                    std::memcpy(dst, &v, sizeof(float));
                }
                else
                {
                    std::memcpy(dst, &v, sizeof(T));
                }
            }, value);
        }
    }
}
