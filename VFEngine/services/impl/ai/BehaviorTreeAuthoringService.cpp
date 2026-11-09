#include "BehaviorTreeAuthoringService.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ai/BehaviorTreeEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "archive/VFPakFormat.hpp"
#include "asset/AssetRef.hpp"
#include "behaviortree/BehaviorTreeAsset.hpp"
#include "behaviortree/BehaviorTreeNodeCatalog.hpp"
#include "behaviortree/BehaviorTreeValidation.hpp"
#include "behaviortree/ScriptTaskScan.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace services
{
    namespace
    {
        namespace fs = std::filesystem;
        namespace bt = behaviortree;

        // BehaviorTreeAsset's own load limit: never pull a bigger file into memory just to hash it.
        constexpr uintmax_t maxTreeFileBytes = 10 * 1024 * 1024;
        // The MCP script tools' limit.
        constexpr uintmax_t maxScriptFileBytes = 1024 * 1024;

        bool hasExtension(const std::string& path, std::string_view lowercaseExtension)
        {
            std::string extension = fs::path(path).extension().string();
            std::ranges::transform(extension, extension.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == lowercaseExtension;
        }

        // Authoring paths are absolute .vfBehaviorTree paths (extension in any case).
        bool checkTreePath(const std::string& path, std::string& error)
        {
            if (path.empty())
            {
                error = "No behavior tree path given";
                return false;
            }
            if (!fs::path(path).is_absolute())
            {
                error = std::format("'{}' is not an absolute path", path);
                return false;
            }
            if (!hasExtension(path, ".vfbehaviortree"))
            {
                error = std::format("'{}' is not a .vfBehaviorTree path", path);
                return false;
            }
            return true;
        }

        // The file's bytes, or nullopt with `error` set when it is missing, too large or unreadable.
        std::optional<std::vector<uint8_t>> readFileBytes(const fs::path& path, uintmax_t maxBytes, std::string& error)
        {
            std::error_code ec;
            if (!fs::exists(path, ec))
            {
                error = std::format("'{}' does not exist", path.string());
                return std::nullopt;
            }
            if (!fs::is_regular_file(path, ec))
            {
                error = std::format("'{}' is not a file", path.string());
                return std::nullopt;
            }

            const uintmax_t size = fs::file_size(path, ec);
            if (ec)
            {
                error = std::format("'{}' could not be read: {}", path.string(), ec.message());
                return std::nullopt;
            }
            if (size > maxBytes)
            {
                error = std::format("'{}' is too large ({} bytes)", path.string(), size);
                return std::nullopt;
            }

            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open() ||
                (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))))
            {
                error = std::format("'{}' could not be read", path.string());
                return std::nullopt;
            }
            return bytes;
        }

        std::string_view asText(const std::vector<uint8_t>& bytes)
        {
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }

        // 64-bit FNV-1a of the file bytes - the same hash the animator authoring events report - which
        // LoadBehaviorTreeAssetQuery returns and SaveBehaviorTreeAssetCommand::expectedContentHash is
        // compared against.
        uint64_t contentHashOf(const std::vector<uint8_t>& bytes)
        {
            return archive::hashBytes(bytes.data(), bytes.size());
        }

        // The path live runtimes of a component-attached tree are keyed by: the asset database's path for
        // the file (BehaviorTreePlayModeHandler attaches behaviorTreeRef.resolve()).
        std::string runtimePathOf(const std::string& path)
        {
            const asset::AssetRef ref = asset::AssetRef::fromPath(path);
            std::string resolved = ref.isValid() ? ref.resolve() : std::string();
            return resolved.empty() ? path : resolved;
        }

        // <workingDirectory>/scripts of the current project (ScriptingAdapter builds from the manifest
        // there); empty without a project or a project service.
        fs::path currentProjectScriptsRoot()
        {
            try
            {
                const auto project =
                    ::events::EventDispatcher::instance().query(::events::project::GetCurrentProjectQuery{});
                if (project.has_value() && !project->workingDirectory.empty())
                    return fs::absolute(fs::path(project->workingDirectory)) / "scripts";
            }
            catch (const std::exception&)
            {
                // No project service registered (tests): ScriptTask paths resolve as given.
            }
            return {};
        }

        bool isError(const bt::validation::Diagnostic& diagnostic)
        {
            return diagnostic.severity == bt::validation::Severity::Error;
        }
    }

    BehaviorTreeAuthoringService::BehaviorTreeAuthoringService(ScriptsRootProvider rootProvider)
        : scriptsRoot(rootProvider ? std::move(rootProvider) : ScriptsRootProvider(currentProjectScriptsRoot))
    {
    }

    BehaviorTreeAuthoringService::~BehaviorTreeAuthoringService()
    {
        unregisterEventHandlers();
    }

    void BehaviorTreeAuthoringService::registerEventHandlers()
    {
        if (handlersRegistered)
            return;
        handlersRegistered = true;

        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.registerQueryHandler<::events::ai::LoadBehaviorTreeAssetQuery>(
            [](const ::events::ai::LoadBehaviorTreeAssetQuery& query)
            {
                return loadAsset(query.path);
            });
        dispatcher.registerCommandHandler<::events::ai::SaveBehaviorTreeAssetCommand>(
            [this](const ::events::ai::SaveBehaviorTreeAssetCommand& cmd)
            {
                return saveAsset(cmd);
            });
    }

    void BehaviorTreeAuthoringService::unregisterEventHandlers()
    {
        if (!handlersRegistered)
            return;
        handlersRegistered = false;

        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.unregisterQueryHandler<::events::ai::LoadBehaviorTreeAssetQuery>();
        dispatcher.unregisterCommandHandler<::events::ai::SaveBehaviorTreeAssetCommand>();
    }

    BehaviorTreeAssetLoadResult BehaviorTreeAuthoringService::loadAsset(const std::string& path)
    {
        BehaviorTreeAssetLoadResult result;
        if (!checkTreePath(path, result.error))
            return result;

        // Parse exactly the bytes that are hashed, so the hash always describes `data`: if the file
        // changes afterwards, a save guarded by this hash refuses instead of overwriting it.
        const auto bytes = readFileBytes(fs::path(path), maxTreeFileBytes, result.error);
        if (!bytes)
            return result;

        auto data = bt::BehaviorTreeAsset::parse(asText(*bytes));
        if (!data)
        {
            result.error = std::format("'{}' is not a behavior tree (invalid JSON, or the root is not an object)", path);
            return result;
        }

        result.data = std::move(*data);
        result.contentHash = contentHashOf(*bytes);
        result.success = true;
        return result;
    }

    BehaviorTreeAssetSaveResult BehaviorTreeAuthoringService::saveAsset(
        const ::events::ai::SaveBehaviorTreeAssetCommand& cmd) const
    {
        BehaviorTreeAssetSaveResult result;
        const std::string& path = cmd.path;
        // The last guard before a JSON write: never clobber another asset.
        if (!checkTreePath(path, result.error))
            return result;

        std::error_code ec;
        const bool existed = fs::exists(fs::path(path), ec);
        if (cmd.createNew && existed)
        {
            result.error = std::format("'{}' already exists", path);
            return result;
        }
        if (cmd.expectedContentHash.has_value())
        {
            if (!existed)
            {
                result.error = std::format("'{}' no longer exists", path);
                return result;
            }
            const auto current = readFileBytes(fs::path(path), maxTreeFileBytes, result.error);
            if (!current)
                return result;
            if (contentHashOf(*current) != *cmd.expectedContentHash)
            {
                result.error = std::format("'{}' changed on disk since it was read", path);
                return result;
            }
        }

        // Engine validation (permissive, the editor's rules; SubTree files must exist) plus the strict
        // catalogue checks, scoped to the nodes the edit touched.
        bt::validation::ValidationContext engineContext;
        engineContext.subtreeExists = [](std::string_view treePath) { return bt::BehaviorTreeAsset::exists(treePath); };
        std::vector<bt::validation::Diagnostic> diagnostics =
            bt::validation::validateBehaviorTree(cmd.data, engineContext).diagnostics;

        bt::catalog::StrictContext strictContext;
        strictContext.scriptClass = [this](std::string_view scriptPath) { return scriptTaskClass(scriptPath); };
        strictContext.treeExists = [](std::string_view treePath) { return bt::BehaviorTreeAsset::exists(treePath); };
        strictContext.onlyNodes = cmd.strictNodes;
        std::vector<bt::validation::Diagnostic> strict = bt::catalog::validateStrict(cmd.data, strictContext);
        diagnostics.insert(diagnostics.end(), std::make_move_iterator(strict.begin()), std::make_move_iterator(strict.end()));

        const auto errorCount = std::ranges::count_if(diagnostics, isError);
        if (errorCount > 0)
        {
            const auto first = std::ranges::find_if(diagnostics, isError);
            result.error = std::format("'{}' was not saved: {} validation error(s), the first: {}",
                                       path, errorCount, first->message);
            result.diagnostics = std::move(diagnostics);
            return result;
        }
        result.diagnostics = std::move(diagnostics);

        if (!bt::BehaviorTreeAsset::save(path, cmd.data))
        {
            result.error = std::format("'{}' could not be written (see the log)", path);
            return result;
        }

        // Hash exactly what was written, before any subscriber reacts to the save.
        std::string readError;
        const auto written = readFileBytes(fs::path(path), maxTreeFileBytes, readError);

        // The asset database registers the GUID/.vfmeta; an open BT editor reloads or shows a conflict.
        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::resource::AssetSavedNotification notification;
        notification.filePath = path;
        dispatcher.publish(notification);

        // Rebind every live runtime using the tree before its next tick (a no-op outside Play).
        ::events::ai::ReloadBehaviorTreeAssetCommand reload;
        reload.treePath = runtimePathOf(path);
        try
        {
            dispatcher.execute(reload);
        }
        catch (const std::exception& e)
        {
            vfLogWarning("[BT] Saved '{}', but live behavior trees were not reloaded: {}", path, e.what());
        }

        if (!written)
        {
            result.error = std::format("saved, but the file could not be read back: {}", readError);
            return result;
        }

        result.success = true;
        result.created = !existed;
        result.contentHash = contentHashOf(*written);
        return result;
    }

    std::optional<std::string> BehaviorTreeAuthoringService::scriptTaskClass(std::string_view scriptPath) const
    {
        if (scriptPath.empty())
            return std::nullopt;

        // Mirrors ScriptingAdapter::loadScript: "<script library>/<path>", falling back to the path as
        // given (e.g. an absolute one) when the joined path does not exist.
        const std::string given(scriptPath);
        const fs::path root = scriptsRoot ? scriptsRoot() : fs::path();
        fs::path file = root.empty() ? fs::path(given) : fs::path(root.string() + "/" + given);
        std::error_code ec;
        if (!fs::exists(file, ec) && fs::exists(fs::path(given), ec))
            file = fs::path(given);

        std::string error;
        const auto bytes = readFileBytes(file, maxScriptFileBytes, error);
        if (!bytes)
            return std::nullopt;

        std::optional<bt::ScriptTaskInfo> info = bt::scanScriptTask(asText(*bytes));
        if (!info)
            return std::nullopt;
        return std::move(info->className);
    }
}
