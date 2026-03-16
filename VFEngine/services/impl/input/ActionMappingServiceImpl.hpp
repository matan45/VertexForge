#pragma once
#include "../../interfaces/input/IActionMappingService.hpp"
#include <unordered_map>
#include <string>
#include <vector>

namespace services {

    class ActionMappingServiceImpl : public IActionMappingService {
    public:
        ActionMappingServiceImpl() = default;
        ~ActionMappingServiceImpl() override = default;

        void registerEventHandlers() override;

        bool isActionDown(const std::string& actionName) const override;
        bool isActionPressed(const std::string& actionName) const override;
        bool isActionReleased(const std::string& actionName) const override;

        std::vector<InputBinding> getActionBindings(const std::string& actionName) const override;
        std::vector<std::string> getAllActionNames() const override;

        void registerAction(const std::string& actionName,
                             const std::vector<InputBinding>& defaultBindings) override;

        void addBinding(const std::string& actionName, const InputBinding& binding) override;
        void removeBinding(const std::string& actionName, const InputBinding& binding) override;
        void setBindings(const std::string& actionName,
                          const std::vector<InputBinding>& bindings) override;
        void resetBindings(const std::string& actionName) override;
        void resetAllBindings() override;

        bool saveBindings(const std::string& filePath) override;
        bool loadBindings(const std::string& filePath) override;

    private:
        struct ActionEntry {
            std::vector<InputBinding> currentBindings;
            std::vector<InputBinding> defaultBindings;
        };

        bool checkBinding(const InputBinding& binding, bool (*queryKey)(int), bool (*queryMouse)(int)) const;

        std::unordered_map<std::string, ActionEntry> actions;
        std::unordered_map<std::string, std::vector<InputBinding>> pendingOverrides;
    };

}
