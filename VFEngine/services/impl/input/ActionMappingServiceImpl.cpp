#include "ActionMappingServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/input/ActionMappingEvents.hpp"
#include "../../events/input/InputEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "../../serialization/InputMappingSerialization.hpp"
#include <glm/glm.hpp>
#include <algorithm>

#include "print/Log.hpp"
namespace services {

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
    // 1D Axis
    // ============================================

    void ActionMappingServiceImpl::registerAxis1D(const std::string& name,
                                                    const std::string& positiveAction,
                                                    const std::string& negativeAction) {
        if (axes1D.find(name) != axes1D.end()) {
            vfLogWarning("[ActionMapping] 1D axis '{}' already registered", name);
            return;
        }
        axes1D[name] = Axis1DDefinition{name, positiveAction, negativeAction};
    }

    void ActionMappingServiceImpl::unregisterAxis1D(const std::string& name) {
        axes1D.erase(name);
    }

    float ActionMappingServiceImpl::getAxis1DValue(const std::string& name) const {
        auto it = axes1D.find(name);
        if (it == axes1D.end()) return 0.0f;

        bool pos = isActionDown(it->second.positiveAction);
        bool neg = isActionDown(it->second.negativeAction);
        if (pos == neg) return 0.0f;
        return pos ? 1.0f : -1.0f;
    }

    std::vector<std::string> ActionMappingServiceImpl::getAllAxis1DNames() const {
        std::vector<std::string> names;
        names.reserve(axes1D.size());
        for (const auto& [name, _] : axes1D) {
            names.push_back(name);
        }
        return names;
    }

    std::optional<Axis1DDefinition> ActionMappingServiceImpl::getAxis1DDefinition(const std::string& name) const {
        auto it = axes1D.find(name);
        if (it == axes1D.end()) return std::nullopt;
        return it->second;
    }

    // ============================================
    // 2D Axis
    // ============================================

    void ActionMappingServiceImpl::registerAxis2D(const std::string& name,
                                                    const std::string& upAction,
                                                    const std::string& downAction,
                                                    const std::string& leftAction,
                                                    const std::string& rightAction,
                                                    bool normalize) {
        if (axes2D.find(name) != axes2D.end()) {
            vfLogWarning("[ActionMapping] 2D axis '{}' already registered", name);
            return;
        }
        axes2D[name] = Axis2DDefinition{name, upAction, downAction, leftAction, rightAction, normalize};
    }

    void ActionMappingServiceImpl::unregisterAxis2D(const std::string& name) {
        axes2D.erase(name);
    }

    glm::vec2 ActionMappingServiceImpl::getAxis2DValue(const std::string& name) const {
        auto it = axes2D.find(name);
        if (it == axes2D.end()) return glm::vec2(0.0f);

        const auto& def = it->second;
        float x = 0.0f, y = 0.0f;
        if (isActionDown(def.rightAction)) x += 1.0f;
        if (isActionDown(def.leftAction))  x -= 1.0f;
        if (isActionDown(def.upAction))    y += 1.0f;
        if (isActionDown(def.downAction))  y -= 1.0f;

        glm::vec2 result(x, y);
        if (def.normalize && glm::length(result) > 1.0f) {
            result = glm::normalize(result);
        }
        return result;
    }

    std::vector<std::string> ActionMappingServiceImpl::getAllAxis2DNames() const {
        std::vector<std::string> names;
        names.reserve(axes2D.size());
        for (const auto& [name, _] : axes2D) {
            names.push_back(name);
        }
        return names;
    }

    std::optional<Axis2DDefinition> ActionMappingServiceImpl::getAxis2DDefinition(const std::string& name) const {
        auto it = axes2D.find(name);
        if (it == axes2D.end()) return std::nullopt;
        return it->second;
    }

    // ============================================
    // Persistence
    // ============================================

    bool ActionMappingServiceImpl::saveBindings(const std::string& filePath) {
        serialization::InputMappingData data;
        for (const auto& [name, entry] : actions) {
            data.actions[name] = {entry.currentBindings};
        }
        data.axes1D = axes1D;
        data.axes2D = axes2D;

        bool ok = serialization::InputMappingSerialization::save(data, filePath);
        if (ok) {
            events::resource::AssetSavedNotification notif;
            notif.filePath = filePath;
            events::EventDispatcher::instance().publish(notif);
        }
        return ok;
    }

    bool ActionMappingServiceImpl::loadBindings(const std::string& filePath) {
        serialization::InputMappingData data;
        if (!serialization::InputMappingSerialization::load(filePath, data)) {
            return false;
        }

        for (auto& [actionName, actionData] : data.actions) {
            auto it = actions.find(actionName);
            if (it != actions.end()) {
                it->second.currentBindings = actionData.bindings;
            } else {
                ActionEntry entry;
                entry.currentBindings = actionData.bindings;
                entry.defaultBindings = actionData.bindings;
                actions[actionName] = std::move(entry);
            }
        }

        for (auto& [name, def] : data.axes1D) {
            axes1D[name] = def;
        }
        for (auto& [name, def] : data.axes2D) {
            axes2D[name] = def;
        }

        return true;
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

        // Axis queries
        dispatcher.registerQueryHandler<events::input::GetAxis1DValueQuery>(
            [this](const events::input::GetAxis1DValueQuery& query) {
                return getAxis1DValue(query.axisName);
            });

        dispatcher.registerQueryHandler<events::input::GetAxis2DValueQuery>(
            [this](const events::input::GetAxis2DValueQuery& query) {
                return getAxis2DValue(query.axisName);
            });

        dispatcher.registerQueryHandler<events::input::GetAllAxis1DNamesQuery>(
            [this](const events::input::GetAllAxis1DNamesQuery&) {
                return getAllAxis1DNames();
            });

        dispatcher.registerQueryHandler<events::input::GetAllAxis2DNamesQuery>(
            [this](const events::input::GetAllAxis2DNamesQuery&) {
                return getAllAxis2DNames();
            });

        dispatcher.registerQueryHandler<events::input::GetAxis1DDefinitionQuery>(
            [this](const events::input::GetAxis1DDefinitionQuery& query) {
                return getAxis1DDefinition(query.axisName);
            });

        dispatcher.registerQueryHandler<events::input::GetAxis2DDefinitionQuery>(
            [this](const events::input::GetAxis2DDefinitionQuery& query) {
                return getAxis2DDefinition(query.axisName);
            });

        // Axis commands
        dispatcher.registerCommandHandler<events::input::RegisterAxis1DCommand>(
            [this](const events::input::RegisterAxis1DCommand& cmd) {
                registerAxis1D(cmd.axisName, cmd.positiveAction, cmd.negativeAction);
            });

        dispatcher.registerCommandHandler<events::input::RegisterAxis2DCommand>(
            [this](const events::input::RegisterAxis2DCommand& cmd) {
                registerAxis2D(cmd.axisName, cmd.upAction, cmd.downAction,
                               cmd.leftAction, cmd.rightAction, cmd.normalize);
            });

        dispatcher.registerCommandHandler<events::input::UnregisterAxis1DCommand>(
            [this](const events::input::UnregisterAxis1DCommand& cmd) {
                unregisterAxis1D(cmd.axisName);
            });

        dispatcher.registerCommandHandler<events::input::UnregisterAxis2DCommand>(
            [this](const events::input::UnregisterAxis2DCommand& cmd) {
                unregisterAxis2D(cmd.axisName);
            });
    }

}
