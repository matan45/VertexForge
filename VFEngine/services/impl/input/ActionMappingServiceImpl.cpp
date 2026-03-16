#include "ActionMappingServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/input/ActionMappingEvents.hpp"
#include "../../events/input/InputContextEvents.hpp"
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

    ActionMappingServiceImpl::ActionMappingServiceImpl() {
        contexts["Default"] = {{"Default", false}, true};
        contextStack.push_back("Default");
    }

    // ============================================
    // Context filtering
    // ============================================

    bool ActionMappingServiceImpl::isActionContextActive(const std::string& contextName) const {
        for (auto it = contextStack.rbegin(); it != contextStack.rend(); ++it) {
            auto ctxIt = contexts.find(*it);
            if (ctxIt == contexts.end() || !ctxIt->second.active) continue;
            if (*it == contextName) return true;
            if (ctxIt->second.definition.blocking) return false;
        }
        return false;
    }

    // ============================================
    // Action state queries
    // ============================================

    bool ActionMappingServiceImpl::isActionDown(const std::string& actionName) const {
        auto it = actions.find(actionName);
        if (it == actions.end()) return false;
        if (!isActionContextActive(it->second.context)) return false;

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
        if (!isActionContextActive(it->second.context)) return false;

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
        if (!isActionContextActive(it->second.context)) return false;

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
                                                    const std::vector<InputBinding>& defaultBindings,
                                                    const std::string& context) {
        if (actions.find(actionName) != actions.end()) return;

        ActionEntry entry;
        entry.defaultBindings = defaultBindings;
        entry.currentBindings = defaultBindings;
        entry.context = context;

        // Apply pending overrides from a previously loaded bindings file
        auto pendingIt = pendingOverrides.find(actionName);
        if (pendingIt != pendingOverrides.end()) {
            entry.currentBindings = pendingIt->second;
            pendingOverrides.erase(pendingIt);
        }

        actions[actionName] = std::move(entry);
    }

    void ActionMappingServiceImpl::setActionContext(const std::string& actionName, const std::string& context) {
        auto it = actions.find(actionName);
        if (it == actions.end()) return;
        it->second.context = context;
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
            data.actions[name] = {entry.currentBindings, entry.context};
        }
        data.axes1D = axes1D;
        data.axes2D = axes2D;
        for (const auto& [name, state] : contexts) {
            if (name == "Default") continue;
            data.contexts[name] = state.definition;
        }

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

        // Load contexts
        for (auto& [name, def] : data.contexts) {
            if (contexts.find(name) == contexts.end()) {
                contexts[name] = {def, false};
            }
        }

        for (auto& [actionName, actionData] : data.actions) {
            auto it = actions.find(actionName);
            if (it != actions.end()) {
                it->second.currentBindings = actionData.bindings;
                it->second.context = actionData.context;
            } else {
                ActionEntry entry;
                entry.currentBindings = actionData.bindings;
                entry.defaultBindings = actionData.bindings;
                entry.context = actionData.context;
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
    // Context management
    // ============================================

    void ActionMappingServiceImpl::createContext(const std::string& name, bool blocking) {
        if (contexts.find(name) != contexts.end()) {
            vfLogWarning("[ActionMapping] Context '{}' already exists", name);
            return;
        }
        contexts[name] = {InputContextDefinition{name, blocking}, false};
    }

    void ActionMappingServiceImpl::removeContext(const std::string& name) {
        if (name == "Default") {
            vfLogWarning("[ActionMapping] Cannot remove Default context");
            return;
        }
        contexts.erase(name);
        contextStack.erase(std::remove(contextStack.begin(), contextStack.end(), name), contextStack.end());
    }

    void ActionMappingServiceImpl::pushContext(const std::string& name) {
        auto it = contexts.find(name);
        if (it == contexts.end()) {
            vfLogWarning("[ActionMapping] Cannot push unknown context '{}'", name);
            return;
        }
        // Remove if already on stack, then push to top
        contextStack.erase(std::remove(contextStack.begin(), contextStack.end(), name), contextStack.end());
        contextStack.push_back(name);
        it->second.active = true;
    }

    void ActionMappingServiceImpl::popContext(const std::string& name) {
        if (name.empty()) {
            // Pop topmost non-Default context
            for (auto it = contextStack.rbegin(); it != contextStack.rend(); ++it) {
                if (*it != "Default") {
                    auto ctxIt = contexts.find(*it);
                    if (ctxIt != contexts.end()) ctxIt->second.active = false;
                    contextStack.erase(std::next(it).base());
                    return;
                }
            }
            return;
        }
        if (name == "Default") return;
        auto ctxIt = contexts.find(name);
        if (ctxIt != contexts.end()) ctxIt->second.active = false;
        contextStack.erase(std::remove(contextStack.begin(), contextStack.end(), name), contextStack.end());
    }

    void ActionMappingServiceImpl::setContextBlocking(const std::string& name, bool blocking) {
        auto it = contexts.find(name);
        if (it == contexts.end()) return;
        it->second.definition.blocking = blocking;
    }

    std::vector<std::string> ActionMappingServiceImpl::getActiveContexts() const {
        std::vector<std::string> result;
        for (const auto& name : contextStack) {
            auto it = contexts.find(name);
            if (it != contexts.end() && it->second.active) {
                result.push_back(name);
            }
        }
        return result;
    }

    std::vector<std::string> ActionMappingServiceImpl::getAllContextNames() const {
        std::vector<std::string> names;
        names.reserve(contexts.size());
        for (const auto& [name, _] : contexts) {
            names.push_back(name);
        }
        return names;
    }

    bool ActionMappingServiceImpl::isContextActive(const std::string& name) const {
        auto it = contexts.find(name);
        if (it == contexts.end()) return false;
        return it->second.active;
    }

    std::vector<std::string> ActionMappingServiceImpl::getContextActions(const std::string& name) const {
        std::vector<std::string> result;
        for (const auto& [actionName, entry] : actions) {
            if (entry.context == name) {
                result.push_back(actionName);
            }
        }
        return result;
    }

    std::string ActionMappingServiceImpl::getActionContext(const std::string& actionName) const {
        auto it = actions.find(actionName);
        if (it == actions.end()) return "";
        return it->second.context;
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
                registerAction(cmd.actionName, cmd.defaultBindings, cmd.context);
            });

        dispatcher.registerCommandHandler<events::input::SetActionContextCommand>(
            [this](const events::input::SetActionContextCommand& cmd) {
                setActionContext(cmd.actionName, cmd.context);
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

        // Context commands
        dispatcher.registerCommandHandler<events::input::CreateContextCommand>(
            [this](const events::input::CreateContextCommand& cmd) {
                createContext(cmd.contextName, cmd.blocking);
            });

        dispatcher.registerCommandHandler<events::input::RemoveContextCommand>(
            [this](const events::input::RemoveContextCommand& cmd) {
                removeContext(cmd.contextName);
            });

        dispatcher.registerCommandHandler<events::input::PushContextCommand>(
            [this](const events::input::PushContextCommand& cmd) {
                pushContext(cmd.contextName);
            });

        dispatcher.registerCommandHandler<events::input::PopContextCommand>(
            [this](const events::input::PopContextCommand& cmd) {
                popContext(cmd.contextName);
            });

        dispatcher.registerCommandHandler<events::input::SetContextBlockingCommand>(
            [this](const events::input::SetContextBlockingCommand& cmd) {
                setContextBlocking(cmd.contextName, cmd.blocking);
            });

        // Context queries
        dispatcher.registerQueryHandler<events::input::GetActiveContextsQuery>(
            [this](const events::input::GetActiveContextsQuery&) {
                return getActiveContexts();
            });

        dispatcher.registerQueryHandler<events::input::GetAllContextNamesQuery>(
            [this](const events::input::GetAllContextNamesQuery&) {
                return getAllContextNames();
            });

        dispatcher.registerQueryHandler<events::input::IsContextActiveQuery>(
            [this](const events::input::IsContextActiveQuery& query) {
                return isContextActive(query.contextName);
            });

        dispatcher.registerQueryHandler<events::input::GetContextActionsQuery>(
            [this](const events::input::GetContextActionsQuery& query) {
                return getContextActions(query.contextName);
            });

        dispatcher.registerQueryHandler<events::input::GetActionContextQuery>(
            [this](const events::input::GetActionContextQuery& query) {
                return getActionContext(query.actionName);
            });
    }

}
