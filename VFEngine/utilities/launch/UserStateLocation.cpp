#include "UserStateLocation.hpp"

#include "Utf8Path.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace launch
{
    namespace
    {
        namespace fs = std::filesystem;

        // FOLDERID_Documents, spelled out so no import library has to provide the symbol (the launcher's
        // WindowsKnownFolders.java uses the same GUID).
        constexpr GUID documentsFolderId = {0xFDD39AD0, 0x238F, 0x46AF, {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}};

        // engine.json is a few hundred bytes; anything far larger is not one.
        constexpr std::uintmax_t maxEngineManifestBytes = 1024 * 1024;

        std::string manifestLabel(const UserStateInputs& inputs)
        {
            return pathToUtf8(inputs.exeDir / engineManifestFileName);
        }

        // What the manifest asks for; Engine (with a problem) for anything it cannot use.
        StateLocation requestedLocation(const UserStateInputs& inputs, std::vector<std::string>& problems)
        {
            if (inputs.engineManifestUnreadable)
            {
                problems.push_back(manifestLabel(inputs) +
                                   " could not be read: the editor keeps its state beside the engine");
                return StateLocation::Engine;
            }
            if (!inputs.engineManifestText)
            {
                return StateLocation::Engine;
            }

            std::string_view text = *inputs.engineManifestText;
            constexpr std::string_view utf8Bom = "\xEF\xBB\xBF";
            if (text.substr(0, utf8Bom.size()) == utf8Bom)
            {
                text.remove_prefix(utf8Bom.size());
            }

            const nlohmann::json manifest = nlohmann::json::parse(text, nullptr, false);
            if (manifest.is_discarded() || !manifest.is_object())
            {
                problems.push_back(manifestLabel(inputs) +
                                   " is not a JSON object: the editor keeps its state beside the engine");
                return StateLocation::Engine;
            }

            const auto member = manifest.find(std::string(stateLocationManifestKey));
            if (member == manifest.end())
            {
                return StateLocation::Engine;
            }
            if (!member->is_string())
            {
                problems.push_back(manifestLabel(inputs) + ": \"" + std::string(stateLocationManifestKey) +
                                   "\" is not a string: the editor keeps its state beside the engine");
                return StateLocation::Engine;
            }

            const std::string& value = member->get_ref<const std::string&>();
            if (value == stateLocationUser)
            {
                return StateLocation::User;
            }
            if (value == stateLocationEngine)
            {
                return StateLocation::Engine;
            }
            problems.push_back(manifestLabel(inputs) + ": unknown \"" + std::string(stateLocationManifestKey) +
                               "\" value \"" + value + "\" (expected \"" + std::string(stateLocationEngine) +
                               "\" or \"" + std::string(stateLocationUser) +
                               "\"): the editor keeps its state beside the engine");
            return StateLocation::Engine;
        }

        fs::path environmentFolder(const wchar_t* name)
        {
            const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
            if (needed <= 1)  // unset, or set to the empty string (just the NUL)
            {
                return {};
            }
            std::wstring value(needed, L'\0');
            const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
            if (written == 0 || written >= needed)
            {
                return {};
            }
            value.resize(written);
            return fs::path(value);
        }

        fs::path documentsFolder()
        {
            PWSTR raw = nullptr;
            fs::path result;
            if (SUCCEEDED(SHGetKnownFolderPath(documentsFolderId, KF_FLAG_DEFAULT, nullptr, &raw)) && raw)
            {
                result = fs::path(raw);
            }
            CoTaskMemFree(raw);  // also when the call failed (it may still have allocated)
            return result;
        }

        fs::path executableFolder()
        {
            std::wstring buffer(MAX_PATH, L'\0');
            for (;;)
            {
                const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return {};
                }
                if (length < buffer.size())
                {
                    buffer.resize(length);
                    return fs::path(buffer).parent_path();
                }
                if (buffer.size() >= 32768)  // the longest path Windows has
                {
                    return {};
                }
                buffer.resize(buffer.size() * 2);
            }
        }

        // Fills the manifest members of `inputs` from <exeDir>/engine.json.
        void readEngineManifest(UserStateInputs& inputs)
        {
            if (inputs.exeDir.empty())
            {
                return;
            }
            const fs::path file = inputs.exeDir / engineManifestFileName;
            std::error_code ec;
            const fs::file_status status = fs::status(file, ec);
            if (ec && status.type() != fs::file_type::not_found)
            {
                inputs.engineManifestUnreadable = true;  // there may be one, but it cannot be looked at
                return;
            }
            if (!fs::exists(status))
            {
                return;  // no manifest: Engine, silently
            }
            const std::uintmax_t size = fs::is_regular_file(status) ? fs::file_size(file, ec) : 0;
            if (!fs::is_regular_file(status) || ec || size > maxEngineManifestBytes)
            {
                inputs.engineManifestUnreadable = true;
                return;
            }
            std::ifstream in(file, std::ios::binary);
            if (!in)
            {
                inputs.engineManifestUnreadable = true;
                return;
            }
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (in.bad())
            {
                inputs.engineManifestUnreadable = true;
                return;
            }
            inputs.engineManifestText = std::move(text);
        }

        // The state from before VK-1732, as relative paths: what a failure falls back to.
        UserStateLocation relativeEngineDefaults(std::string problem)
        {
            UserStateLocation location;
            location.logsDir = fs::path(L"logs");
            location.crashesDir = fs::path(L"crashes");
            location.imguiIniFile = fs::path(L"imgui.ini");
            location.problems.push_back(std::move(problem));
            return location;
        }

        UserStateLocation computeProcessUserState()
        {
            UserStateInputs inputs;
            inputs.exeDir = executableFolder();
            std::error_code ec;
            inputs.workingDir = fs::current_path(ec);
            if (ec)
            {
                inputs.workingDir.clear();
            }
            if (inputs.exeDir.empty())
            {
                inputs.exeDir = inputs.workingDir;  // as ImguiRender and PluginManager fell back before
            }
            readEngineManifest(inputs);
            inputs.profileDir = environmentFolder(L"USERPROFILE");
            if (inputs.profileDir.empty())
            {
                inputs.profileDir = environmentFolder(L"HOME");
            }
            inputs.documentsDir = documentsFolder();
            return resolveUserStateLocation(inputs);
        }
    }

    UserStateLocation resolveUserStateLocation(const UserStateInputs& inputs)
    {
        UserStateLocation location;
        StateLocation mode = requestedLocation(inputs, location.problems);

        if (mode == StateLocation::User && inputs.profileDir.empty())
        {
            location.problems.push_back(manifestLabel(inputs) +
                                        " asks for per-user state, but neither USERPROFILE nor HOME is set: the "
                                        "editor keeps its state beside the engine");
            mode = StateLocation::Engine;
        }

        fs::path engineRoot = inputs.exeDir;
        for (int level = 0; level < 4; ++level)
        {
            engineRoot = engineRoot.parent_path();
        }
        location.engineRoot = engineRoot;
        location.mode = mode;

        if (mode == StateLocation::Engine)
        {
            location.logsDir = inputs.workingDir / L"logs";
            location.crashesDir = inputs.workingDir / L"crashes";
            location.imguiIniFile = (inputs.exeDir.empty() ? inputs.workingDir : inputs.exeDir) / L"imgui.ini";
            return location;
        }

        const fs::path root = inputs.profileDir / profileStateDirName / userEditorStateDirName;
        location.stateRoot = root;
        location.logsDir = root / L"logs";
        location.crashesDir = root / L"crashes";
        location.imguiIniFile = root / L"imgui.ini";
        location.pluginDataDir = root / L"plugin-data";
        location.pluginOverridesFile = root / L"plugin-overrides.json";
        location.savesFallbackDir = root / L"saves";
        location.diagnosticsDir = root / L"diagnostics";

        fs::path documents = inputs.documentsDir;
        if (documents.empty())
        {
            documents = inputs.profileDir / L"Documents";
            location.problems.push_back("the Documents folder could not be found: your plugins go in " +
                                        pathToUtf8(documents / userPluginsParentDirName / userPluginsDirName));
        }
        location.userPluginsDir = documents / userPluginsParentDirName / userPluginsDirName;
        return location;
    }

    const UserStateLocation& processUserState() noexcept
    {
        static const UserStateLocation location = []() -> UserStateLocation {
            try
            {
                return computeProcessUserState();
            }
            catch (const std::exception& e)
            {
                return relativeEngineDefaults(std::string("the editor's state folders could not be worked out (") +
                                              e.what() + "): it keeps its state beside the engine");
            }
            catch (...)
            {
                return relativeEngineDefaults(
                    "the editor's state folders could not be worked out: it keeps its state beside the engine");
            }
        }();
        return location;
    }

    std::vector<std::string> createUserStateFolders(const UserStateLocation& location)
    {
        std::vector<std::string> problems;
        if (!location.isUser())
        {
            return problems;
        }
        for (const fs::path* folder : {&location.stateRoot, &location.logsDir, &location.crashesDir})
        {
            if (folder->empty())
            {
                continue;
            }
            std::error_code ec;
            fs::create_directories(*folder, ec);
            if (ec)
            {
                problems.push_back("cannot create " + pathToUtf8(*folder) + ": " + ec.message());
            }
        }
        return problems;
    }
}
