#pragma once
#include "../../data/ActionMappingTypes.hpp"
#include <glm/vec2.hpp>
#include <optional>
#include <string>
#include <vector>

namespace services {

    class IActionMappingService {
    public:
        virtual ~IActionMappingService() = default;

        virtual void registerEventHandlers() = 0;

        // Action state queries
        virtual bool isActionDown(const std::string& actionName) const = 0;
        virtual bool isActionPressed(const std::string& actionName) const = 0;
        virtual bool isActionReleased(const std::string& actionName) const = 0;

        // Binding queries
        virtual std::vector<InputBinding> getActionBindings(const std::string& actionName) const = 0;
        virtual std::vector<std::string> getAllActionNames() const = 0;

        // Action registration
        virtual void registerAction(const std::string& actionName,
                                     const std::vector<InputBinding>& defaultBindings,
                                     const std::string& context = "Default") = 0;

        // Action context
        virtual void setActionContext(const std::string& actionName, const std::string& context) = 0;

        // Action removal
        virtual void unregisterAction(const std::string& actionName) = 0;

        // Binding mutations
        virtual void addBinding(const std::string& actionName, const InputBinding& binding) = 0;
        virtual void removeBinding(const std::string& actionName, const InputBinding& binding) = 0;
        virtual void setBindings(const std::string& actionName,
                                  const std::vector<InputBinding>& bindings) = 0;
        virtual void resetBindings(const std::string& actionName) = 0;
        virtual void resetAllBindings() = 0;

        // Persistence
        virtual bool saveBindings(const std::string& filePath) = 0;
        virtual bool loadBindings(const std::string& filePath) = 0;

        // 1D Axis
        virtual void registerAxis1D(const std::string& name,
                                     const std::string& positiveAction,
                                     const std::string& negativeAction) = 0;
        virtual void unregisterAxis1D(const std::string& name) = 0;
        virtual float getAxis1DValue(const std::string& name) const = 0;
        virtual std::vector<std::string> getAllAxis1DNames() const = 0;
        virtual std::optional<Axis1DDefinition> getAxis1DDefinition(const std::string& name) const = 0;

        // 2D Axis
        virtual void registerAxis2D(const std::string& name,
                                     const std::string& upAction,
                                     const std::string& downAction,
                                     const std::string& leftAction,
                                     const std::string& rightAction,
                                     bool normalize = true) = 0;
        virtual void unregisterAxis2D(const std::string& name) = 0;
        virtual glm::vec2 getAxis2DValue(const std::string& name) const = 0;
        virtual std::vector<std::string> getAllAxis2DNames() const = 0;
        virtual std::optional<Axis2DDefinition> getAxis2DDefinition(const std::string& name) const = 0;

        // Context management
        virtual void createContext(const std::string& name, bool blocking = true) = 0;
        virtual void removeContext(const std::string& name) = 0;
        virtual void pushContext(const std::string& name) = 0;
        virtual void popContext(const std::string& name = "") = 0;
        virtual void setContextBlocking(const std::string& name, bool blocking) = 0;

        // Context queries
        virtual std::vector<std::string> getActiveContexts() const = 0;
        virtual std::vector<std::string> getAllContextNames() const = 0;
        virtual bool isContextActive(const std::string& name) const = 0;
        virtual std::vector<std::string> getContextActions(const std::string& name) const = 0;
        virtual std::string getActionContext(const std::string& actionName) const = 0;

        // Action consumption
        virtual void consumeAction(const std::string& actionName) = 0;
        virtual bool isActionConsumed(const std::string& actionName) const = 0;
        virtual void clearConsumedActions() = 0;
    };

}
