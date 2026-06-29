#include "MaterialIR.hpp"
#include "../archive/VFPakFormat.hpp"
#include "../asset/AssetRef.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <format>
#include <optional>
#include <tuple>

namespace material
{
    namespace
    {
        std::string nodeStableId(uint32_t nodeId)
        {
            return "node:" + std::to_string(nodeId);
        }

        std::string pinStableId(uint32_t nodeId, PinKind kind, const std::string& pinName)
        {
            return nodeStableId(nodeId) + ":" +
                (kind == PinKind::Input ? "in:" : "out:") + pinName;
        }

        bool propertyLooksLikeTextureRef(const std::string& key)
        {
            std::string lower = key;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower.find("texturepath") != std::string::npos ||
                   (lower.find("texture") != std::string::npos && lower.ends_with("path"));
        }

        MaterialIRPropertyType propertyTypeFor(const NodeProperty& value, bool assetRef)
        {
            if (assetRef) return MaterialIRPropertyType::AssetReference;
            if (std::holds_alternative<float>(value)) return MaterialIRPropertyType::Scalar;
            if (std::holds_alternative<glm::vec2>(value)) return MaterialIRPropertyType::Vec2;
            if (std::holds_alternative<glm::vec3>(value)) return MaterialIRPropertyType::Vec3;
            if (std::holds_alternative<glm::vec4>(value)) return MaterialIRPropertyType::Vec4;
            return MaterialIRPropertyType::String;
        }

        MaterialIRAssetRef assetRefForPath(const std::string& path)
        {
            MaterialIRAssetRef out;
            out.path = path;
            if (path.empty()) return out;

            asset::AssetRef ref = asset::AssetRef::fromPath(path);
            if (ref.isValid())
            {
                out.guid = ref.toHexString();
                const std::string& resolved = ref.resolve();
                if (!resolved.empty())
                {
                    out.path = resolved;
                }
            }
            return out;
        }

        std::vector<MaterialIRProperty> normalizeProperties(const ShaderNode& node)
        {
            std::vector<MaterialIRProperty> properties;
            properties.reserve(node.properties.size());
            for (const auto& [key, value] : node.properties)
            {
                MaterialIRProperty prop;
                prop.name = key;
                prop.value = value;

                const std::string* str = std::get_if<std::string>(&value);
                const bool isAssetRef = str && propertyLooksLikeTextureRef(key);
                prop.type = propertyTypeFor(value, isAssetRef);
                if (isAssetRef)
                {
                    prop.assetRef = assetRefForPath(*str);
                }
                properties.push_back(std::move(prop));
            }
            return properties;
        }

        std::vector<MaterialIRPin> normalizePins(
            uint32_t nodeId,
            PinKind kind,
            const std::vector<NodePin>& pins)
        {
            std::vector<MaterialIRPin> out;
            out.reserve(pins.size());
            for (const auto& pin : pins)
            {
                MaterialIRPin irPin;
                irPin.stableId = pinStableId(nodeId, kind, pin.name);
                irPin.name = pin.name;
                irPin.type = pin.type;
                irPin.kind = kind;
                irPin.defaultValue = pin.defaultValue;
                out.push_back(std::move(irPin));
            }
            std::sort(out.begin(), out.end(),
                      [](const MaterialIRPin& a, const MaterialIRPin& b)
                      {
                          return a.stableId < b.stableId;
                      });
            return out;
        }

        const ShaderNode* findNode(const ShaderGraph& graph, uint32_t nodeId)
        {
            return graph.findNode(nodeId);
        }

        bool nodeCanReachOutput(const ShaderGraph& graph, uint32_t sourceNodeId, uint32_t outputNodeId)
        {
            std::vector<uint32_t> stack{sourceNodeId};
            std::vector<uint32_t> visited;
            size_t guard = 0;
            while (!stack.empty() && guard++ < graph.nodes.size() * 4 + 16)
            {
                uint32_t current = stack.back();
                stack.pop_back();
                if (std::find(visited.begin(), visited.end(), current) != visited.end()) continue;
                visited.push_back(current);

                if (current == outputNodeId) return true;
                for (const auto& link : graph.links)
                {
                    if (link.sourceNodeId == current)
                    {
                        stack.push_back(link.targetNodeId);
                    }
                }
            }
            return false;
        }

        bool isEmissionStrengthEvaluable(const ShaderGraph& graph, uint32_t nodeId, size_t depth = 0)
        {
            if (depth > 32) return false;
            const ShaderNode* node = findNode(graph, nodeId);
            if (!node) return false;

            switch (node->type)
            {
            case NodeType::ConstantScalar:
            case NodeType::Time:
                return true;
            case NodeType::Sin:
            case NodeType::Cos:
                for (const auto& link : graph.links)
                {
                    if (link.targetNodeId == nodeId && link.targetPin == "Value")
                    {
                        return isEmissionStrengthEvaluable(graph, link.sourceNodeId, depth + 1);
                    }
                }
                return true;
            case NodeType::Add:
            case NodeType::Multiply:
                {
                    bool sawInput = false;
                    for (const auto& link : graph.links)
                    {
                        if (link.targetNodeId == nodeId)
                        {
                            sawInput = true;
                            if (!isEmissionStrengthEvaluable(graph, link.sourceNodeId, depth + 1))
                            {
                                return false;
                            }
                        }
                    }
                    return sawInput;
                }
            default:
                return false;
            }
        }

        bool isDirectPBRInputSupported(const ShaderGraph& graph, const NodeLink& link)
        {
            const ShaderNode* source = findNode(graph, link.sourceNodeId);
            if (!source) return false;

            if (link.targetPin == "EmissionStrength")
            {
                return isEmissionStrengthEvaluable(graph, link.sourceNodeId);
            }

            switch (source->type)
            {
            case NodeType::ConstantScalar:
            case NodeType::ConstantVec2:
            case NodeType::ConstantVec3:
            case NodeType::ConstantColor:
            case NodeType::TextureSample:
            case NodeType::OrmSample:
                return true;
            default:
                return false;
            }
        }

        void appendBytes(std::vector<uint8_t>& out, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            out.insert(out.end(), bytes, bytes + size);
        }

        template<typename T>
        void appendValue(std::vector<uint8_t>& out, const T& value)
        {
            appendBytes(out, &value, sizeof(T));
        }

        void appendBool(std::vector<uint8_t>& out, bool value)
        {
            uint8_t bit = value ? 1u : 0u;
            appendValue(out, bit);
        }

        void appendString(std::vector<uint8_t>& out, const std::string& value)
        {
            uint32_t size = static_cast<uint32_t>(value.size());
            appendValue(out, size);
            appendBytes(out, value.data(), value.size());
        }

        void appendParameterValue(std::vector<uint8_t>& out, const ParameterValue& value)
        {
            uint8_t index = static_cast<uint8_t>(value.index());
            appendValue(out, index);
            std::visit([&](const auto& v)
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>)
                {
                    appendValue(out, v);
                }
                else
                {
                    appendBytes(out, &v, sizeof(T));
                }
            }, value);
        }

        void appendNodeProperty(std::vector<uint8_t>& out, const MaterialIRProperty& prop)
        {
            appendString(out, prop.name);
            appendValue(out, static_cast<uint8_t>(prop.type));
            if (prop.type == MaterialIRPropertyType::AssetReference)
            {
                appendString(out, prop.assetRef.guid);
                appendString(out, prop.assetRef.path);
                return;
            }

            uint8_t index = static_cast<uint8_t>(prop.value.index());
            appendValue(out, index);
            std::visit([&](const auto& v)
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>)
                {
                    appendValue(out, v);
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    appendString(out, v);
                }
                else
                {
                    appendBytes(out, &v, sizeof(T));
                }
            }, prop.value);
        }

        void appendParameterSet(std::vector<uint8_t>& out, const MaterialParameterSet& set)
        {
            appendValue(out, set.uniformBlockSize);
            uint32_t valueCount = static_cast<uint32_t>(set.values.size());
            appendValue(out, valueCount);
            for (const auto& desc : set.values)
            {
                appendString(out, desc.name);
                appendString(out, desc.glslName);
                appendValue(out, static_cast<uint8_t>(desc.type));
                appendParameterValue(out, desc.defaultValue);
                appendValue(out, desc.min);
                appendValue(out, desc.max);
                appendValue(out, desc.byteOffset);
                appendValue(out, desc.sourceNodeId);
            }

            uint32_t textureCount = static_cast<uint32_t>(set.textures.size());
            appendValue(out, textureCount);
            for (const auto& desc : set.textures)
            {
                appendString(out, desc.name);
                appendValue(out, desc.slot);
                appendValue(out, desc.sourceNodeId);
                appendString(out, desc.defaultTexturePath);
            }
        }
    }

    MaterialIR MaterialIRBuilder::fromMaterialData(const MaterialData& material)
    {
        MaterialIR ir;
        ir.domain = material.domain;
        ir.shadingModel = material.shadingModel;
        ir.blendMode = material.blendMode;
        ir.opacity = material.opacity;
        ir.alphaCutoff = material.alphaCutoff;
        ir.parameters = collectParameters(material.graph);

        for (const auto& [name, value] : material.staticParameters)
        {
            ir.staticParameters.push_back({name, value});
        }

        std::vector<const ShaderNode*> sortedNodes;
        sortedNodes.reserve(material.graph.nodes.size());
        for (const auto& node : material.graph.nodes)
        {
            sortedNodes.push_back(&node);
        }
        std::sort(sortedNodes.begin(), sortedNodes.end(),
                  [](const ShaderNode* a, const ShaderNode* b)
                  {
                      if (a->id != b->id) return a->id < b->id;
                      return nodeTypeToString(a->type) < nodeTypeToString(b->type);
                  });

        ir.nodes.reserve(sortedNodes.size());
        for (const ShaderNode* node : sortedNodes)
        {
            MaterialIRNode irNode;
            irNode.stableId = nodeStableId(node->id);
            irNode.sourceNodeId = node->id;
            irNode.op = node->type;
            irNode.displayName = node->name;
            irNode.properties = normalizeProperties(*node);
            irNode.inputs = normalizePins(node->id, PinKind::Input, node->inputs);
            irNode.outputs = normalizePins(node->id, PinKind::Output, node->outputs);
            ir.nodes.push_back(std::move(irNode));
        }

        std::vector<const NodeLink*> sortedLinks;
        sortedLinks.reserve(material.graph.links.size());
        for (const auto& link : material.graph.links)
        {
            sortedLinks.push_back(&link);
        }
        std::sort(sortedLinks.begin(), sortedLinks.end(),
                  [](const NodeLink* a, const NodeLink* b)
                  {
                      return std::tie(a->sourceNodeId, a->targetNodeId, a->sourcePin, a->targetPin, a->id) <
                             std::tie(b->sourceNodeId, b->targetNodeId, b->sourcePin, b->targetPin, b->id);
                  });

        ir.links.reserve(sortedLinks.size());
        for (const NodeLink* link : sortedLinks)
        {
            MaterialIRLink irLink;
            irLink.stableId = std::format(
                "link:{}:{}:{}:{}",
                link->sourceNodeId, link->sourcePin,
                link->targetNodeId, link->targetPin);
            irLink.sourceNodeId = nodeStableId(link->sourceNodeId);
            irLink.targetNodeId = nodeStableId(link->targetNodeId);
            irLink.sourcePinName = link->sourcePin;
            irLink.targetPinName = link->targetPin;
            irLink.sourcePinId = pinStableId(link->sourceNodeId, PinKind::Output, link->sourcePin);
            irLink.targetPinId = pinStableId(link->targetNodeId, PinKind::Input, link->targetPin);
            ir.links.push_back(std::move(irLink));
        }

        ir.gpuDrivenSupported = isGraphGPUDrivenSupported(material.graph, ir.gpuDrivenFallbackReason);
        return ir;
    }

    uint64_t MaterialIRBuilder::computeHash(const MaterialIR& ir)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(4096);

        appendValue(bytes, ir.version);
        appendValue(bytes, ir.compilerVersion);
        appendValue(bytes, static_cast<uint8_t>(ir.domain));
        appendValue(bytes, static_cast<uint8_t>(ir.shadingModel));
        appendValue(bytes, static_cast<uint8_t>(ir.blendMode));
        appendValue(bytes, ir.opacity);
        appendValue(bytes, ir.alphaCutoff);

        uint32_t staticCount = static_cast<uint32_t>(ir.staticParameters.size());
        appendValue(bytes, staticCount);
        for (const auto& param : ir.staticParameters)
        {
            appendString(bytes, param.name);
            appendBool(bytes, param.value);
        }

        uint32_t nodeCount = static_cast<uint32_t>(ir.nodes.size());
        appendValue(bytes, nodeCount);
        for (const auto& node : ir.nodes)
        {
            appendString(bytes, node.stableId);
            appendValue(bytes, node.sourceNodeId);
            appendValue(bytes, static_cast<uint8_t>(node.op));
            appendString(bytes, node.displayName);

            uint32_t propCount = static_cast<uint32_t>(node.properties.size());
            appendValue(bytes, propCount);
            for (const auto& prop : node.properties)
            {
                appendNodeProperty(bytes, prop);
            }

            auto appendPins = [&](const std::vector<MaterialIRPin>& pins)
            {
                uint32_t pinCount = static_cast<uint32_t>(pins.size());
                appendValue(bytes, pinCount);
                for (const auto& pin : pins)
                {
                    appendString(bytes, pin.stableId);
                    appendString(bytes, pin.name);
                    appendValue(bytes, static_cast<uint8_t>(pin.type));
                    appendValue(bytes, static_cast<uint8_t>(pin.kind));
                    bool hasDefault = pin.defaultValue.has_value();
                    appendBool(bytes, hasDefault);
                    if (pin.defaultValue)
                    {
                        appendParameterValue(bytes, *pin.defaultValue);
                    }
                }
            };
            appendPins(node.inputs);
            appendPins(node.outputs);
        }

        uint32_t linkCount = static_cast<uint32_t>(ir.links.size());
        appendValue(bytes, linkCount);
        for (const auto& link : ir.links)
        {
            appendString(bytes, link.stableId);
            appendString(bytes, link.sourceNodeId);
            appendString(bytes, link.targetNodeId);
            appendString(bytes, link.sourcePinId);
            appendString(bytes, link.targetPinId);
            appendString(bytes, link.sourcePinName);
            appendString(bytes, link.targetPinName);
        }

        appendParameterSet(bytes, ir.parameters);
        appendBool(bytes, ir.gpuDrivenSupported);
        appendString(bytes, ir.gpuDrivenFallbackReason);

        return archive::hashBytes(bytes.data(), bytes.size());
    }

    std::string MaterialIRBuilder::computeHashString(const MaterialIR& ir)
    {
        return formatHash(computeHash(ir));
    }

    std::string MaterialIRBuilder::computeHashString(const MaterialData& material)
    {
        return computeHashString(fromMaterialData(material));
    }

    std::string MaterialIRBuilder::formatHash(uint64_t hash)
    {
        char buf[17];
        snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
        return std::string(buf);
    }

    bool MaterialIRBuilder::isGraphGPUDrivenSupported(
        const ShaderGraph& graph,
        std::string& fallbackReason)
    {
        const ShaderNode* outputNode = graph.findOutputNode();
        if (!outputNode)
        {
            fallbackReason = "missing PBROutput node";
            return false;
        }

        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == outputNode->id)
            {
                if (!isDirectPBRInputSupported(graph, link))
                {
                    const ShaderNode* source = graph.findNode(link.sourceNodeId);
                    fallbackReason = "PBROutput." + link.targetPin +
                        " is driven by unsupported GPU-driven graph node " +
                        (source ? nodeTypeToString(source->type) : std::string("Unknown"));
                    return false;
                }
            }
        }

        for (const auto& node : graph.nodes)
        {
            if (!nodeCanReachOutput(graph, node.id, outputNode->id)) continue;

            switch (node.type)
            {
            case NodeType::PBROutput:
            case NodeType::ConstantScalar:
            case NodeType::ConstantVec2:
            case NodeType::ConstantVec3:
            case NodeType::ConstantColor:
            case NodeType::TextureSample:
            case NodeType::OrmSample:
            case NodeType::Time:
            case NodeType::Sin:
            case NodeType::Cos:
            case NodeType::Add:
            case NodeType::Multiply:
                break;
            default:
                fallbackReason = "unsupported GPU-driven graph node " + nodeTypeToString(node.type);
                return false;
            }
        }

        fallbackReason.clear();
        return true;
    }
}
