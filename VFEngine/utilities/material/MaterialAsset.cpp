#include "MaterialAsset.hpp"
#include "../print/EditorLogger.hpp"
#include "../uuid/UUID.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <format>

namespace material
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    json MaterialAsset::serializeProperty(const NodeProperty& prop)
    {
        return std::visit([](auto&& arg) -> json
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, float>)
            {
                return arg;
            }
            else if constexpr (std::is_same_v<T, glm::vec2>)
            {
                return json::array({arg.x, arg.y});
            }
            else if constexpr (std::is_same_v<T, glm::vec3>)
            {
                return json::array({arg.x, arg.y, arg.z});
            }
            else if constexpr (std::is_same_v<T, glm::vec4>)
            {
                return json::array({arg.x, arg.y, arg.z, arg.w});
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return arg;
            }
            return json();
        }, prop);
    }

    NodeProperty MaterialAsset::deserializeProperty(const json& j, const std::string& context)
    {
        try
        {
            if (j.is_number())
            {
                return j.get<float>();
            }
            else if (j.is_string())
            {
                return j.get<std::string>();
            }
            else if (j.is_array())
            {
                for (size_t i = 0; i < j.size(); ++i)
                {
                    if (!j[i].is_number())
                    {
                        vfLogWarning("Invalid array element at index {} in property{}, using default",
                                     i, context.empty() ? "" : " '" + context + "'");
                        return 0.0f;
                    }
                }
                if (j.size() == 2)
                {
                    return glm::vec2(j[0].get<float>(), j[1].get<float>());
                }
                else if (j.size() == 3)
                {
                    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
                }
                else if (j.size() == 4)
                {
                    return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
                }
                else
                {
                    vfLogWarning("Unexpected array size {} in property{}, using default",
                                 j.size(), context.empty() ? "" : " '" + context + "'");
                }
            }
            else if (!j.is_null())
            {
                vfLogWarning("Unexpected JSON type for property{}, using default",
                             context.empty() ? "" : " '" + context + "'");
            }
        }
        catch (const json::exception& e)
        {
            vfLogWarning("Failed to parse property{}: {}",
                         context.empty() ? "" : " '" + context + "'", e.what());
        }
        return 0.0f;
    }

    json MaterialAsset::serializeParamValue(const ParameterValue& val)
    {
        return std::visit([](auto&& arg) -> json
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, float>)
            {
                return arg;
            }
            else if constexpr (std::is_same_v<T, glm::vec2>)
            {
                return json::array({arg.x, arg.y});
            }
            else if constexpr (std::is_same_v<T, glm::vec3>)
            {
                return json::array({arg.x, arg.y, arg.z});
            }
            else if constexpr (std::is_same_v<T, glm::vec4>)
            {
                return json::array({arg.x, arg.y, arg.z, arg.w});
            }
            return json();
        }, val);
    }

    ParameterValue MaterialAsset::deserializeParamValue(const json& j, ParameterType type, const std::string& paramName)
    {
        try
        {
            switch (type)
            {
            case ParameterType::Scalar:
                if (!j.is_number())
                {
                    vfLogWarning("Parameter '{}' expected scalar, got different type", paramName);
                    return 0.0f;
                }
                return j.get<float>();
            case ParameterType::Vec2:
                if (!j.is_array() || j.size() < 2)
                {
                    vfLogWarning("Parameter '{}' expected vec2 array, using default", paramName);
                    return glm::vec2(0.0f);
                }
                return glm::vec2(j[0].get<float>(), j[1].get<float>());
            case ParameterType::Vec3:
                if (!j.is_array() || j.size() < 3)
                {
                    vfLogWarning("Parameter '{}' expected vec3 array, using default", paramName);
                    return glm::vec3(0.0f);
                }
                return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
            case ParameterType::Vec4:
            case ParameterType::Color:
                if (!j.is_array() || j.size() < 4)
                {
                    vfLogWarning("Parameter '{}' expected vec4 array, using default", paramName);
                    return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                }
                return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
            default:
                return 0.0f;
            }
        }
        catch (const json::exception& e)
        {
            vfLogWarning("Failed to parse parameter '{}': {}", paramName, e.what());
            switch (type)
            {
            case ParameterType::Vec2: return glm::vec2(0.0f);
            case ParameterType::Vec3: return glm::vec3(0.0f);
            case ParameterType::Vec4:
            case ParameterType::Color: return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            default: return 0.0f;
            }
        }
    }

    namespace
    {
        json serializeGraph(const MaterialData& material)
        {
            json graphJson;
            json nodesJson = json::array();
            for (const auto& node : material.graph.nodes)
            {
                json nodeJson;
                nodeJson["id"] = node.id;
                nodeJson["type"] = nodeTypeToString(node.type);
                nodeJson["name"] = node.name;
                nodeJson["position"] = json::array({node.position.x, node.position.y});

                if (!node.properties.empty())
                {
                    json propsJson;
                    for (const auto& [key, val] : node.properties)
                    {
                        propsJson[key] = MaterialAsset::serializeProperty(val);
                    }
                    nodeJson["properties"] = propsJson;
                }

                nodesJson.push_back(nodeJson);
            }
            graphJson["nodes"] = nodesJson;

            json linksJson = json::array();
            for (const auto& link : material.graph.links)
            {
                json linkJson;
                linkJson["id"] = link.id;
                linkJson["sourceNode"] = link.sourceNodeId;
                linkJson["targetNode"] = link.targetNodeId;
                linkJson["sourcePin"] = link.sourcePin;
                linkJson["targetPin"] = link.targetPin;
                linksJson.push_back(linkJson);
            }
            graphJson["links"] = linksJson;

            return graphJson;
        }

        bool writeJsonToFile(const json& j, std::string_view path, const std::string& materialName)
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create material file: {}", path);
                return false;
            }

            file << j.dump(4);

            // Flush to OS buffers before closing to avoid race conditions
            // where readers might see incomplete/stale data
            file.flush();
            if (!file.good())
            {
                vfLogError("Failed to flush material file: {}", path);
                return false;
            }

            file.close();
            if (file.fail())
            {
                vfLogError("Failed to close material file: {}", path);
                return false;
            }

            vfLogInfo("Saved material: {} to {}", materialName, path);
            return true;
        }
    } // anonymous namespace

    bool MaterialAsset::save(std::string_view path, const MaterialData& material)
    {
        json j;

        j["version"] = MATERIAL_FORMAT_VERSION;
        j["uuid"] = material.uuid;
        j["name"] = material.name;
        j["blendMode"] = blendModeToString(material.blendMode);
        j["graph"] = serializeGraph(material);

        if (!material.cachedVertexShader.empty() || !material.cachedFragmentShader.empty())
        {
            json cachedJson;
            cachedJson["vertexCode"] = material.cachedVertexShader;
            cachedJson["fragmentCode"] = material.cachedFragmentShader;
            j["cachedShader"] = cachedJson;
        }

        try
        {
            return writeJsonToFile(j, path, material.name);
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save material file {}: {}", path, e.what());
            return false;
        }
    }

    MaterialData MaterialAsset::createDefault(const std::string& name)
    {
        MaterialData material;
        material.uuid = std::to_string(uuid::UUID().getValue());
        material.name = name;
        material.blendMode = BlendMode::Opaque;
        material.needsRecompile = true;

        // Create only the PBR Output node - user will add other nodes
        ShaderNode outputNode;
        outputNode.id = material.graph.nextNodeId++;
        outputNode.type = NodeType::PBROutput;
        outputNode.name = "PBR Output";
        outputNode.position = glm::vec2(300.0f, 200.0f);

        material.graph.nodes.push_back(std::move(outputNode));

        return material;
    }
}
