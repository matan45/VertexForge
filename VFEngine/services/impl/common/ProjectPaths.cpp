#include "ProjectPaths.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include <filesystem>

namespace services
{
    namespace
    {
        // EventDispatcher::query throws when nothing has registered the handler — the normal
        // state in the unit-test runner, not an error worth propagating into a save.
        std::string projectWorkingDirectory()
        {
            try
            {
                const auto project = ::events::EventDispatcher::instance()
                    .query(::events::project::GetCurrentProjectQuery{});
                return project ? project->workingDirectory : std::string{};
            }
            catch (const std::exception&)
            {
                return {};
            }
        }
    }

    std::string toProjectRelativePath(const std::string& path)
    {
        namespace fs = std::filesystem;

        if (path.empty())
            return path;

        const std::string workingDirectory = projectWorkingDirectory();
        if (workingDirectory.empty())
            return path;

        std::error_code ec;
        const fs::path relative = fs::relative(fs::path(path), fs::path(workingDirectory), ec);
        if (ec || relative.empty())
            return path;

        // fs::relative happily walks upward with "..", which is no more portable than the
        // absolute path we started from.
        if (const auto first = relative.begin(); first != relative.end() && *first == "..")
            return path;

        return relative.generic_string();
    }

    std::string resolveProjectPath(const std::string& storedPath)
    {
        namespace fs = std::filesystem;

        if (storedPath.empty())
            return storedPath;

        const fs::path stored(storedPath);
        if (stored.is_absolute())
            return storedPath;

        const std::string workingDirectory = projectWorkingDirectory();
        if (workingDirectory.empty())
            return storedPath;

        return (fs::path(workingDirectory) / stored).generic_string();
    }
}
