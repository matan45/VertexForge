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
            root["schemaVersion"] = "1.0";

            // Actions
            json actionsJson = json::object();
            for (const auto& [name, action] : data.actions) {
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
                actionsJson[name] = arr;
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

            // Actions
            for (auto& [actionName, bindingsArr] : root["actionBindings"].items()) {
                if (!bindingsArr.is_array()) continue;

                InputMappingData::ActionData action;
                for (const auto& b : bindingsArr) {
                    if (!b.contains("type") || !b.contains("code")) continue;

                    services::InputBinding binding;
                    std::string typeStr = b["type"].get<std::string>();
                    binding.type = (typeStr == "key") ? services::BindingType::Key : services::BindingType::MouseButton;
                    binding.code = b["code"].get<int>();
                    binding.requireShift = b.value("shift", false);
                    binding.requireCtrl = b.value("ctrl", false);
                    binding.requireAlt = b.value("alt", false);
                    action.bindings.push_back(binding);
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
