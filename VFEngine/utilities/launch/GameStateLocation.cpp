#include "GameStateLocation.hpp"

#include "Utf8Path.hpp"
#include "../string/FileNameSanitize.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
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

        // FOLDERID_LocalAppData (KnownFolders.h), spelled out so no import library has to provide the symbol.
        constexpr GUID localAppDataFolderId = {0xF1B32785, 0x6FBA, 0x4FCF, {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};

        // A .vfproj is a few hundred bytes; anything far larger is not one.
        constexpr std::uintmax_t maxProjectFileBytes = 1024 * 1024;

        constexpr std::string_view projectNameKey = "projectName";

        std::string inQuotes(std::string_view text)
        {
            return "\"" + std::string(text) + "\"";
        }

        // Portable: everything in `root` (the .vfproj's folder, or the working directory without a project).
        void keepStateIn(GameStateLocation& location, const fs::path& root)
        {
            location.mode = GameStateMode::Portable;
            location.stateRoot = root;
            location.logsDir = root / L"logs";
            location.crashesDir = root / L"crashes";
        }

        fs::path localAppDataFolder()
        {
            PWSTR raw = nullptr;
            fs::path result;
            if (SUCCEEDED(SHGetKnownFolderPath(localAppDataFolderId, KF_FLAG_DEFAULT, nullptr, &raw)) && raw)
            {
                result = fs::path(raw);
            }
            CoTaskMemFree(raw);  // also when the call failed (it may still have allocated)
            return result;
        }

        // The text of the .vfproj, nullopt when it cannot be read (missing, not a file, too large, a read error).
        std::optional<std::string> readProjectText(const fs::path& file)
        {
            std::error_code ec;
            const fs::file_status status = fs::status(file, ec);
            if (ec || !fs::is_regular_file(status))
            {
                return std::nullopt;
            }
            const std::uintmax_t size = fs::file_size(file, ec);
            if (ec || size > maxProjectFileBytes)
            {
                return std::nullopt;
            }
            std::ifstream in(file, std::ios::binary);
            if (!in)
            {
                return std::nullopt;
            }
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (in.bad())
            {
                return std::nullopt;
            }
            return text;
        }

        // What a failure falls back to: the state from before VK-1726, as relative paths.
        GameStateLocation relativeDefaults(std::string problem)
        {
            GameStateLocation location;
            location.mode = GameStateMode::Portable;
            location.logsDir = fs::path(L"logs");
            location.crashesDir = fs::path(L"crashes");
            location.problems.push_back(std::move(problem));
            return location;
        }

        GameStateLocation computeProcessGameState(std::string_view projectArgument)
        {
            GameStateInputs inputs;
            std::error_code ec;
            inputs.workingDir = fs::current_path(ec);
            if (ec)
            {
                inputs.workingDir.clear();
            }
            if (!projectArgument.empty())
            {
                const fs::path argument = utf8ToPath(projectArgument);
                const fs::path absolute = fs::absolute(argument, ec);
                inputs.projectFile = ec ? argument : absolute.lexically_normal();
                inputs.projectText = readProjectText(inputs.projectFile);
            }
            inputs.localAppDataDir = localAppDataFolder();
            return resolveGameStateLocation(inputs);
        }
    }

    GameIdentityRead readGameIdentity(std::string_view projectText)
    {
        GameIdentityRead read;
        try
        {
            constexpr std::string_view utf8Bom = "\xEF\xBB\xBF";
            if (projectText.substr(0, utf8Bom.size()) == utf8Bom)
            {
                projectText.remove_prefix(utf8Bom.size());
            }

            const nlohmann::json project = nlohmann::json::parse(projectText, nullptr, false);
            if (project.is_discarded() || !project.is_object())
            {
                read.problems.push_back("is not a JSON object: the game keeps its state in its own folder");
                return read;
            }

            const auto name = project.find(std::string(projectNameKey));
            if (name == project.end() || !name->is_string() || name->get_ref<const std::string&>().empty())
            {
                read.problems.push_back("has no " + inQuotes(projectNameKey) +
                                        ": the game keeps its state in its own folder");
                return read;
            }

            GameIdentity identity;
            identity.projectName = name->get<std::string>();

            if (const auto publisher = project.find(std::string(projectPublisherKey)); publisher != project.end())
            {
                if (publisher->is_string())
                {
                    identity.publisher = publisher->get<std::string>();
                }
                else
                {
                    read.problems.push_back("has a " + inQuotes(projectPublisherKey) +
                                            " that is not a string: it is ignored");
                }
            }

            if (const auto location = project.find(std::string(projectStateLocationKey)); location != project.end())
            {
                if (location->is_string())
                {
                    identity.stateLocation = location->get<std::string>();
                }
                else
                {
                    read.problems.push_back("has a " + inQuotes(projectStateLocationKey) +
                                            " that is not a string: the game keeps its state in the per-user folder");
                }
            }

            read.identity = std::move(identity);
        }
        catch (const std::exception& e)
        {
            read.identity.reset();
            read.problems.push_back(std::string("could not be read (") + e.what() +
                                    "): the game keeps its state in its own folder");
        }
        return read;
    }

    GameFolderName gameStateFolderName(std::string_view projectName, std::string_view publisher)
    {
        GameFolderName result;
        try
        {
            std::string game = strutil::toSafeFolderName(projectName, maxGameFolderNameUnits, {});
            if (game.empty())
            {
                game = std::string(fallbackGameFolderName);
                result.problems.push_back("the game's name " + inQuotes(projectName) +
                                          " leaves no usable folder name: its state goes in " + inQuotes(game));
            }

            std::string publisherFolder = strutil::toSafeFolderName(publisher, maxGameFolderNameUnits, {});
            if (publisherFolder.empty() && publisher.find_first_not_of(' ') != std::string_view::npos)
            {
                result.problems.push_back("the publisher " + inQuotes(publisher) +
                                          " leaves no usable folder name: it is left out of the state folder");
            }

            result.relative = publisherFolder.empty() ? utf8ToPath(game) : utf8ToPath(publisherFolder) / utf8ToPath(game);
        }
        catch (const std::exception& e)
        {
            // Only text that is not valid UTF-8 gets here (utf8ToPath).
            result.relative = utf8ToPath(fallbackGameFolderName);
            result.problems.push_back(std::string("the game's or the publisher's name is not usable as a folder name (") +
                                      e.what() + "): its state goes in " + inQuotes(fallbackGameFolderName));
        }
        return result;
    }

    GameStateLocation resolveGameStateLocation(const GameStateInputs& inputs)
    {
        GameStateLocation location;

        if (inputs.projectFile.empty())
        {
            location.problems.push_back("no .vfproj was given and none lies in " + pathToUtf8(inputs.workingDir) +
                                        ": the game keeps its state there");
            keepStateIn(location, inputs.workingDir);
            return location;
        }

        const fs::path projectFolder = inputs.projectFile.parent_path();
        const std::string label = pathToUtf8(inputs.projectFile);

        if (!inputs.projectText)
        {
            location.problems.push_back(label + " could not be read: the game keeps its state in its own folder");
            keepStateIn(location, projectFolder);
            return location;
        }

        GameIdentityRead read = readGameIdentity(*inputs.projectText);
        for (const std::string& problem : read.problems)
        {
            location.problems.push_back(label + " " + problem);
        }
        if (!read.identity)
        {
            keepStateIn(location, projectFolder);
            return location;
        }
        const GameIdentity& identity = *read.identity;

        if (identity.stateLocation)
        {
            if (*identity.stateLocation == gameStateLocationPortable)
            {
                keepStateIn(location, projectFolder);
                return location;
            }
            if (*identity.stateLocation != gameStateLocationUser)
            {
                location.problems.push_back(label + ": unknown " + inQuotes(projectStateLocationKey) + " value " +
                                            inQuotes(*identity.stateLocation) + " (expected " +
                                            inQuotes(gameStateLocationUser) + " or " + inQuotes(gameStateLocationPortable) +
                                            "): the game keeps its state in the per-user folder");
            }
        }

        if (inputs.localAppDataDir.empty())
        {
            location.problems.push_back("the per-user folder (LocalAppData) is unknown: the game keeps its state in " +
                                        pathToUtf8(projectFolder));
            keepStateIn(location, projectFolder);
            return location;
        }

        GameFolderName name = gameStateFolderName(identity.projectName, identity.publisher);
        for (const std::string& problem : name.problems)
        {
            location.problems.push_back(label + ": " + problem);
        }

        const fs::path root = inputs.localAppDataDir / name.relative;
        location.mode = GameStateMode::User;
        location.stateRoot = root;
        location.logsDir = root / L"logs";
        location.crashesDir = root / L"crashes";
        return location;
    }

    fs::path findGameProjectFile(const fs::path& folder)
    {
        try
        {
            std::error_code ec;
            for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
            {
                if (it->path().extension() == L".vfproj")
                {
                    return it->path();
                }
            }
        }
        catch (const std::exception&)
        {
            // A name that cannot be compared (it never can be, for a path the iterator itself produced).
        }
        return {};
    }

    GameStateLocation processGameState(std::string_view projectArgument) noexcept
    {
        std::string reason;
        try
        {
            return computeProcessGameState(projectArgument);
        }
        catch (const std::exception& e)
        {
            try
            {
                reason = std::string(" (") + e.what() + ")";
            }
            catch (...)
            {
            }
        }
        catch (...)
        {
        }
        try
        {
            return relativeDefaults("the game's state folders could not be worked out" + reason +
                                    ": it keeps its state beside its .vfproj");
        }
        catch (...)
        {
            return GameStateLocation{};  // out of memory: no problem text, but nothing escapes
        }
    }

    std::vector<std::string> createGameStateFolders(const GameStateLocation& location)
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
