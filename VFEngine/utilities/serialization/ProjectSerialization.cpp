#include "ProjectSerialization.hpp"
#include "../print/Log.hpp"
#include "../config/Config.hpp"
#include "SerializationFileAccess.hpp"
#include "../resource/AtomicFileReplace.hpp"
#include "../launch/GameStateLocation.hpp"
#include <fstream>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <chrono>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <filesystem>
#include <system_error>
#include <thread>

namespace serialization
{
    namespace
    {
        using VersionTriple = std::array<std::uint64_t, 3>;

        // MAJOR.MINOR.PATCH, digits only and without leading zeros. With productPrefix (an
        // engineVersion), spaces, tabs, CRs and LFs around it are ignored and "VertexForge " may come
        // first; otherwise (an assetFormatVersion) the text must be exactly the version. These are the
        // launcher's VersionNumber.parseProduct / parse rules, so both read the same texts alike.
        // Empty for anything else. A part too large for 64 bits saturates: still a (very) new version.
        std::optional<VersionTriple> parseVersionTriple(std::string_view text, bool productPrefix)
        {
            if (productPrefix)
            {
                constexpr std::string_view blanks = " \t\r\n";
                const std::size_t first = text.find_first_not_of(blanks);
                if (first == std::string_view::npos)
                {
                    return std::nullopt;
                }
                text = text.substr(first, text.find_last_not_of(blanks) - first + 1);
                constexpr std::string_view product = "VertexForge ";
                if (text.starts_with(product))
                {
                    text.remove_prefix(product.size());
                }
            }

            VersionTriple parts{};
            for (std::size_t part = 0; part < parts.size(); ++part)
            {
                const bool last = part + 1 == parts.size();
                const std::size_t end = last ? text.size() : text.find('.');
                if (end == std::string_view::npos || end == 0)
                {
                    return std::nullopt;
                }
                const std::string_view digits = text.substr(0, end);
                if (digits.size() > 1 && digits.front() == '0')
                {
                    return std::nullopt;
                }
                std::uint64_t value = 0;
                for (const char c : digits)
                {
                    if (c < '0' || c > '9')
                    {
                        return std::nullopt;
                    }
                    const auto digit = static_cast<std::uint64_t>(c - '0');
                    value = value > (UINT64_MAX - digit) / 10 ? UINT64_MAX : value * 10 + digit;
                }
                parts[part] = value;
                text.remove_prefix(last ? end : end + 1);
            }
            return parts;
        }

        // VK-1690: what a member that records an engine should hold: the stored text when it names a
        // NEWER version than this engine's, else this engine's value. Absent or unparseable counts as
        // older. So neither the open stamp nor a save ever lowers the record.
        std::string newerOf(const std::optional<std::string>& stored, const std::string& mine,
                            const VersionTriple& mineVersion, bool productPrefix)
        {
            if (stored.has_value())
            {
                const std::optional<VersionTriple> storedVersion = parseVersionTriple(*stored, productPrefix);
                if (storedVersion.has_value() && *storedVersion > mineVersion)
                {
                    return *stored;
                }
            }
            return mine;
        }

        // This engine's MAJOR.MINOR.PATCH, read from Version::text (the generated "1.0.0"): the engine
        // Version's numbers are identity only and are never read outside the version tests
        // (test_asset_format_version). test_engine_version pins that the text is a valid triple.
        VersionTriple engineVersionTriple()
        {
            static const VersionTriple triple = parseVersionTriple(Version::text, false).value_or(VersionTriple{});
            return triple;
        }

        constexpr VersionTriple assetFormatVersionTriple()
        {
            return {AssetFormatVersion::major, AssetFormatVersion::minor, AssetFormatVersion::patch};
        }

        // The string member of the file at `filename`, read straight from disk; empty when the file,
        // the JSON or the member is missing or unreadable.
        std::optional<std::string> storedStringMember(std::string_view filename, const char* key)
        {
            try
            {
                std::ifstream in(std::filesystem::path{std::string(filename)}, std::ios::binary);
                if (!in.is_open())
                {
                    return std::nullopt;
                }
                const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                const json document = json::parse(text, nullptr, false);
                if (document.is_discarded() || !document.is_object())
                {
                    return std::nullopt;
                }
                const auto it = document.find(key);
                if (it == document.end() || !it->is_string())
                {
                    return std::nullopt;
                }
                return it->get<std::string>();
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
    }

    ProjectInspection ProjectSerialization::inspectProject(std::string_view filename)
    {
        ProjectInspection inspection;
        auto fail = [&inspection](ProjectInspectionStatus status, std::string message)
        {
            inspection.status = status;
            inspection.message = std::move(message);
            inspection.config.reset();
            return inspection;
        };

        try
        {
            std::string filePath{filename};
            json projectJson;
            try
            {
                projectJson = readSerializationJsonFile(filePath);
            }
            catch (const json::parse_error& e)
            {
                return fail(ProjectInspectionStatus::ParseError, std::string("JSON parse error: ") + e.what());
            }
            if (projectJson.is_null())
            {
                return fail(ProjectInspectionStatus::FileUnreadable, "the file is missing, empty or unreadable");
            }

            if (std::string error = structureError(projectJson); !error.empty())
            {
                return fail(ProjectInspectionStatus::InvalidStructure, std::move(error));
            }

            config::ProjectFileVersion fileVersion;
            std::string versionStr = projectJson["schemaVersion"].get<std::string>();
            if (!parseSchemaVersion(versionStr, fileVersion))
            {
                return fail(ProjectInspectionStatus::InvalidSchemaVersion,
                            "Invalid schema version format: '" + versionStr + "' (expected MAJOR.MINOR)");
            }

            if (fileVersion.major > config::ProjectSchemaVersion::major)
            {
                return fail(ProjectInspectionStatus::SchemaTooNew,
                            "Project schema version " + fileVersion.toString() +
                                " is newer than this engine supports (" + config::ProjectSchemaVersion::toString() +
                                "); open it with a newer VertexForge");
            }
            if (!fileVersion.isCompatible())
            {
                return fail(ProjectInspectionStatus::SchemaTooOld,
                            "Project schema version " + fileVersion.toString() +
                                " is older than this engine supports (" + config::ProjectSchemaVersion::toString() + ")");
            }

            config::ProjectConfig project;
            project.schemaVersion = fileVersion;
            project.projectName = projectJson["projectName"].get<std::string>();
            project.version = projectJson["version"].get<std::string>();
            project.workingDirectory = projectJson["workingDirectory"].get<std::string>();
            project.startupScene = projectJson["startupScene"].get<std::string>();

            if (projectJson.contains("exeIconPath") && projectJson["exeIconPath"].is_string())
            {
                project.exeIconPath = projectJson["exeIconPath"].get<std::string>();
            }

            if (projectJson.contains("inputMapping") && projectJson["inputMapping"].is_string())
            {
                project.inputMapping = projectJson["inputMapping"].get<std::string>();
            }

            if (projectJson.contains("engineVersion") && projectJson["engineVersion"].is_string())
            {
                project.engineVersion = projectJson["engineVersion"].get<std::string>();
            }

            if (projectJson.contains("lastModified") && projectJson["lastModified"].is_string())
            {
                project.lastModified = projectJson["lastModified"].get<std::string>();
            }

            if (projectJson.contains("pluginApiVersion") && projectJson["pluginApiVersion"].is_number_unsigned())
            {
                project.pluginApiVersion = projectJson["pluginApiVersion"].get<uint32_t>();
            }

            // VK-1726: the publisher (Project Settings) and, in a shipped .vfproj, where the game keeps its
            // state. The Runtime reads both again before its log opens (launch::readGameIdentity, same keys).
            if (const auto it = projectJson.find(std::string(launch::projectPublisherKey)); it != projectJson.end())
            {
                if (it->is_string())
                {
                    project.publisher = it->get<std::string>();
                }
                else
                {
                    inspection.warnings.push_back("Ignoring invalid 'publisher' in project file: expected a string");
                }
            }

            if (const auto it = projectJson.find(std::string(launch::projectStateLocationKey)); it != projectJson.end())
            {
                if (it->is_string())
                {
                    project.stateLocation = it->get<std::string>();
                }
                else
                {
                    inspection.warnings.push_back(
                        "Ignoring invalid 'stateLocation' in project file: expected a string");
                }
            }

            if (projectJson.contains("fontFallbackChain"))
            {
                const auto& fallbackJson = projectJson["fontFallbackChain"];
                if (!fallbackJson.is_array())
                {
                    inspection.warnings.push_back(
                        "Ignoring invalid 'fontFallbackChain' in project file: expected an array");
                }
                else
                {
                    for (const auto& entry : fallbackJson)
                    {
                        if (!entry.is_string())
                        {
                            inspection.warnings.push_back("Ignoring non-string entry in project 'fontFallbackChain'");
                            continue;
                        }
                        if (project.fontFallbackChain.size() >= config::ProjectConfig::maxFontFallbacks)
                        {
                            inspection.warnings.push_back("Ignoring project font fallback beyond the maximum of " +
                                                          std::to_string(config::ProjectConfig::maxFontFallbacks));
                            break;
                        }
                        project.fontFallbackChain.push_back(entry.get<std::string>());
                    }
                }
            }

            inspection.status = ProjectInspectionStatus::Ok;
            inspection.message.clear();
            inspection.config = std::move(project);
            return inspection;
        }
        catch (const std::exception& e)
        {
            return fail(ProjectInspectionStatus::FileUnreadable, std::string("the file could not be read: ") + e.what());
        }
    }

    std::optional<config::ProjectConfig> ProjectSerialization::loadProject(std::string_view filename)
    {
        ProjectInspection inspection = inspectProject(filename);
        for (const std::string& warning : inspection.warnings)
        {
            vfLogWarning("{}", warning);
        }

        if (!inspection.ok())
        {
            vfLogError("Failed to load project file '{}': {}", filename, inspection.message);
            return std::nullopt;
        }

        vfLogInfo("Project loaded successfully: {}", inspection.config->projectName);
        return std::move(inspection.config);
    }

    bool ProjectSerialization::saveProject(const config::ProjectConfig& project, std::string_view filename)
    {
        try
        {
            if (!project.isValid())
            {
                vfLogError("Cannot save project: required fields are missing");
                return false;
            }

            json projectJson;
            projectJson["schemaVersion"] = config::ProjectSchemaVersion::toString();
            projectJson["projectName"] = project.projectName;
            projectJson["version"] = project.version;
            projectJson["workingDirectory"] = project.workingDirectory;
            projectJson["startupScene"] = project.startupScene;

            if (!project.exeIconPath.empty())
            {
                projectJson["exeIconPath"] = project.exeIconPath;
            }

            if (project.inputMapping.has_value() && !project.inputMapping->empty())
            {
                projectJson["inputMapping"] = *project.inputMapping;
            }

            // VK-1690: never lower what the file records (the open stamp keeps a newer engine's value
            // in project.engineVersion; the asset format is read from the file being replaced).
            projectJson["engineVersion"] = newerOf(project.engineVersion, Version::displayName,
                                                   engineVersionTriple(), true);
            projectJson["assetFormatVersion"] = newerOf(storedStringMember(filename, "assetFormatVersion"),
                                                        AssetFormatVersion::toString(),
                                                        assetFormatVersionTriple(), false);
            projectJson["lastModified"] = getCurrentTimestamp();

            if (project.pluginApiVersion.has_value())
            {
                projectJson["pluginApiVersion"] = *project.pluginApiVersion;
            }

            if (!project.publisher.empty())
            {
                projectJson[std::string(launch::projectPublisherKey)] = project.publisher;
            }

            if (project.stateLocation.has_value())
            {
                projectJson[std::string(launch::projectStateLocationKey)] = *project.stateLocation;
            }

            if (!project.fontFallbackChain.empty())
            {
                projectJson["fontFallbackChain"] = json::array();
                const size_t fallbackCount =
                    std::min(project.fontFallbackChain.size(), config::ProjectConfig::maxFontFallbacks);
                for (size_t i = 0; i < fallbackCount; ++i)
                {
                    projectJson["fontFallbackChain"].push_back(project.fontFallbackChain[i]);
                }
                if (project.fontFallbackChain.size() > fallbackCount)
                {
                    vfLogWarning("Saving only the first {} project font fallbacks",
                                 config::ProjectConfig::maxFontFallbacks);
                }
            }

            std::string filePath{filename};
            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open project file for writing: {}", filename);
                return false;
            }

            file << projectJson.dump(2);
            file.close();

            vfLogInfo("Project saved successfully: {}", filename);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save project file '{}': {}", filename, e.what());
            return false;
        }
    }

    ProjectStamp ProjectSerialization::stampEngineVersions(std::string_view filename)
    {
        namespace fs = std::filesystem;
        ProjectStamp stamp;
        fs::path tempPath;

        // AtomicFileReplace.hpp promises that a failed replace removes the temp file, but
        // replaceFileAtomically leaves it in place (AtomicFileReplace.cpp): remove it here.
        const auto removeTemp = [&tempPath]
        {
            if (!tempPath.empty())
            {
                std::error_code removeError;
                fs::remove(tempPath, removeError);
            }
        };
        const auto fail = [&stamp, &removeTemp](std::string message)
        {
            removeTemp();
            stamp.status = ProjectStampStatus::Failed;
            stamp.message = std::move(message);
            return stamp;
        };

        try
        {
            // The process code page is UTF-8 (VK-1684), so the narrow name converts as UTF-8. Read
            // straight from disk: the serialization file bridge may serve a packaged copy.
            const fs::path finalPath{std::string(filename)};
            tempPath = finalPath;
            tempPath += ".stamp.tmp";

            std::string original;
            {
                std::ifstream in(finalPath, std::ios::binary);
                if (!in.is_open())
                {
                    return fail("the file could not be opened");
                }
                original.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                if (in.bad())
                {
                    return fail("the file could not be read");
                }
            }

            nlohmann::ordered_json document = nlohmann::ordered_json::parse(original, nullptr, false);
            if (document.is_discarded())
            {
                return fail("the file is not valid JSON");
            }
            if (!document.is_object())
            {
                return fail("the file's root is not a JSON object");
            }

            const auto storedString = [&document](const char* key) -> std::optional<std::string>
            {
                const auto it = document.find(key);
                if (it == document.end() || !it->is_string())
                {
                    return std::nullopt;
                }
                return it->get<std::string>();
            };
            // A newer engine's record is kept: never lowered.
            const std::string engineVersion = newerOf(storedString("engineVersion"), Version::displayName,
                                                      engineVersionTriple(), true);
            const std::string assetFormatVersion = newerOf(storedString("assetFormatVersion"),
                                                           AssetFormatVersion::toString(),
                                                           assetFormatVersionTriple(), false);
            const auto holds = [&storedString](const char* key, const std::string& expected)
            {
                const std::optional<std::string> stored = storedString(key);
                return stored.has_value() && *stored == expected;
            };
            if (holds("engineVersion", engineVersion) && holds("assetFormatVersion", assetFormatVersion))
            {
                stamp.engineVersion = engineVersion;
                stamp.assetFormatVersion = assetFormatVersion;
                return stamp;
            }

            // Fail fast on a read-only file (its folder is usually writable, so only the replace
            // would fail, logging an error on every try). MSVC maps FILE_ATTRIBUTE_READONLY to "no
            // write bits".
            std::error_code statusError;
            const fs::file_status fileStatus = fs::status(finalPath, statusError);
            if (!statusError && (fileStatus.permissions() & fs::perms::owner_write) == fs::perms::none)
            {
                return fail("the file is read-only");
            }

            // ordered_json cannot insert at a position, so the object is rebuilt in file order.
            const bool hadAssetFormatVersion = document.contains("assetFormatVersion");
            bool wroteEngineVersion = false;
            nlohmann::ordered_json stamped = nlohmann::ordered_json::object();
            for (auto it = document.begin(); it != document.end(); ++it)
            {
                const std::string& key = it.key();
                if (key == "engineVersion")
                {
                    stamped[key] = engineVersion;
                    wroteEngineVersion = true;
                    if (!hadAssetFormatVersion)
                    {
                        stamped["assetFormatVersion"] = assetFormatVersion;
                    }
                }
                else if (key == "assetFormatVersion")
                {
                    stamped[key] = assetFormatVersion;
                }
                else
                {
                    stamped[key] = std::move(it.value());
                }
            }
            if (!wroteEngineVersion)
            {
                stamped["engineVersion"] = engineVersion;
                // Appended after it, or set in place when the file already had one.
                stamped["assetFormatVersion"] = assetFormatVersion;
            }

            // dump() escapes CR and LF inside strings, so every '\n' it emits is a line break.
            std::string text = stamped.dump(2);
            if (!original.empty() && original.back() == '\n')
            {
                text += '\n';
            }
            if (original.find("\r\n") != std::string::npos)
            {
                std::string crlf;
                crlf.reserve(text.size() + text.size() / 8);
                for (const char c : text)
                {
                    if (c == '\n')
                    {
                        crlf += '\r';
                    }
                    crlf += c;
                }
                text = std::move(crlf);
            }
            constexpr std::string_view utf8Bom = "\xEF\xBB\xBF";
            if (std::string_view(original).starts_with(utf8Bom))
            {
                text.insert(0, utf8Bom);
            }

            // A reader holding the file open (the launcher checking its project list) blocks the
            // replace for a moment: retry, writing the temp file afresh each time.
            std::string lastError;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                if (attempt > 0)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                bool written = false;
                {
                    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                    if (out.is_open())
                    {
                        out.write(text.data(), static_cast<std::streamsize>(text.size()));
                        out.close();
                        written = !out.fail();
                    }
                }
                if (!written)
                {
                    lastError = "the temporary file beside it could not be written";
                    removeTemp();
                    continue;
                }

                if (::resource::replaceFileAtomically(tempPath, finalPath))
                {
                    stamp.status = ProjectStampStatus::Stamped;
                    stamp.engineVersion = engineVersion;
                    stamp.assetFormatVersion = assetFormatVersion;
                    return stamp;
                }
                lastError = "the file could not be replaced";
                removeTemp();
            }
            return fail(lastError);
        }
        catch (const std::exception& e)
        {
            return fail(std::string("the file could not be updated: ") + e.what());
        }
        catch (...)
        {
            return fail("the file could not be updated");
        }
    }

    bool ProjectSerialization::validateProject(std::string_view filename)
    {
        try
        {
            std::string filePath{filename};
            json projectJson = readSerializationJsonFile(filePath);
            if (projectJson.is_null())
            {
                return false;
            }

            return structureError(projectJson).empty();
        }
        catch (...)
        {
            return false;
        }
    }

    std::string ProjectSerialization::structureError(const json& projectJson)
    {
        if (!projectJson.is_object())
        {
            return "Invalid project file: root is not a JSON object";
        }

        for (const char* field : {"schemaVersion", "projectName", "version", "workingDirectory", "startupScene"})
        {
            if (!projectJson.contains(field) || !projectJson[field].is_string())
            {
                return std::string("Invalid project file: missing or invalid '") + field + "' field";
            }
        }

        for (const char* field : {"projectName", "version", "workingDirectory", "startupScene"})
        {
            if (projectJson[field].get<std::string>().empty())
            {
                return std::string("Invalid project file: '") + field + "' cannot be empty";
            }
        }

        return {};
    }

    bool ProjectSerialization::parseSchemaVersion(const std::string& versionStr, config::ProjectFileVersion& outVersion)
    {
        // Decimal digits on both sides of exactly one dot. std::stoul used to accept "-1" (wrapping
        // to 0xFFFFFFFF, i.e. "newer than any engine"), leading blanks and trailing junk. A number
        // too large for 32 bits is still a (very) new version, so it saturates.
        const auto parsePart = [](std::string_view part, uint32_t& out)
        {
            if (part.empty())
            {
                return false;
            }
            for (char c : part)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
            }
            const char* last = part.data() + part.size();
            auto [ptr, ec] = std::from_chars(part.data(), last, out);
            if (ec == std::errc::result_out_of_range)
            {
                out = UINT32_MAX;
                return true;
            }
            return ec == std::errc{} && ptr == last;
        };

        const size_t dotPos = versionStr.find('.');
        if (dotPos == std::string::npos)
        {
            return false;
        }

        const std::string_view text(versionStr);
        config::ProjectFileVersion parsed;
        if (!parsePart(text.substr(0, dotPos), parsed.major) || !parsePart(text.substr(dotPos + 1), parsed.minor))
        {
            return false;
        }
        outVersion = parsed;
        return true;
    }

    std::string ProjectSerialization::getCurrentTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }
}
