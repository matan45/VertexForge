#include "BehaviorTreeAsset.hpp"
#include "../print/Log.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <format>
#include <functional>

namespace behaviortree
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;
    using WarningLogger = std::function<void(const std::string&)>;

    // --- BlackboardValue serialization ---

    static json serializeBlackboardValue(const BlackboardValue& val, BlackboardValueType type)
    {
        switch (type)
        {
        case BlackboardValueType::Float:
            return std::holds_alternative<float>(val) ? std::get<float>(val) : 0.0f;
        case BlackboardValueType::Int:
            return std::holds_alternative<int32_t>(val) ? std::get<int32_t>(val) : 0;
        case BlackboardValueType::Bool:
            return std::holds_alternative<bool>(val) ? std::get<bool>(val) : false;
        case BlackboardValueType::String:
            return std::holds_alternative<std::string>(val) ? std::get<std::string>(val) : "";
        case BlackboardValueType::Vec3:
        {
            glm::vec3 v = std::holds_alternative<glm::vec3>(val) ? std::get<glm::vec3>(val) : glm::vec3{0.0f};
            return json::array({v.x, v.y, v.z});
        }
        case BlackboardValueType::Entity:
        {
            auto handle = std::holds_alternative<services::EntityHandle>(val)
                              ? std::get<services::EntityHandle>(val)
                              : services::EntityHandle::invalid();
            return handle.id;
        }
        default:
            return 0.0f;
        }
    }

    static BlackboardValue deserializeBlackboardValue(const json& j, BlackboardValueType type)
    {
        try
        {
            switch (type)
            {
            case BlackboardValueType::Float:
                return j.is_number() ? j.get<float>() : 0.0f;
            case BlackboardValueType::Int:
                return j.is_number_integer() ? j.get<int32_t>() : 0;
            case BlackboardValueType::Bool:
                return j.is_boolean() ? j.get<bool>() : false;
            case BlackboardValueType::String:
                return j.is_string() ? j.get<std::string>() : std::string{};
            case BlackboardValueType::Vec3:
            {
                if (j.is_array() && j.size() >= 3)
                {
                    return glm::vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
                }
                return glm::vec3{0.0f};
            }
            case BlackboardValueType::Entity:
            {
                services::EntityHandle handle;
                handle.id = j.is_number_unsigned() ? j.get<uint64_t>() : services::EntityHandle::INVALID_ID;
                return handle;
            }
            default:
                return 0.0f;
            }
        }
        catch (const json::exception&)
        {
            return 0.0f;
        }
    }

    // --- Properties serialization ---

    static json serializeProperties(const std::unordered_map<std::string, BlackboardValue>& properties)
    {
        json j = json::object();
        for (const auto& [key, val] : properties)
        {
            std::visit([&j, &key](auto&& arg)
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, float>)
                    j[key] = arg;
                else if constexpr (std::is_same_v<T, int32_t>)
                    j[key] = arg;
                else if constexpr (std::is_same_v<T, bool>)
                    j[key] = arg;
                else if constexpr (std::is_same_v<T, std::string>)
                    j[key] = arg;
                else if constexpr (std::is_same_v<T, glm::vec3>)
                    j[key] = json::array({arg.x, arg.y, arg.z});
                else if constexpr (std::is_same_v<T, services::EntityHandle>)
                    j[key] = arg.id;
            }, val);
        }
        return j;
    }

    static std::unordered_map<std::string, BlackboardValue> deserializeProperties(const json& j)
    {
        std::unordered_map<std::string, BlackboardValue> properties;
        if (!j.is_object()) return properties;

        for (auto it = j.begin(); it != j.end(); ++it)
        {
            const auto& val = it.value();
            if (val.is_boolean())
                properties[it.key()] = val.get<bool>();
            else if (val.is_number_integer())
                properties[it.key()] = val.get<int32_t>();
            else if (val.is_number())
                properties[it.key()] = val.get<float>();
            else if (val.is_string())
                properties[it.key()] = val.get<std::string>();
            else if (val.is_array() && val.size() >= 3)
                properties[it.key()] = glm::vec3{val[0].get<float>(), val[1].get<float>(), val[2].get<float>()};
        }
        return properties;
    }

    // --- Node serialization ---

    static json serializeNode(const BTNode& node)
    {
        json j;
        j["id"] = node.id;
        j["type"] = nodeTypeToString(node.type);
        j["name"] = node.name;
        j["position"] = json::array({node.position.x, node.position.y});

        if (!node.properties.empty())
        {
            j["properties"] = serializeProperties(node.properties);
        }

        if (!node.scriptPath.empty())
        {
            j["scriptPath"] = node.scriptPath;
        }
        if (!node.scriptClassName.empty())
        {
            j["scriptClassName"] = node.scriptClassName;
        }

        return j;
    }

    static BTNode deserializeNode(const json& j)
    {
        BTNode node;
        node.id = j.value("id", 0u);
        node.type = stringToNodeType(j.value("type", "Sequence"));
        node.name = j.value("name", "");

        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2)
        {
            node.position.x = j["position"][0].get<float>();
            node.position.y = j["position"][1].get<float>();
        }

        if (j.contains("properties") && j["properties"].is_object())
        {
            node.properties = deserializeProperties(j["properties"]);
        }

        node.scriptPath = j.value("scriptPath", "");
        node.scriptClassName = j.value("scriptClassName", "");

        return node;
    }

    // --- Link serialization ---

    static json serializeLink(const BTLink& link)
    {
        json j;
        j["id"] = link.id;
        j["source"] = link.sourceNodeId;
        j["target"] = link.targetNodeId;
        j["sortOrder"] = link.sortOrder;
        return j;
    }

    static BTLink deserializeLink(const json& j)
    {
        BTLink link;
        link.id = j.value("id", 0u);
        link.sourceNodeId = j.value("source", 0u);
        link.targetNodeId = j.value("target", 0u);
        link.sortOrder = j.value("sortOrder", 0u);
        return link;
    }

    // --- Blackboard key serialization ---

    static json serializeBlackboardKey(const BlackboardKeyDef& keyDef)
    {
        json j;
        j["name"] = keyDef.name;
        j["type"] = blackboardValueTypeToString(keyDef.type);
        j["default"] = serializeBlackboardValue(keyDef.defaultValue, keyDef.type);
        return j;
    }

    static BlackboardKeyDef deserializeBlackboardKey(const json& j)
    {
        BlackboardKeyDef keyDef;
        keyDef.name = j.value("name", "");
        keyDef.type = stringToBlackboardValueType(j.value("type", "Float"));
        if (j.contains("default"))
        {
            keyDef.defaultValue = deserializeBlackboardValue(j["default"], keyDef.type);
        }
        return keyDef;
    }

    // --- Parse helpers ---

    static void parseNodes(const json& j, BTGraph& graph, const WarningLogger& logWarning)
    {
        if (!j.contains("nodes") || !j["nodes"].is_array())
        {
            return;
        }

        for (size_t i = 0; i < j["nodes"].size(); ++i)
        {
            const auto& nodeJson = j["nodes"][i];
            if (!nodeJson.is_object())
            {
                logWarning(std::format("Node at index {} is not an object, skipping", i));
                continue;
            }
            BTNode node = deserializeNode(nodeJson);
            graph.nextNodeId = std::max(graph.nextNodeId, node.id + 1);
            graph.nodes.push_back(std::move(node));
        }
    }

    static void parseLinks(const json& j, BTGraph& graph, const WarningLogger& logWarning)
    {
        if (!j.contains("links") || !j["links"].is_array())
        {
            return;
        }

        for (size_t i = 0; i < j["links"].size(); ++i)
        {
            const auto& linkJson = j["links"][i];
            if (!linkJson.is_object())
            {
                logWarning(std::format("Link at index {} is not an object, skipping", i));
                continue;
            }
            BTLink link = deserializeLink(linkJson);
            graph.nextLinkId = std::max(graph.nextLinkId, link.id + 1);
            graph.links.push_back(std::move(link));
        }
    }

    static void parseBlackboardKeys(const json& j, BTGraph& graph, const WarningLogger& logWarning)
    {
        if (!j.contains("blackboardKeys") || !j["blackboardKeys"].is_array())
        {
            return;
        }

        for (size_t i = 0; i < j["blackboardKeys"].size(); ++i)
        {
            const auto& keyJson = j["blackboardKeys"][i];
            if (!keyJson.is_object())
            {
                logWarning(std::format("Blackboard key at index {} is not an object, skipping", i));
                continue;
            }
            graph.blackboardKeys.push_back(deserializeBlackboardKey(keyJson));
        }
    }

    static BehaviorTreeData parseBehaviorTreeData(const json& j, const WarningLogger& logWarning)
    {
        BehaviorTreeData data;
        data.version = j.value("version", BT_FORMAT_VERSION);
        data.name = j.value("name", "Unnamed Behavior Tree");

        if (data.name.empty())
        {
            data.name = "Unnamed Behavior Tree";
            logWarning("Behavior tree has empty name, using default");
        }

        data.graph.rootNodeId = j.value("rootNodeId", 0u);

        parseNodes(j, data.graph, logWarning);
        parseLinks(j, data.graph, logWarning);
        parseBlackboardKeys(j, data.graph, logWarning);

        if (data.graph.rootNodeId == 0 && !data.graph.nodes.empty())
        {
            // Find the root node
            for (const auto& node : data.graph.nodes)
            {
                if (node.type == BTNodeType::Root)
                {
                    data.graph.rootNodeId = node.id;
                    break;
                }
            }
        }

        return data;
    }

    static std::optional<json> readJsonFromFile(std::string_view path)
    {
        fs::path filePath(path);
        if (!fs::exists(filePath))
        {
            vfLogError("Behavior tree file not found: {}", path);
            return std::nullopt;
        }

        std::error_code ec;
        auto fileSize = fs::file_size(filePath, ec);
        if (ec)
        {
            vfLogError("Cannot read behavior tree file size '{}': {}", path, ec.message());
            return std::nullopt;
        }

        constexpr size_t MAX_BT_FILE_SIZE = 10 * 1024 * 1024;
        if (fileSize > MAX_BT_FILE_SIZE)
        {
            vfLogError("Behavior tree file '{}' is too large ({} bytes, max {} bytes)",
                       path, fileSize, MAX_BT_FILE_SIZE);
            return std::nullopt;
        }

        std::ifstream file(filePath);
        if (!file.is_open())
        {
            vfLogError("Failed to open behavior tree file: {}", path);
            return std::nullopt;
        }

        json j;
        try { file >> j; }
        catch (const json::parse_error& e)
        {
            vfLogError("Behavior tree file '{}' contains invalid JSON at byte {}: {}",
                       path, e.byte, e.what());
            return std::nullopt;
        }

        if (!j.is_object())
        {
            vfLogError("Behavior tree file '{}' must contain a JSON object at root level", path);
            return std::nullopt;
        }
        return j;
    }

    static json buildBehaviorTreeJson(const BehaviorTreeData& data)
    {
        json j;

        j["version"] = BT_FORMAT_VERSION;
        j["name"] = data.name;
        j["rootNodeId"] = data.graph.rootNodeId;

        json nodesJson = json::array();
        for (const auto& node : data.graph.nodes)
        {
            nodesJson.push_back(serializeNode(node));
        }
        j["nodes"] = nodesJson;

        json linksJson = json::array();
        for (const auto& link : data.graph.links)
        {
            linksJson.push_back(serializeLink(link));
        }
        j["links"] = linksJson;

        json bbKeysJson = json::array();
        for (const auto& keyDef : data.graph.blackboardKeys)
        {
            bbKeysJson.push_back(serializeBlackboardKey(keyDef));
        }
        j["blackboardKeys"] = bbKeysJson;

        return j;
    }

    // --- Public API ---

    std::optional<BehaviorTreeData> BehaviorTreeAsset::load(std::string_view path)
    {
        auto jsonOpt = readJsonFromFile(path);
        if (!jsonOpt.has_value())
        {
            return std::nullopt;
        }

        const json& j = jsonOpt.value();
        int warningCount = 0;
        constexpr int MAX_WARNINGS = 20;

        auto logWarningLimited = [&](const std::string& msg)
        {
            if (warningCount < MAX_WARNINGS)
            {
                vfLogWarning("{}", msg);
                warningCount++;
                if (warningCount == MAX_WARNINGS)
                {
                    vfLogWarning("(suppressing further warnings for this file)");
                }
            }
        };

        try
        {
            BehaviorTreeData data = parseBehaviorTreeData(j, logWarningLimited);

            if (warningCount > 0)
            {
                vfLogWarning("Loaded behavior tree '{}' with {} warning(s)", data.name, warningCount);
            }

            vfLogInfo("Loaded behavior tree '{}': {} nodes, {} links, {} blackboard keys",
                      data.name, data.graph.nodes.size(),
                      data.graph.links.size(), data.graph.blackboardKeys.size());

            return data;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse behavior tree file '{}': {}", path, e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Unexpected error loading behavior tree '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool BehaviorTreeAsset::save(std::string_view path, const BehaviorTreeData& data)
    {
        json j = buildBehaviorTreeJson(data);

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create behavior tree file: {}", path);
                return false;
            }

            file << j.dump(4);
            vfLogInfo("Saved behavior tree: {} to {}", data.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save behavior tree file {}: {}", path, e.what());
            return false;
        }
    }

    BehaviorTreeData BehaviorTreeAsset::createDefault(const std::string& name)
    {
        BehaviorTreeData data;
        data.name = name;
        data.version = BT_FORMAT_VERSION;

        // Create root node
        BTNode rootNode;
        rootNode.id = data.graph.nextNodeId++;
        rootNode.type = BTNodeType::Root;
        rootNode.name = "Root";
        rootNode.position = glm::vec2(300.0f, 50.0f);
        data.graph.rootNodeId = rootNode.id;
        data.graph.nodes.push_back(std::move(rootNode));

        // Create default sequence child
        BTNode sequenceNode;
        sequenceNode.id = data.graph.nextNodeId++;
        sequenceNode.type = BTNodeType::Sequence;
        sequenceNode.name = "Sequence";
        sequenceNode.position = glm::vec2(300.0f, 200.0f);
        data.graph.nodes.push_back(std::move(sequenceNode));

        // Link root -> sequence
        BTLink link;
        link.id = data.graph.nextLinkId++;
        link.sourceNodeId = 1; // root
        link.targetNodeId = 2; // sequence
        link.sortOrder = 0;
        data.graph.links.push_back(std::move(link));

        return data;
    }
}
