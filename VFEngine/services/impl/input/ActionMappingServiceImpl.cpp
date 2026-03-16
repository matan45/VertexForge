#include "ActionMappingServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/input/ActionMappingEvents.hpp"
#include "../../events/input/InputEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

#include "print/Log.hpp"
namespace services {

    using json = nlohmann::json;

    namespace {
        // Check if required modifier keys are currently held
        bool areModifiersHeld(const services::InputBinding& binding,
                              events::EventDispatcher& dispatcher) {
            if (binding.requireShift) {
                events::input::IsKeyDownQuery q1; q1.keyCode = 340; // Left Shift
                events::input::IsKeyDownQuery q2; q2.keyCode = 344; // Right Shift
                if (!dispatcher.query(q1) && !dispatcher.query(q2)) return false;
            }
            if (binding.requireCtrl) {
                events::input::IsKeyDownQuery q1; q1.keyCode = 341; // Left Ctrl
                events::input::IsKeyDownQuery q2; q2.keyCode = 345; // Right Ctrl
                if (!dispatcher.query(q1) && !dispatcher.query(q2)) return false;
            }
            if (binding.requireAlt) {
                events::input::IsKeyDownQuery q1; q1.keyCode = 342; // Left Alt
                events::input::IsKeyDownQuery q2; q2.keyCode = 346; // Right Alt
                if (!dispatcher.query(q1) && !dispatcher.query(q2)) return false;
            }
            return true;
        }
    }

    // ============================================
    // Action state queries
    // ============================================

    bool ActionMappingServiceImpl::isActionDown(const std::string& actionName) const {
        auto it = actions.find(actionName);
        if (it == actions.end()) return false;

        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& binding : it->second.currentBindings) {
            if (!areModifiersHeld(binding, dispatcher)) continue;

            if (binding.type == BindingType::Key) {
                events::input::IsKeyDownQuery query;
                query.keyCode = binding.code;
                if (dispatcher.query(query)) return true;
            } else {
                events::input::IsMouseButtonDownQuery query;
                query.button = binding.code;
                if (dispatcher.query(query)) return true;
            }
        }
        return false;
    }

    bool ActionMappingServiceImpl::isActionPressed(const std::string& actionName) const {
        auto it = actions.find(actionName);
        if (it == actions.end()) return false;

        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& binding : it->second.currentBindings) {
            if (!areModifiersHeld(binding, dispatcher)) continue;

            if (binding.type == BindingType::Key) {
                events::input::IsKeyPressedQuery query;
                query.keyCode = binding.code;
                if (dispatcher.query(query)) return true;
            } else {
                events::input::IsMouseButtonPressedQuery query;
                query.button = binding.code;
                if (dispatcher.query(query)) return true;
            }
        }
        return false;
    }

    bool ActionMappingServiceImpl::isActionReleased(const std::string& actionName) const {
        auto it = actions.find(actionName);
        if (it == actions.end()) return false;

        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& binding : it->second.currentBindings) {
            // For release, we don't check modifiers - they may already be released
            if (binding.type == BindingType::Key) {
                events::input::IsKeyReleasedQuery query;
                query.keyCode = binding.code;
                if (dispatcher.query(query)) return true;
            } else {
                events::input::IsMouseButtonReleasedQuery query;
                query.button = binding.code;
                if (dispatcher.query(query)) return true;
            }
        }
        return false;
    }

    // ============================================
    // Binding queries
    // ============================================

    std::vector<InputBinding> ActionMappingServiceImpl::getActionBindings(const std::string& actionName) const {
        auto it = actions.find(actionName);
        if (it == actions.end()) return {};
        return it->second.currentBindings;
    }

    std::vector<std::string> ActionMappingServiceImpl::getAllActionNames() const {
        std::vector<std::string> names;
        names.reserve(actions.size());
        for (const auto& [name, _] : actions) {
            names.push_back(name);
        }
        return names;
    }

    // ============================================
    // Action registration
    // ============================================

    void ActionMappingServiceImpl::registerAction(const std::string& actionName,
                                                    const std::vector<InputBinding>& defaultBindings) {
        if (actions.find(actionName) != actions.end()) return;

        ActionEntry entry;
        entry.defaultBindings = defaultBindings;
        entry.currentBindings = defaultBindings;

        // Apply pending overrides from a previously loaded bindings file
        auto pendingIt = pendingOverrides.find(actionName);
        if (pendingIt != pendingOverrides.end()) {
            entry.currentBindings = pendingIt->second;
            pendingOverrides.erase(pendingIt);
        }

        actions[actionName] = std::move(entry);
    }

    // ============================================
    // Action removal
    // ============================================

    void ActionMappingServiceImpl::unregisterAction(const std::string& actionName) {
        actions.erase(actionName);
    }

    // ============================================
    // Binding mutations
    // ============================================

    void ActionMappingServiceImpl::addBinding(const std::string& actionName, const InputBinding& binding) {
        auto it = actions.find(actionName);
        if (it == actions.end()) {
            vfLogWarning("[ActionMapping] Cannot add binding: action '{}' not registered", actionName);
            return;
        }

        auto& bindings = it->second.currentBindings;
        if (std::find(bindings.begin(), bindings.end(), binding) == bindings.end()) {
            bindings.push_back(binding);
        }
    }

    void ActionMappingServiceImpl::removeBinding(const std::string& actionName, const InputBinding& binding) {
        auto it = actions.find(actionName);
        if (it == actions.end()) return;

        auto& bindings = it->second.currentBindings;
        bindings.erase(std::remove(bindings.begin(), bindings.end(), binding), bindings.end());
    }

    void ActionMappingServiceImpl::setBindings(const std::string& actionName,
                                                const std::vector<InputBinding>& bindings) {
        auto it = actions.find(actionName);
        if (it == actions.end()) {
            vfLogWarning("[ActionMapping] Cannot set bindings: action '{}' not registered", actionName);
            return;
        }
        it->second.currentBindings = bindings;
    }

    void ActionMappingServiceImpl::resetBindings(const std::string& actionName) {
        auto it = actions.find(actionName);
        if (it == actions.end()) return;
        it->second.currentBindings = it->second.defaultBindings;
    }

    void ActionMappingServiceImpl::resetAllBindings() {
        for (auto& [name, entry] : actions) {
            entry.currentBindings = entry.defaultBindings;
        }
    }

    // ============================================
    // Persistence
    // ============================================

    bool ActionMappingServiceImpl::saveBindings(const std::string& filePath) {
        try {
            json root;
            root["schemaVersion"] = "1.0";

            json bindingsJson = json::object();
            for (const auto& [name, entry] : actions) {
                // Only save actions whose bindings differ from defaults
                if (entry.currentBindings != entry.defaultBindings) {
                    json arr = json::array();
                    for (const auto& binding : entry.currentBindings) {
                        json b;
                        b["type"] = binding.type == BindingType::Key ? "key" : "mouseButton";
                        b["code"] = binding.code;
                        if (binding.requireShift) b["shift"] = true;
                        if (binding.requireCtrl) b["ctrl"] = true;
                        if (binding.requireAlt) b["alt"] = true;
                        arr.push_back(b);
                    }
                    bindingsJson[name] = arr;
                }
            }
            root["actionBindings"] = bindingsJson;

            std::ofstream file(filePath);
            if (!file.is_open()) {
                vfLogError("[ActionMapping] Failed to open file for writing: {}", filePath);
                return false;
            }
            file << root.dump(2);
            file.close();

            // Notify asset database so it generates a .vfmeta sidecar
            events::resource::AssetSavedNotification notif;
            notif.filePath = filePath;
            events::EventDispatcher::instance().publish(notif);

            vfLogInfo("[ActionMapping] Saved bindings to: {}", filePath);
            return true;
        } catch (const std::exception& e) {
            vfLogError("[ActionMapping] Failed to save bindings: {}", e.what());
            return false;
        }
    }

    bool ActionMappingServiceImpl::loadBindings(const std::string& filePath) {
        try {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                vfLogWarning("[ActionMapping] Bindings file not found: {}", filePath);
                return false;
            }

            json root = json::parse(file);

            if (!root.contains("actionBindings") || !root["actionBindings"].is_object()) {
                vfLogError("[ActionMapping] Invalid bindings file format");
                return false;
            }

            for (auto& [actionName, bindingsArr] : root["actionBindings"].items()) {
                if (!bindingsArr.is_array()) continue;

                std::vector<InputBinding> bindings;
                for (const auto& b : bindingsArr) {
                    if (!b.contains("type") || !b.contains("code")) continue;

                    InputBinding binding;
                    std::string typeStr = b["type"].get<std::string>();
                    binding.type = (typeStr == "key") ? BindingType::Key : BindingType::MouseButton;
                    binding.code = b["code"].get<int>();
                    binding.requireShift = b.value("shift", false);
                    binding.requireCtrl = b.value("ctrl", false);
                    binding.requireAlt = b.value("alt", false);
                    bindings.push_back(binding);
                }

                auto it = actions.find(actionName);
                if (it != actions.end()) {
                    it->second.currentBindings = bindings;
                } else {
                    // Cache for later when the action gets registered
                    pendingOverrides[actionName] = bindings;
                }
            }

            vfLogInfo("[ActionMapping] Loaded bindings from: {}", filePath);
            return true;
        } catch (const std::exception& e) {
            vfLogError("[ActionMapping] Failed to load bindings: {}", e.what());
            return false;
        }
    }

    // ============================================
    // Event handler registration
    // ============================================

    void ActionMappingServiceImpl::registerEventHandlers() {
        auto& dispatcher = events::EventDispatcher::instance();

        // Queries
        dispatcher.registerQueryHandler<events::input::IsActionDownQuery>(
            [this](const events::input::IsActionDownQuery& query) {
                return isActionDown(query.actionName);
            });

        dispatcher.registerQueryHandler<events::input::IsActionPressedQuery>(
            [this](const events::input::IsActionPressedQuery& query) {
                return isActionPressed(query.actionName);
            });

        dispatcher.registerQueryHandler<events::input::IsActionReleasedQuery>(
            [this](const events::input::IsActionReleasedQuery& query) {
                return isActionReleased(query.actionName);
            });

        dispatcher.registerQueryHandler<events::input::GetActionBindingsQuery>(
            [this](const events::input::GetActionBindingsQuery& query) {
                return getActionBindings(query.actionName);
            });

        dispatcher.registerQueryHandler<events::input::GetAllActionNamesQuery>(
            [this](const events::input::GetAllActionNamesQuery&) {
                return getAllActionNames();
            });

        // Commands
        dispatcher.registerCommandHandler<events::input::RegisterActionCommand>(
            [this](const events::input::RegisterActionCommand& cmd) {
                registerAction(cmd.actionName, cmd.defaultBindings);
            });

        dispatcher.registerCommandHandler<events::input::UnregisterActionCommand>(
            [this](const events::input::UnregisterActionCommand& cmd) {
                unregisterAction(cmd.actionName);
            });

        dispatcher.registerCommandHandler<events::input::AddActionBindingCommand>(
            [this](const events::input::AddActionBindingCommand& cmd) {
                addBinding(cmd.actionName, cmd.binding);
            });

        dispatcher.registerCommandHandler<events::input::RemoveActionBindingCommand>(
            [this](const events::input::RemoveActionBindingCommand& cmd) {
                removeBinding(cmd.actionName, cmd.binding);
            });

        dispatcher.registerCommandHandler<events::input::SetActionBindingsCommand>(
            [this](const events::input::SetActionBindingsCommand& cmd) {
                setBindings(cmd.actionName, cmd.bindings);
            });

        dispatcher.registerCommandHandler<events::input::ResetActionBindingsCommand>(
            [this](const events::input::ResetActionBindingsCommand& cmd) {
                resetBindings(cmd.actionName);
            });

        dispatcher.registerCommandHandler<events::input::ResetAllActionBindingsCommand>(
            [this](const events::input::ResetAllActionBindingsCommand&) {
                resetAllBindings();
            });

        dispatcher.registerCommandHandler<events::input::SaveActionBindingsCommand>(
            [this](const events::input::SaveActionBindingsCommand& cmd) {
                return saveBindings(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::input::LoadActionBindingsCommand>(
            [this](const events::input::LoadActionBindingsCommand& cmd) {
                return loadBindings(cmd.filePath);
            });
    }

}
