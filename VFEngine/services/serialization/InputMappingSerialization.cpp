#include "InputMappingSerialization.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include "print/Log.hpp"

namespace serialization
{
    using json = nlohmann::json;

    bool InputMappingSerialization::save(const InputMappingData& data, const std::string& filePath)
    {
        try {
            json root;
            root["schemaVersion"] = "2.0";

            // Contexts
            json contextsJson = json::object();
            for (const auto& [name, def] : data.contexts) {
                json c;
                c["blocking"] = def.blocking;
                contextsJson[name] = c;
            }
            root["contexts"] = contextsJson;

            // Actions
            json actionsJson = json::object();
            for (const auto& [name, action] : data.actions) {
                json actionObj;
                actionObj["context"] = action.context;

                json arr = json::array();
                for (const auto& binding : action.bindings) {
                    json b;
                    b["type"] = binding.type == services::BindingType::Key ? "key" : "mouseButton";
                    b["code"] = binding.code;
                    if (binding.requireShift) b["shift"] = true;
                    if (binding.requireCtrl) b["ctrl"] = true;
                    if (binding.requireAlt) b["alt"] = true;
                    arr.push_back(b);
                }
                actionObj["bindings"] = arr;
                actionsJson[name] = actionObj;
            }
            root["actionBindings"] = actionsJson;

            // 1D Axes
            json axes1DJson = json::object();
            for (const auto& [name, def] : data.axes1D) {
                json a;
                a["positiveAction"] = def.positiveAction;
                a["negativeAction"] = def.negativeAction;
                axes1DJson[name] = a;
            }
            root["axes1D"] = axes1DJson;

            // 2D Axes
            json axes2DJson = json::object();
            for (const auto& [name, def] : data.axes2D) {
                json a;
                a["upAction"] = def.upAction;
                a["downAction"] = def.downAction;
                a["leftAction"] = def.leftAction;
                a["rightAction"] = def.rightAction;
                if (!def.normalize) a["normalize"] = false;
                axes2DJson[name] = a;
            }
            root["axes2D"] = axes2DJson;

            std::ofstream file(filePath);
            if (!file.is_open()) {
                vfLogError("[InputMappingSerialization] Failed to open file for writing: {}", filePath);
                return false;
            }
            file << root.dump(2);
            file.close();

            vfLogInfo("[InputMappingSerialization] Saved to: {}", filePath);
            return true;
        } catch (const std::exception& e) {
            vfLogError("[InputMappingSerialization] Failed to save: {}", e.what());
            return false;
        }
    }

    namespace {
        void parseBindings(const json& bindingsArr, std::vector<services::InputBinding>& outBindings)
        {
            for (const auto& b : bindingsArr) {
                if (!b.contains("type") || !b.contains("code")) continue;

                services::InputBinding binding;
                std::string typeStr = b["type"].get<std::string>();
                binding.type = (typeStr == "key") ? services::BindingType::Key : services::BindingType::MouseButton;
                binding.code = b["code"].get<int>();
                binding.requireShift = b.value("shift", false);
                binding.requireCtrl = b.value("ctrl", false);
                binding.requireAlt = b.value("alt", false);
                outBindings.push_back(binding);
            }
        }
    }

    bool InputMappingSerialization::load(const std::string& filePath, InputMappingData& outData)
    {
        try {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                vfLogWarning("[InputMappingSerialization] File not found: {}", filePath);
                return false;
            }

            json root = json::parse(file);

            if (!root.contains("actionBindings") || !root["actionBindings"].is_object()) {
                vfLogError("[InputMappingSerialization] Invalid file format");
                return false;
            }

            std::string version = root.value("schemaVersion", "1.0");

            // Contexts (v2.0+)
            if (root.contains("contexts") && root["contexts"].is_object()) {
                for (auto& [ctxName, ctxObj] : root["contexts"].items()) {
                    if (!ctxObj.is_object()) continue;
                    services::InputContextDefinition def;
                    def.name = ctxName;
                    def.blocking = ctxObj.value("blocking", true);
                    outData.contexts[ctxName] = def;
                }
            }

            // Actions
            for (auto& [actionName, actionVal] : root["actionBindings"].items()) {
                InputMappingData::ActionData action;

                if (actionVal.is_object()) {
                    // v2.0 format: { "context": "...", "bindings": [...] }
                    action.context = actionVal.value("context", "Default");
                    if (actionVal.contains("bindings") && actionVal["bindings"].is_array()) {
                        parseBindings(actionVal["bindings"], action.bindings);
                    }
                } else if (actionVal.is_array()) {
                    // v1.0 format: [...]
                    parseBindings(actionVal, action.bindings);
                } else {
                    continue;
                }

                outData.actions[actionName] = std::move(action);
            }

            // 1D Axes
            if (root.contains("axes1D") && root["axes1D"].is_object()) {
                for (auto& [axisName, axisObj] : root["axes1D"].items()) {
                    if (!axisObj.is_object()) continue;
                    services::Axis1DDefinition def;
                    def.name = axisName;
                    def.positiveAction = axisObj.value("positiveAction", "");
                    def.negativeAction = axisObj.value("negativeAction", "");
                    outData.axes1D[axisName] = def;
                }
            }

            // 2D Axes
            if (root.contains("axes2D") && root["axes2D"].is_object()) {
                for (auto& [axisName, axisObj] : root["axes2D"].items()) {
                    if (!axisObj.is_object()) continue;
                    services::Axis2DDefinition def;
                    def.name = axisName;
                    def.upAction = axisObj.value("upAction", "");
                    def.downAction = axisObj.value("downAction", "");
                    def.leftAction = axisObj.value("leftAction", "");
                    def.rightAction = axisObj.value("rightAction", "");
                    def.normalize = axisObj.value("normalize", true);
                    outData.axes2D[axisName] = def;
                }
            }

            vfLogInfo("[InputMappingSerialization] Loaded from: {}", filePath);
            return true;
        } catch (const std::exception& e) {
            vfLogError("[InputMappingSerialization] Failed to load: {}", e.what());
            return false;
        }
    }
}
