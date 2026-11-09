#include "EQSAuthoringService.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "archive/VFPakFormat.hpp"
#include "eqs/EQSQuerySpec.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace services
{
    namespace
    {
        namespace fs = std::filesystem;

        // Templates are small; never pull a bigger file into memory just to hash it.
        constexpr uintmax_t maxTemplateFileBytes = 1024 * 1024;

        bool hasTemplateExtension(const fs::path& path)
        {
            std::string extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == ".vfeqsquery";
        }

        // Authoring paths are absolute .vfEQSQuery paths whose file stem is the query's name.
        bool checkTemplatePath(const std::string& path, std::string& error)
        {
            if (path.empty())
            {
                error = "No EQS query path given";
                return false;
            }
            const fs::path file(path);
            if (!file.is_absolute())
            {
                error = std::format("'{}' is not an absolute path", path);
                return false;
            }
            if (!hasTemplateExtension(file))
            {
                error = std::format("'{}' is not a .vfEQSQuery path", path);
                return false;
            }
            const std::string name = file.stem().string();
            if (!eqs::isValidQueryName(name))
            {
                error = std::format("'{}' is not a valid query name (a letter, then up to 63 letters, digits or '_')",
                                    name);
                return false;
            }
            return true;
        }

        // The file's bytes, or nullopt with `error` set when it is missing, too large or unreadable.
        std::optional<std::vector<uint8_t>> readFileBytes(const fs::path& path, std::string& error)
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
            if (size > maxTemplateFileBytes)
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

        // 64-bit FNV-1a of the file bytes - the hash the BT and animator authoring events report too.
        uint64_t contentHashOf(const std::vector<uint8_t>& bytes)
        {
            return archive::hashBytes(bytes.data(), bytes.size());
        }

        // Writes `text` to a sibling "<path>.tmp", then renames it over `path`, so a failed write never leaves
        // a truncated template behind.
        bool writeAtomically(const fs::path& path, const std::string& text, std::string& error)
        {
            std::error_code ec;
            if (path.has_parent_path())
            {
                fs::create_directories(path.parent_path(), ec);
                if (ec)
                {
                    error = std::format("'{}' could not be created: {}", path.parent_path().string(), ec.message());
                    return false;
                }
            }

            fs::path tempPath = path;
            tempPath += ".tmp";
            {
                std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                {
                    error = std::format("'{}' could not be created", tempPath.string());
                    return false;
                }
                file << text;
                file.flush();
                if (!file.good())
                {
                    file.close();
                    std::error_code removeError;
                    fs::remove(tempPath, removeError);
                    error = std::format("'{}' could not be written", tempPath.string());
                    return false;
                }
            }

            // std::filesystem::rename replaces an existing file (MoveFileExW REPLACE_EXISTING on MSVC).
            fs::rename(tempPath, path, ec);
            if (ec)
            {
                std::error_code removeError;
                fs::remove(tempPath, removeError);
                error = std::format("'{}' could not be replaced: {}", path.string(), ec.message());
                return false;
            }
            return true;
        }

        std::string describeSpecErrors(const std::vector<eqs::EQSSpecError>& errors)
        {
            const eqs::EQSSpecError& first = errors.front();
            const std::string where = first.path.empty() ? std::string() : first.path + ": ";
            return std::format("{} spec error(s), the first: {}{}", errors.size(), where, first.message);
        }
    }

    EQSAuthoringService::~EQSAuthoringService()
    {
        unregisterEventHandlers();
    }

    void EQSAuthoringService::registerEventHandlers()
    {
        if (handlersRegistered)
            return;
        handlersRegistered = true;

        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.registerQueryHandler<::events::ai::LoadEQSQueryAssetQuery>(
            [](const ::events::ai::LoadEQSQueryAssetQuery& query)
            {
                return loadAsset(query.path);
            });
        dispatcher.registerCommandHandler<::events::ai::SaveEQSQueryAssetCommand>(
            [](const ::events::ai::SaveEQSQueryAssetCommand& cmd)
            {
                return saveAsset(cmd);
            });
    }

    void EQSAuthoringService::unregisterEventHandlers()
    {
        if (!handlersRegistered)
            return;
        handlersRegistered = false;

        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.unregisterQueryHandler<::events::ai::LoadEQSQueryAssetQuery>();
        dispatcher.unregisterCommandHandler<::events::ai::SaveEQSQueryAssetCommand>();
    }

    EQSAssetLoadResult EQSAuthoringService::loadAsset(const std::string& path)
    {
        EQSAssetLoadResult result;
        if (!checkTemplatePath(path, result.error))
            return result;

        const auto bytes = readFileBytes(fs::path(path), result.error);
        if (!bytes)
            return result;

        result.text.assign(bytes->begin(), bytes->end());
        result.contentHash = contentHashOf(*bytes);
        result.success = true;
        return result;
    }

    EQSAssetSaveResult EQSAuthoringService::saveAsset(const ::events::ai::SaveEQSQueryAssetCommand& cmd)
    {
        EQSAssetSaveResult result;
        const std::string& path = cmd.path;
        // The last guard before a JSON write: never clobber another asset.
        if (!checkTemplatePath(path, result.error))
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
            const auto current = readFileBytes(fs::path(path), result.error);
            if (!current)
                return result;
            if (contentHashOf(*current) != *cmd.expectedContentHash)
            {
                result.error = std::format("'{}' changed on disk since it was read", path);
                return result;
            }
        }

        // The canonical text, checked again with the same strict parser the template is loaded with: a
        // spec built by hand (not by parseQuerySpec) can hold values no template may.
        std::string text;
        try
        {
            text = eqs::querySpecToText(cmd.spec);
        }
        catch (const std::exception& e)
        {
            result.error = std::format("'{}' was not saved: the query could not be written as JSON: {}", path, e.what());
            return result;
        }

        eqs::EQSSpecParseResult parsed = eqs::parseQuerySpecText(text);
        if (!parsed.errors.empty())
        {
            result.error = std::format("'{}' was not saved: {}", path, describeSpecErrors(parsed.errors));
            result.errors = std::move(parsed.errors);
            return result;
        }

        if (!writeAtomically(fs::path(path), text, result.error))
            return result;

        // Hash exactly what was written, before any subscriber reacts to the save.
        std::string readError;
        const auto written = readFileBytes(fs::path(path), readError);

        // The EQS library drops its cached copy; the asset database registers the GUID/.vfmeta.
        ::events::resource::AssetSavedNotification notification;
        notification.filePath = path;
        ::events::EventDispatcher::instance().publish(notification);

        if (!written)
        {
            result.error = std::format("saved, but the file could not be read back: {}", readError);
            return result;
        }

        vfLogInfo("[EQS] Saved query template: {}", path);
        result.success = true;
        result.created = !existed;
        result.contentHash = contentHashOf(*written);
        return result;
    }
}
