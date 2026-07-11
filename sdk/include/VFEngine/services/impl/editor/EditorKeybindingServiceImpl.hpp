#pragma once
#include "../../interfaces/editor/IEditorKeybindingService.hpp"
#include <unordered_map>
#include <mutex>

namespace services
{
    class EditorKeybindingServiceImpl : public IEditorKeybindingService
    {
    private:
        struct EditorActionEntry
        {
            std::string category;
            std::string displayName;
            std::vector<InputBinding> currentBindings;
            std::vector<InputBinding> defaultBindings;
        };

        std::unordered_map<std::string, EditorActionEntry> actions;
        std::unordered_map<std::string, std::vector<InputBinding>> persistedBindings;
        mutable std::mutex actionsMutex;
        bool loaded = false;

    public:
        EditorKeybindingServiceImpl();
        ~EditorKeybindingServiceImpl() override = default;

        void registerEventHandlers() override;

        void registerAction(const std::string& name, const std::string& category,
                            const std::string& displayName,
                            const std::vector<InputBinding>& defaultBindings) override;

        bool isActionPressed(const std::string& name) const override;

        std::vector<InputBinding> getBindings(const std::string& name) const override;
        void setBindings(const std::string& name, const std::vector<InputBinding>& bindings) override;
        void resetBindings(const std::string& name) override;
        void resetAll() override;

        std::vector<EditorActionInfo> getAllActions() const override;
        std::vector<KeybindingConflict> getConflicts(const std::string& actionName,
                                                      const InputBinding& binding) const override;

        bool save() override;
        bool load() override;

    private:
        void ensureLoaded();
        std::string getKeybindingsPath() const;
        bool checkBinding(const InputBinding& binding) const;
    };
}
