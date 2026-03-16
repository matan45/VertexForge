#pragma once
#include "../../interfaces/input/IActionMappingService.hpp"
#include <glm/vec2.hpp>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace services {

    class ActionMappingServiceImpl : public IActionMappingService {
    public:
        ActionMappingServiceImpl();

        ~ActionMappingServiceImpl() override = default;

        void registerEventHandlers() override;

        bool isActionDown(const std::string& actionName) const override;
        bool isActionPressed(const std::string& actionName) const override;
        bool isActionReleased(const std::string& actionName) const override;

        std::vector<InputBinding> getActionBindings(const std::string& actionName) const override;
        std::vector<std::string> getAllActionNames() const override;

        void registerAction(const std::string& actionName,
                             const std::vector<InputBinding>& defaultBindings,
                             const std::string& context = "Default") override;

        void setActionContext(const std::string& actionName, const std::string& context) override;
        void unregisterAction(const std::string& actionName) override;

        void addBinding(const std::string& actionName, const InputBinding& binding) override;
        void removeBinding(const std::string& actionName, const InputBinding& binding) override;
        void setBindings(const std::string& actionName,
                          const std::vector<InputBinding>& bindings) override;
        void resetBindings(const std::string& actionName) override;
        void resetAllBindings() override;

        bool saveBindings(const std::string& filePath) override;
        bool loadBindings(const std::string& filePath) override;

        void registerAxis1D(const std::string& name,
                             const std::string& positiveAction,
                             const std::string& negativeAction) override;
        void unregisterAxis1D(const std::string& name) override;
        float getAxis1DValue(const std::string& name) const override;
        std::vector<std::string> getAllAxis1DNames() const override;
        std::optional<Axis1DDefinition> getAxis1DDefinition(const std::string& name) const override;

        void registerAxis2D(const std::string& name,
                             const std::string& upAction,
                             const std::string& downAction,
                             const std::string& leftAction,
                             const std::string& rightAction,
                             bool normalize = true) override;
        void unregisterAxis2D(const std::string& name) override;
        glm::vec2 getAxis2DValue(const std::string& name) const override;
        std::vector<std::string> getAllAxis2DNames() const override;
        std::optional<Axis2DDefinition> getAxis2DDefinition(const std::string& name) const override;

        void createContext(const std::string& name, bool blocking = true) override;
        void removeContext(const std::string& name) override;
        void pushContext(const std::string& name) override;
        void popContext(const std::string& name = "") override;
        void setContextBlocking(const std::string& name, bool blocking) override;

        std::vector<std::string> getActiveContexts() const override;
        std::vector<std::string> getAllContextNames() const override;
        bool isContextActive(const std::string& name) const override;
        std::vector<std::string> getContextActions(const std::string& name) const override;
        std::string getActionContext(const std::string& actionName) const override;

        void consumeAction(const std::string& actionName) override;
        bool isActionConsumed(const std::string& actionName) const override;
        void clearConsumedActions() override;

    private:
        struct ActionEntry {
            std::vector<InputBinding> currentBindings;
            std::vector<InputBinding> defaultBindings;
            std::string context = "Default";
        };

        struct ContextState {
            InputContextDefinition definition;
            bool active = false;
        };

        bool isActionContextActive(const std::string& contextName) const;

        std::unordered_map<std::string, ActionEntry> actions;
        std::unordered_map<std::string, Axis1DDefinition> axes1D;
        std::unordered_map<std::string, Axis2DDefinition> axes2D;
        std::vector<std::string> contextStack;
        std::unordered_map<std::string, ContextState> contexts;
        std::unordered_set<std::string> consumedActions;
    };

}
