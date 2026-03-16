#pragma once
#include "../../data/ActionMappingTypes.hpp"
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
                                     const std::vector<InputBinding>& defaultBindings) = 0;

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
    };

}
