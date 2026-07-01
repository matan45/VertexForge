#include "BTPropertyPanel.hpp"
#include "BTValueWidgets.hpp"
#include <imgui.h>
#include <array>

using namespace behaviortree;

namespace editor::windows
{
    namespace
    {
        BlackboardValueType resolveSelectedKeyType(const BTGraph* graph, const std::string& key)
        {
            return bt::resolveKeyType(graph, key);
        }

        bool drawTypedNodeValue(BTNode& node,
                                const char* propertyName,
                                const char* label,
                                BlackboardValueType type)
        {
            BlackboardValue value = bt::defaultValueForType(type);
            auto it = node.properties.find(propertyName);
            if (it != node.properties.end())
            {
                value = it->second;
            }

            if (bt::drawBlackboardValueWidget(label, type, value))
            {
                node.properties[propertyName] = value;
                return true;
            }
            return false;
        }
    }

    void BTPropertyPanel::notifyChanged()
    {
        if (onPropertyChanged) onPropertyChanged();
    }

    void BTPropertyPanel::draw(BTNode* node, const BTGraph* graph)
    {
        if (!node)
        {
            ImGui::TextDisabled("Select a node to edit properties");
            return;
        }

        ImGui::Text("Node: %s", node->name.empty() ? nodeTypeToString(node->type) : node->name.c_str());
        ImGui::Text("Type: %s", nodeTypeToString(node->type));
        ImGui::Separator();

        // Editable name
        char nameBuf[128];
        strncpy(nameBuf, node->name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        {
            node->name = nameBuf;
            notifyChanged();
        }

        ImGui::Separator();

        // Type-specific properties
        switch (node->type)
        {
        case BTNodeType::Wait: drawWaitProperties(*node); break;
        case BTNodeType::Log: drawLogProperties(*node); break;
        case BTNodeType::MoveTo: drawMoveToProperties(*node, graph); break;
        case BTNodeType::PlayAnimation: drawPlayAnimationProperties(*node); break;
        case BTNodeType::Parallel: drawParallelProperties(*node); break;
        case BTNodeType::Repeater: drawRepeaterProperties(*node); break;
        case BTNodeType::Cooldown: drawCooldownProperties(*node); break;
        case BTNodeType::TimeLimit: drawTimeLimitProperties(*node); break;
        case BTNodeType::ScriptTask: drawScriptTaskProperties(*node); break;
        case BTNodeType::SetBlackboardValue: drawSetBlackboardProperties(*node, graph); break;
        case BTNodeType::CheckBlackboardValue: drawCheckBlackboardProperties(*node, graph); break;
        case BTNodeType::LineOfSight: drawLineOfSightProperties(*node, graph); break;
        case BTNodeType::BlackboardCondition: drawBlackboardConditionProperties(*node, graph); break;
        case BTNodeType::EnvironmentQuery: drawEnvironmentQueryProperties(*node, graph); break;
        case BTNodeType::SubTree: drawSubTreeProperties(*node); break;
        case BTNodeType::DynamicSubTree: drawDynamicSubTreeProperties(*node); break;
        case BTNodeType::Service: drawServiceProperties(*node, graph); break;
        default: break;
        }
    }

    void BTPropertyPanel::drawWaitProperties(BTNode& node)
    {
        float duration = 1.0f;
        auto it = node.properties.find("duration");
        if (it != node.properties.end() && std::holds_alternative<float>(it->second))
            duration = std::get<float>(it->second);

        if (ImGui::DragFloat("Duration (s)", &duration, 0.1f, 0.0f, 60.0f))
        {
            node.properties["duration"] = duration;
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawLogProperties(BTNode& node)
    {
        // Message
        std::string message = "Hello";
        auto msgIt = node.properties.find("message");
        if (msgIt != node.properties.end() && std::holds_alternative<std::string>(msgIt->second))
            message = std::get<std::string>(msgIt->second);

        char msgBuf[256];
        strncpy(msgBuf, message.c_str(), sizeof(msgBuf) - 1);
        msgBuf[sizeof(msgBuf) - 1] = '\0';
        if (ImGui::InputText("Message", msgBuf, sizeof(msgBuf)))
        {
            node.properties["message"] = std::string(msgBuf);
            notifyChanged();
        }

        // Level
        std::string levelStr = "Info";
        auto lvlIt = node.properties.find("level");
        if (lvlIt != node.properties.end() && std::holds_alternative<std::string>(lvlIt->second))
            levelStr = std::get<std::string>(lvlIt->second);

        static const std::array<const char*, 3> levels = {"Info", "Warn", "Error"};
        int currentLevel = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (levelStr == levels[i]) { currentLevel = i; break; }
        }
        if (ImGui::Combo("Level", &currentLevel, levels.data(), static_cast<int>(levels.size())))
        {
            node.properties["level"] = std::string(levels[currentLevel]);
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawMoveToProperties(BTNode& node, const BTGraph* graph)
    {
        // Target key dropdown from blackboard keys (Vec3), free-text fallback when none exist.
        const auto isVec3 = [](BlackboardValueType t) { return t == BlackboardValueType::Vec3; };
        if (bt::drawKeyDropdown(node, graph, "Target Key", "targetKey", "target", isVec3))
            notifyChanged();

        float arrivalDist = 0.5f;
        auto distIt = node.properties.find("arrivalDistance");
        if (distIt != node.properties.end() && std::holds_alternative<float>(distIt->second))
            arrivalDist = std::get<float>(distIt->second);

        if (ImGui::DragFloat("Arrival Distance", &arrivalDist, 0.1f, 0.0f, 100.0f))
        {
            node.properties["arrivalDistance"] = arrivalDist;
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawPlayAnimationProperties(BTNode& node)
    {
        std::string stateName;
        auto nameIt = node.properties.find("stateName");
        if (nameIt != node.properties.end() && std::holds_alternative<std::string>(nameIt->second))
            stateName = std::get<std::string>(nameIt->second);

        char nameBuf[128];
        strncpy(nameBuf, stateName.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("State Name", nameBuf, sizeof(nameBuf)))
        {
            node.properties["stateName"] = std::string(nameBuf);
            notifyChanged();
        }

        bool waitCompletion = false;
        auto waitIt = node.properties.find("waitForCompletion");
        if (waitIt != node.properties.end() && std::holds_alternative<bool>(waitIt->second))
            waitCompletion = std::get<bool>(waitIt->second);

        if (ImGui::Checkbox("Wait for Completion", &waitCompletion))
        {
            node.properties["waitForCompletion"] = waitCompletion;
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawParallelProperties(BTNode& node)
    {
        std::string policyStr = "RequireAll";
        auto it = node.properties.find("policy");
        if (it != node.properties.end() && std::holds_alternative<std::string>(it->second))
            policyStr = std::get<std::string>(it->second);

        static const std::array<const char*, 2> policies = {"RequireAll", "RequireOne"};
        int current = policyStr == "RequireOne" ? 1 : 0;
        if (ImGui::Combo("Policy", &current, policies.data(), static_cast<int>(policies.size())))
        {
            node.properties["policy"] = std::string(policies[current]);
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawRepeaterProperties(BTNode& node)
    {
        int count = 3;
        auto it = node.properties.find("repeatCount");
        if (it != node.properties.end() && std::holds_alternative<int32_t>(it->second))
            count = std::get<int32_t>(it->second);

        if (ImGui::DragInt("Repeat Count", &count, 1, 0, 1000))
        {
            node.properties["repeatCount"] = static_cast<int32_t>(count);
            notifyChanged();
        }
        ImGui::TextDisabled("0 = infinite");
    }

    void BTPropertyPanel::drawCooldownProperties(BTNode& node)
    {
        float time = 1.0f;
        auto it = node.properties.find("cooldownTime");
        if (it != node.properties.end() && std::holds_alternative<float>(it->second))
            time = std::get<float>(it->second);

        if (ImGui::DragFloat("Cooldown (s)", &time, 0.1f, 0.0f, 60.0f))
        {
            node.properties["cooldownTime"] = time;
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawTimeLimitProperties(BTNode& node)
    {
        float time = 5.0f;
        auto it = node.properties.find("timeLimit");
        if (it != node.properties.end() && std::holds_alternative<float>(it->second))
            time = std::get<float>(it->second);

        if (ImGui::DragFloat("Time Limit (s)", &time, 0.1f, 0.0f, 300.0f))
        {
            node.properties["timeLimit"] = time;
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawScriptTaskProperties(BTNode& node)
    {
        // Script path
        char pathBuf[256];
        strncpy(pathBuf, node.scriptPath.c_str(), sizeof(pathBuf) - 1);
        pathBuf[sizeof(pathBuf) - 1] = '\0';
        if (ImGui::InputText("Script Path", pathBuf, sizeof(pathBuf)))
        {
            node.scriptPath = pathBuf;
            notifyChanged();
        }

        // Class name
        char classBuf[128];
        strncpy(classBuf, node.scriptClassName.c_str(), sizeof(classBuf) - 1);
        classBuf[sizeof(classBuf) - 1] = '\0';
        if (ImGui::InputText("Class Name", classBuf, sizeof(classBuf)))
        {
            node.scriptClassName = classBuf;
            notifyChanged();
        }

        ImGui::TextDisabled("Script must implement tick(float): string");
    }

    void BTPropertyPanel::drawSetBlackboardProperties(BTNode& node, const BTGraph* graph)
    {
        // Key
        std::string key;
        auto keyIt = node.properties.find("key");
        if (keyIt != node.properties.end() && std::holds_alternative<std::string>(keyIt->second))
            key = std::get<std::string>(keyIt->second);

        if (graph && !graph->blackboardKeys.empty())
        {
            if (ImGui::BeginCombo("Key", key.c_str()))
            {
                for (const auto& keyDef : graph->blackboardKeys)
                {
                    bool selected = (keyDef.name == key);
                    // Only reset the stored value on a GENUINE key change, and only when the existing
                    // value can't hold the new key's type — re-selecting the same key (Selectable fires on
                    // every click) must not wipe a configured value (VK-1457 fix).
                    if (ImGui::Selectable(keyDef.name.c_str(), selected) && keyDef.name != key)
                    {
                        node.properties["key"] = keyDef.name;
                        auto valIt = node.properties.find("value");
                        if (valIt == node.properties.end() || bt::typeOfValue(valIt->second) != keyDef.type)
                            node.properties["value"] = bt::defaultValueForType(keyDef.type);
                        notifyChanged();
                    }
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            char keyBuf[64];
            strncpy(keyBuf, key.c_str(), sizeof(keyBuf) - 1);
            keyBuf[sizeof(keyBuf) - 1] = '\0';
            if (ImGui::InputText("Key", keyBuf, sizeof(keyBuf)))
            {
                node.properties["key"] = std::string(keyBuf);
                notifyChanged();
            }
        }

        if (drawTypedNodeValue(node, "value", "Value", resolveSelectedKeyType(graph, key)))
        {
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawCheckBlackboardProperties(BTNode& node, const BTGraph* graph)
    {
        // Key
        std::string key;
        auto keyIt = node.properties.find("key");
        if (keyIt != node.properties.end() && std::holds_alternative<std::string>(keyIt->second))
            key = std::get<std::string>(keyIt->second);

        if (graph && !graph->blackboardKeys.empty())
        {
            if (ImGui::BeginCombo("Key", key.c_str()))
            {
                for (const auto& keyDef : graph->blackboardKeys)
                {
                    bool selected = (keyDef.name == key);
                    // Only reset compareValue on a GENUINE key change, and only when the existing value
                    // can't hold the new key's type — re-selecting the same key must not wipe it (VK-1457).
                    if (ImGui::Selectable(keyDef.name.c_str(), selected) && keyDef.name != key)
                    {
                        node.properties["key"] = keyDef.name;
                        auto valIt = node.properties.find("compareValue");
                        if (valIt == node.properties.end() || bt::typeOfValue(valIt->second) != keyDef.type)
                            node.properties["compareValue"] = bt::defaultValueForType(keyDef.type);
                        notifyChanged();
                    }
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            char keyBuf[64];
            strncpy(keyBuf, key.c_str(), sizeof(keyBuf) - 1);
            keyBuf[sizeof(keyBuf) - 1] = '\0';
            if (ImGui::InputText("Key", keyBuf, sizeof(keyBuf)))
            {
                node.properties["key"] = std::string(keyBuf);
                notifyChanged();
            }
        }

        // Compare operator
        std::string opStr = "==";
        auto opIt = node.properties.find("compareOp");
        if (opIt != node.properties.end() && std::holds_alternative<std::string>(opIt->second))
            opStr = std::get<std::string>(opIt->second);

        static const std::array<const char*, 6> ops = {"==", "!=", ">", "<", ">=", "<="};
        int currentOp = 0;
        for (int i = 0; i < 6; ++i)
        {
            if (opStr == ops[i]) { currentOp = i; break; }
        }
        if (ImGui::Combo("Operator", &currentOp, ops.data(), static_cast<int>(ops.size())))
        {
            node.properties["compareOp"] = std::string(ops[currentOp]);
            notifyChanged();
        }

        if (drawTypedNodeValue(node, "compareValue", "Compare Value", resolveSelectedKeyType(graph, key)))
        {
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawBlackboardConditionProperties(BTNode& node, const BTGraph* graph)
    {
        // Key
        std::string key;
        auto keyIt = node.properties.find("key");
        if (keyIt != node.properties.end() && std::holds_alternative<std::string>(keyIt->second))
            key = std::get<std::string>(keyIt->second);

        if (graph && !graph->blackboardKeys.empty())
        {
            if (ImGui::BeginCombo("Key", key.c_str()))
            {
                for (const auto& keyDef : graph->blackboardKeys)
                {
                    bool selected = (keyDef.name == key);
                    // Only reset compareValue on a GENUINE key change, and only when the existing value
                    // can't hold the new key's type — re-selecting the same key must not wipe it (VK-1457).
                    if (ImGui::Selectable(keyDef.name.c_str(), selected) && keyDef.name != key)
                    {
                        node.properties["key"] = keyDef.name;
                        auto valIt = node.properties.find("compareValue");
                        if (valIt == node.properties.end() || bt::typeOfValue(valIt->second) != keyDef.type)
                            node.properties["compareValue"] = bt::defaultValueForType(keyDef.type);
                        notifyChanged();
                    }
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            char keyBuf[64];
            strncpy(keyBuf, key.c_str(), sizeof(keyBuf) - 1);
            keyBuf[sizeof(keyBuf) - 1] = '\0';
            if (ImGui::InputText("Key", keyBuf, sizeof(keyBuf)))
            {
                node.properties["key"] = std::string(keyBuf);
                notifyChanged();
            }
        }

        // Compare operator
        std::string opStr = "==";
        auto opIt = node.properties.find("compareOp");
        if (opIt != node.properties.end() && std::holds_alternative<std::string>(opIt->second))
            opStr = std::get<std::string>(opIt->second);

        static const std::array<const char*, 6> ops = {"==", "!=", ">", "<", ">=", "<="};
        int currentOp = 0;
        for (int i = 0; i < 6; ++i)
        {
            if (opStr == ops[i]) { currentOp = i; break; }
        }
        if (ImGui::Combo("Operator", &currentOp, ops.data(), static_cast<int>(ops.size())))
        {
            node.properties["compareOp"] = std::string(ops[currentOp]);
            notifyChanged();
        }

        if (drawTypedNodeValue(node, "compareValue", "Compare Value", resolveSelectedKeyType(graph, key)))
        {
            notifyChanged();
        }

        // Abort mode
        std::string modeStr = "None";
        auto modeIt = node.properties.find("abortMode");
        if (modeIt != node.properties.end() && std::holds_alternative<std::string>(modeIt->second))
            modeStr = std::get<std::string>(modeIt->second);

        static const std::array<const char*, 4> modes = {"None", "Self", "LowerPriority", "Both"};
        int currentMode = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (modeStr == modes[i]) { currentMode = i; break; }
        }
        if (ImGui::Combo("Abort Mode", &currentMode, modes.data(), static_cast<int>(modes.size())))
        {
            node.properties["abortMode"] = std::string(modes[currentMode]);
            notifyChanged();
        }
        ImGui::TextDisabled("Self: abort own subtree when condition turns false");
        ImGui::TextDisabled("LowerPriority: preempt running lower Selector branch");
    }

    void BTPropertyPanel::drawEnvironmentQueryProperties(BTNode& node, const BTGraph* graph)
    {
        // Query name
        std::string queryName;
        auto qIt = node.properties.find("queryName");
        if (qIt != node.properties.end() && std::holds_alternative<std::string>(qIt->second))
            queryName = std::get<std::string>(qIt->second);

        char queryBuf[128];
        strncpy(queryBuf, queryName.c_str(), sizeof(queryBuf) - 1);
        queryBuf[sizeof(queryBuf) - 1] = '\0';
        if (ImGui::InputText("Query Name", queryBuf, sizeof(queryBuf)))
        {
            node.properties["queryName"] = std::string(queryBuf);
            notifyChanged();
        }

        // Result key (Vec3 blackboard keys), free-text fallback when none exist.
        const auto isVec3 = [](BlackboardValueType t) { return t == BlackboardValueType::Vec3; };
        if (bt::drawKeyDropdown(node, graph, "Result Key", "resultKey", "eqsResult", isVec3))
            notifyChanged();
        ImGui::TextDisabled("Best query position is written to the result key");
    }

    void BTPropertyPanel::drawSubTreeProperties(BTNode& node)
    {
        std::string treePath;
        auto pIt = node.properties.find("treePath");
        if (pIt != node.properties.end() && std::holds_alternative<std::string>(pIt->second))
            treePath = std::get<std::string>(pIt->second);

        char pathBuf[256];
        strncpy(pathBuf, treePath.c_str(), sizeof(pathBuf) - 1);
        pathBuf[sizeof(pathBuf) - 1] = '\0';
        if (ImGui::InputText("Tree Path", pathBuf, sizeof(pathBuf)))
        {
            node.properties["treePath"] = std::string(pathBuf);
            notifyChanged();
        }

        ImGui::TextDisabled("Inlined at load time; blackboard keys are merged");
        ImGui::TextDisabled("(parent wins on name collision)");
    }

    void BTPropertyPanel::drawDynamicSubTreeProperties(BTNode& node)
    {
        auto editStringProp = [this, &node](const char* label, const char* key)
        {
            std::string current;
            auto it = node.properties.find(key);
            if (it != node.properties.end() && std::holds_alternative<std::string>(it->second))
                current = std::get<std::string>(it->second);

            char buf[256];
            strncpy(buf, current.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText(label, buf, sizeof(buf)))
            {
                node.properties[key] = std::string(buf);
                notifyChanged();
            }
        };

        // Resolution order (highest priority first): selection key > injection tag > default path.
        editStringProp("Selection Key", "selectionKey");
        editStringProp("Injection Tag", "injectionTag");
        editStringProp("Default Tree Path", "defaultTreePath");
        ImGui::TextDisabled("Resolves: blackboard[Selection Key] >");
        ImGui::TextDisabled("injection[Tag] > Default Tree Path");

        ImGui::Separator();
        ImGui::Text("Blackboard Mappings");
        ImGui::TextDisabled("Explicit parent<->child value bindings");

        static const std::array<const char*, 3> directionNames = {"In", "Out", "InOut"};

        int removeIdx = -1;
        for (int i = 0; i < static_cast<int>(node.blackboardMappings.size()); ++i)
        {
            ImGui::PushID(i);
            auto& mapping = node.blackboardMappings[static_cast<size_t>(i)];

            char parentBuf[64];
            strncpy(parentBuf, mapping.parentKey.c_str(), sizeof(parentBuf) - 1);
            parentBuf[sizeof(parentBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputText("##parent", parentBuf, sizeof(parentBuf)))
            {
                mapping.parentKey = parentBuf;
                notifyChanged();
            }

            ImGui::SameLine();
            int dir = static_cast<int>(mapping.direction);
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::Combo("##dir", &dir, directionNames.data(), static_cast<int>(directionNames.size())))
            {
                mapping.direction = static_cast<MappingDirection>(dir);
                notifyChanged();
            }

            ImGui::SameLine();
            char childBuf[64];
            strncpy(childBuf, mapping.childKey.c_str(), sizeof(childBuf) - 1);
            childBuf[sizeof(childBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputText("##child", childBuf, sizeof(childBuf)))
            {
                mapping.childKey = childBuf;
                notifyChanged();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                removeIdx = i;
            }

            ImGui::PopID();
        }

        if (removeIdx >= 0)
        {
            node.blackboardMappings.erase(node.blackboardMappings.begin() + removeIdx);
            notifyChanged();
        }

        if (ImGui::Button("+ Add Mapping"))
        {
            node.blackboardMappings.push_back({"parentKey", "childKey", MappingDirection::In});
            notifyChanged();
        }
    }

    void BTPropertyPanel::drawLineOfSightProperties(BTNode& node, const BTGraph* graph)
    {
        // Target key (Entity or Vec3 from blackboard), free-text fallback when none exist.
        const auto isEntityOrVec3 = [](BlackboardValueType t)
        { return t == BlackboardValueType::Entity || t == BlackboardValueType::Vec3; };
        if (bt::drawKeyDropdown(node, graph, "Target Key", "targetKey", "target", isEntityOrVec3))
            notifyChanged();

        // Max distance
        float maxDist = 50.0f;
        auto distIt = node.properties.find("maxDistance");
        if (distIt != node.properties.end() && std::holds_alternative<float>(distIt->second))
            maxDist = std::get<float>(distIt->second);

        if (ImGui::DragFloat("Max Distance", &maxDist, 1.0f, 0.0f, 500.0f))
        {
            node.properties["maxDistance"] = maxDist;
            notifyChanged();
        }

        // Eye offset
        float eyeOffset = 1.6f;
        auto eyeIt = node.properties.find("eyeOffset");
        if (eyeIt != node.properties.end() && std::holds_alternative<float>(eyeIt->second))
            eyeOffset = std::get<float>(eyeIt->second);

        if (ImGui::DragFloat("Eye Offset", &eyeOffset, 0.1f, 0.0f, 10.0f))
        {
            node.properties["eyeOffset"] = eyeOffset;
            notifyChanged();
        }
        ImGui::TextDisabled("Height offset for ray origin (eye level)");
    }

    void BTPropertyPanel::drawServiceProperties(BTNode& node, const BTGraph* graph)
    {
        // Blackboard-key dropdowns for this service use the shared bt::drawKeyDropdown helper (below).

        // Interval
        float interval = 0.5f;
        auto intervalIt = node.properties.find("interval");
        if (intervalIt != node.properties.end() && std::holds_alternative<float>(intervalIt->second))
            interval = std::get<float>(intervalIt->second);
        if (ImGui::DragFloat("Interval (s)", &interval, 0.05f, 0.01f, 60.0f))
        {
            node.properties["interval"] = interval;
            notifyChanged();
        }

        // Random deviation
        float deviation = 0.0f;
        auto devIt = node.properties.find("randomDeviation");
        if (devIt != node.properties.end() && std::holds_alternative<float>(devIt->second))
            deviation = std::get<float>(devIt->second);
        if (ImGui::DragFloat("Random Deviation (s)", &deviation, 0.05f, 0.0f, 60.0f))
        {
            node.properties["randomDeviation"] = deviation;
            notifyChanged();
        }

        // Run on activation
        bool runOnActivation = false;
        auto runIt = node.properties.find("runOnActivation");
        if (runIt != node.properties.end() && std::holds_alternative<bool>(runIt->second))
            runOnActivation = std::get<bool>(runIt->second);
        if (ImGui::Checkbox("Run On Activation", &runOnActivation))
        {
            node.properties["runOnActivation"] = runOnActivation;
            notifyChanged();
        }

        ImGui::Separator();

        // Service type
        std::string typeStr = "EQSRefresh";
        auto typeIt = node.properties.find("serviceType");
        if (typeIt != node.properties.end() && std::holds_alternative<std::string>(typeIt->second))
            typeStr = std::get<std::string>(typeIt->second);

        static const std::array<const char*, 3> serviceTypes = {"EQSRefresh", "LineOfSightRefresh", "FocusUpdate"};
        int currentType = 0;
        for (int i = 0; i < static_cast<int>(serviceTypes.size()); ++i)
        {
            if (typeStr == serviceTypes[i]) { currentType = i; break; }
        }
        if (ImGui::Combo("Service Type", &currentType, serviceTypes.data(), static_cast<int>(serviceTypes.size())))
        {
            node.properties["serviceType"] = std::string(serviceTypes[currentType]);
            notifyChanged();
        }

        const auto isVec3 = [](BlackboardValueType t) { return t == BlackboardValueType::Vec3; };
        const auto isBool = [](BlackboardValueType t) { return t == BlackboardValueType::Bool; };
        const auto isEntityOrVec3 = [](BlackboardValueType t)
        { return t == BlackboardValueType::Entity || t == BlackboardValueType::Vec3; };

        if (typeStr == "EQSRefresh")
        {
            std::string queryName;
            auto qIt = node.properties.find("queryName");
            if (qIt != node.properties.end() && std::holds_alternative<std::string>(qIt->second))
                queryName = std::get<std::string>(qIt->second);
            char queryBuf[128];
            strncpy(queryBuf, queryName.c_str(), sizeof(queryBuf) - 1);
            queryBuf[sizeof(queryBuf) - 1] = '\0';
            if (ImGui::InputText("Query Name", queryBuf, sizeof(queryBuf)))
            {
                node.properties["queryName"] = std::string(queryBuf);
                notifyChanged();
            }
            if (bt::drawKeyDropdown(node, graph, "Result Key", "resultKey", "eqsResult", isVec3))
                notifyChanged();
            ImGui::TextDisabled("Refreshes an EQS query on interval; best position -> result key");
        }
        else if (typeStr == "LineOfSightRefresh")
        {
            if (bt::drawKeyDropdown(node, graph, "Target Key", "targetKey", "target", isEntityOrVec3))
                notifyChanged();
            if (bt::drawKeyDropdown(node, graph, "Visibility Key", "visibilityKey", "targetVisible", isBool))
                notifyChanged();

            float maxDist = 50.0f;
            auto distIt = node.properties.find("maxDistance");
            if (distIt != node.properties.end() && std::holds_alternative<float>(distIt->second))
                maxDist = std::get<float>(distIt->second);
            if (ImGui::DragFloat("Max Distance", &maxDist, 1.0f, 0.0f, 500.0f))
            {
                node.properties["maxDistance"] = maxDist;
                notifyChanged();
            }

            float eyeOffset = 1.6f;
            auto eyeIt = node.properties.find("eyeOffset");
            if (eyeIt != node.properties.end() && std::holds_alternative<float>(eyeIt->second))
                eyeOffset = std::get<float>(eyeIt->second);
            if (ImGui::DragFloat("Eye Offset", &eyeOffset, 0.1f, 0.0f, 10.0f))
            {
                node.properties["eyeOffset"] = eyeOffset;
                notifyChanged();
            }
            ImGui::TextDisabled("Refreshes line-of-sight on interval; bool -> visibility key");
        }
        else if (typeStr == "FocusUpdate")
        {
            if (bt::drawKeyDropdown(node, graph, "Target Key", "targetKey", "target", isEntityOrVec3))
                notifyChanged();
            if (bt::drawKeyDropdown(node, graph, "Focus Key", "focusKey", "focusPoint", isVec3))
                notifyChanged();
            ImGui::TextDisabled("Writes the target's world position to the focus key on interval");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Runs while its branch is active; passes through to its single child");
    }
}
