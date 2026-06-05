#pragma once
#include "../../data/EditorKeybindingTypes.hpp"
#include <string>
#include <vector>

namespace services
{
    class IEditorKeybindingService
    {
    public:
        virtual ~IEditorKeybindingService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void registerAction(const std::string& name, const std::string& category,
                                    const std::string& displayName,
                                    const std::vector<InputBinding>& defaultBindings) = 0;

        virtual bool isActionPressed(const std::string& name) const = 0;

        virtual std::vector<InputBinding> getBindings(const std::string& name) const = 0;
        virtual void setBindings(const std::string& name, const std::vector<InputBinding>& bindings) = 0;
        virtual void resetBindings(const std::string& name) = 0;
        virtual void resetAll() = 0;

        virtual std::vector<EditorActionInfo> getAllActions() const = 0;
        virtual std::vector<KeybindingConflict> getConflicts(const std::string& actionName,
                                                              const InputBinding& binding) const = 0;

        virtual bool save() = 0;
        virtual bool load() = 0;
    };
}
