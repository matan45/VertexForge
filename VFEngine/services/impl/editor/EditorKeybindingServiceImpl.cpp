#include "EditorKeybindingServiceImpl.hpp"
#include "../../events/editor/EditorKeybindingEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "print/Log.hpp"
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace services
{
    using json = nlohmann::json;

    EditorKeybindingServiceImpl::EditorKeybindingServiceImpl() = default;

    void EditorKeybindingServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<::events::editor::RegisterEditorActionCommand>(
            [this](const ::events::editor::RegisterEditorActionCommand& cmd)
            {
                registerAction(cmd.actionName, cmd.category, cmd.displayName, cmd.defaultBindings);
            });

        dispatcher.registerCommandHandler<::events::editor::SetEditorActionBindingsCommand>(
            [this](const ::events::editor::SetEditorActionBindingsCommand& cmd)
            {
                setBindings(cmd.actionName, cmd.bindings);
            });

        dispatcher.registerCommandHandler<::events::editor::ResetEditorActionBindingsCommand>(
            [this](const ::events::editor::ResetEditorActionBindingsCommand& cmd)
            {
                if (cmd.actionName.empty())
                    resetAll();
                else
                    resetBindings(cmd.actionName);
            });

        dispatcher.registerCommandHandler<::events::editor::SaveEditorKeybindingsCommand>(
            [this](const ::events::editor::SaveEditorKeybindingsCommand&)
            {
                return save();
            });

        dispatcher.registerQueryHandler<::events::editor::GetEditorActionBindingsQuery>(
            [this](const ::events::editor::GetEditorActionBindingsQuery& q)
            {
                return getBindings(q.actionName);
            });

        dispatcher.registerQueryHandler<::events::editor::GetAllEditorActionsQuery>(
            [this](const ::events::editor::GetAllEditorActionsQuery&)
            {
                return getAllActions();
            });

        dispatcher.registerQueryHandler<::events::editor::IsEditorActionPressedQuery>(
            [this](const ::events::editor::IsEditorActionPressedQuery& q)
            {
                return isActionPressed(q.actionName);
            });

        dispatcher.registerQueryHandler<::events::editor::GetKeybindingConflictsQuery>(
            [this](const ::events::editor::GetKeybindingConflictsQuery& q)
            {
                return getConflicts(q.actionName, q.binding);
            });
    }

    void EditorKeybindingServiceImpl::registerAction(const std::string& name, const std::string& category,
                                                      const std::string& displayName,
                                                      const std::vector<InputBinding>& defaultBindings)
    {
        // Load persisted data before taking actionsMutex. load() also locks the
        // action map, so calling ensureLoaded() under this lock deadlocks once
        // the keybindings file exists.
        ensureLoaded();

        std::lock_guard<std::mutex> lock(actionsMutex);

        if (actions.contains(name))
            return;

        EditorActionEntry entry;
        entry.category = category;
        entry.displayName = displayName;
        entry.defaultBindings = defaultBindings;
        const auto saved = persistedBindings.find(name);
        entry.currentBindings = saved != persistedBindings.end()
                                    ? saved->second
                                    : defaultBindings;
        actions[name] = std::move(entry);
    }

    bool EditorKeybindingServiceImpl::isActionPressed(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(actionsMutex);
        auto it = actions.find(name);
        if (it == actions.end()) return false;

        for (const auto& binding : it->second.currentBindings)
        {
            if (checkBinding(binding))
                return true;
        }
        return false;
    }

    bool EditorKeybindingServiceImpl::checkBinding(const InputBinding& binding) const
    {
        bool shiftHeld = ImGui::IsKeyDown(ImGuiMod_Shift);
        bool ctrlHeld = ImGui::IsKeyDown(ImGuiMod_Ctrl);
        bool altHeld = ImGui::IsKeyDown(ImGuiMod_Alt);

        if (binding.requireShift != shiftHeld) return false;
        if (binding.requireCtrl != ctrlHeld) return false;
        if (binding.requireAlt != altHeld) return false;

        if (binding.type == BindingType::Key)
            return ImGui::IsKeyPressed(static_cast<ImGuiKey>(binding.code), false);
        else
            return ImGui::IsMouseClicked(binding.code);
    }

    std::vector<InputBinding> EditorKeybindingServiceImpl::getBindings(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(actionsMutex);
        auto it = actions.find(name);
        if (it == actions.end()) return {};
        return it->second.currentBindings;
    }

    void EditorKeybindingServiceImpl::setBindings(const std::string& name, const std::vector<InputBinding>& bindings)
    {
        std::lock_guard<std::mutex> lock(actionsMutex);
        auto it = actions.find(name);
        if (it == actions.end()) return;
        it->second.currentBindings = bindings;
    }

    void EditorKeybindingServiceImpl::resetBindings(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(actionsMutex);
        auto it = actions.find(name);
        if (it == actions.end()) return;
        it->second.currentBindings = it->second.defaultBindings;
    }

    void EditorKeybindingServiceImpl::resetAll()
    {
        std::lock_guard<std::mutex> lock(actionsMutex);
        for (auto& [name, entry] : actions)
            entry.currentBindings = entry.defaultBindings;
    }

    std::vector<EditorActionInfo> EditorKeybindingServiceImpl::getAllActions() const
    {
        std::lock_guard<std::mutex> lock(actionsMutex);
        std::vector<EditorActionInfo> result;
        result.reserve(actions.size());
        for (const auto& [name, entry] : actions)
        {
            EditorActionInfo info;
            info.name = name;
            info.category = entry.category;
            info.displayName = entry.displayName;
            info.currentBindings = entry.currentBindings;
            info.defaultBindings = entry.defaultBindings;
            result.push_back(std::move(info));
        }
        return result;
    }

    std::vector<KeybindingConflict> EditorKeybindingServiceImpl::getConflicts(const std::string& actionName,
                                                                               const InputBinding& binding) const
    {
        std::lock_guard<std::mutex> lock(actionsMutex);
        std::vector<KeybindingConflict> conflicts;
        for (const auto& [name, entry] : actions)
        {
            if (name == actionName) continue;
            for (const auto& b : entry.currentBindings)
            {
                if (b == binding)
                {
                    conflicts.push_back({name, b});
                    break;
                }
            }
        }
        return conflicts;
    }

    bool EditorKeybindingServiceImpl::save()
    {
        std::string path = getKeybindingsPath();
        if (path.empty()) return false;

        try
        {
            auto parentDir = std::filesystem::path(path).parent_path();
            if (!std::filesystem::exists(parentDir))
                std::filesystem::create_directories(parentDir);

            json j = json::object();
            std::lock_guard<std::mutex> lock(actionsMutex);
            for (const auto& [name, entry] : actions)
            {
                persistedBindings[name] = entry.currentBindings;
                json bindings = json::array();
                for (const auto& b : entry.currentBindings)
                {
                    bindings.push_back({
                        {"type", static_cast<int>(b.type)},
                        {"code", b.code},
                        {"shift", b.requireShift},
                        {"ctrl", b.requireCtrl},
                        {"alt", b.requireAlt}
                    });
                }
                j[name] = bindings;
            }

            std::ofstream file(path);
            file << j.dump(2);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("[EditorKeybindingService] Failed to save: {}", e.what());
            return false;
        }
    }

    bool EditorKeybindingServiceImpl::load()
    {
        std::string path = getKeybindingsPath();
        if (path.empty() || !std::filesystem::exists(path)) return false;

        try
        {
            std::ifstream file(path);
            json j = json::parse(file);

            std::unordered_map<std::string, std::vector<InputBinding>> loadedBindings;
            for (auto& [name, arr] : j.items())
            {
                if (!arr.is_array()) continue;

                std::vector<InputBinding> bindings;
                for (const auto& bj : arr)
                {
                    InputBinding b;
                    b.type = static_cast<BindingType>(bj.value("type", 0));
                    b.code = bj.value("code", 0);
                    b.requireShift = bj.value("shift", false);
                    b.requireCtrl = bj.value("ctrl", false);
                    b.requireAlt = bj.value("alt", false);
                    bindings.push_back(b);
                }
                loadedBindings.emplace(name, std::move(bindings));
            }

            std::lock_guard<std::mutex> lock(actionsMutex);
            persistedBindings = std::move(loadedBindings);
            for (auto& [name, entry] : actions)
            {
                const auto saved = persistedBindings.find(name);
                if (saved != persistedBindings.end())
                    entry.currentBindings = saved->second;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogWarning("[EditorKeybindingService] Failed to load: {}", e.what());
            return false;
        }
    }

    void EditorKeybindingServiceImpl::ensureLoaded()
    {
        if (loaded) return;
        loaded = true;
        load();
    }

    std::string EditorKeybindingServiceImpl::getKeybindingsPath() const
    {
        char* home = nullptr;
        size_t len = 0;
        _dupenv_s(&home, &len, "USERPROFILE");
        if (!home)
            _dupenv_s(&home, &len, "HOME");
        if (!home)
            return "";

        std::string path = std::string(home) + "/.vertexforge/editor_keybindings.json";
        free(home);
        return path;
    }
}
