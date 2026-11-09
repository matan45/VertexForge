#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"

#include "controllers/Import.hpp"
#include "events/EventDispatcher.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/project/ResourceEvents.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr int64_t maxListCap = 5000;
        constexpr std::size_t maxImportFiles = 64;

        // Main thread only. ProjectConfig::workingDirectory is the project's asset root.
        fs::path assetsRootOnMain()
        {
            auto project = events::EventDispatcher::instance().query(events::project::GetCurrentProjectQuery{});
            if (!project.has_value() || project->workingDirectory.empty())
            {
                throw std::runtime_error("No project is loaded; open one with project_open first");
            }
            // workingDirectory is built with path::string() (narrow) by ProjectServiceImpl.
            return fs::absolute(fs::path(project->workingDirectory));
        }

        std::string projectRelative(const fs::path& root, const fs::path& path)
        {
            std::error_code ec;
            fs::path relative = fs::relative(path, root, ec);
            if (ec || relative.empty() || *relative.begin() == "..")
            {
                return genericPathToUtf8(path);
            }
            return genericPathToUtf8(relative);
        }

        std::string lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::vector<std::string> assetTypeNames()
        {
            std::vector<std::string> names;
            for (uint8_t i = 0; i < static_cast<uint8_t>(resource::AssetType::COUNT); ++i)
            {
                const auto type = static_cast<resource::AssetType>(i);
                if (type == resource::AssetType::WorldSector)
                {
                    continue;  // scheduler-only tag, never in the asset database
                }
                names.emplace_back(resource::assetTypeName(type));
            }
            return names;
        }

        resource::AssetType parseAssetType(const std::string& name)
        {
            const std::string wanted = lowercase(name);
            for (uint8_t i = 0; i < static_cast<uint8_t>(resource::AssetType::COUNT); ++i)
            {
                const auto type = static_cast<resource::AssetType>(i);
                if (lowercase(resource::assetTypeName(type)) == wanted)
                {
                    return type;
                }
            }
            throw ArgError("unknown asset type '" + name + "'");
        }

        void registerAssetsList(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "assets_list";
            tool.title = "List assets";
            tool.description =
                "List assets registered in the project asset database (imported/engine assets: .vfMesh, "
                ".vfImage, .vfMat, .vfAnim (animation clips, type Animation), .vfAnimator, .vfVFX (effects, type VFX), "
                ".vfVFXSequence (effect combos, type VFXSequence), .vfScene, .vfPrefab, ...). "
                "Paths are relative to the project asset root and are the form the other project-path tools (e.g. "
                "material_assign) accept. For clips that fit a character's skeleton use animation_list_clips; for "
                "effects with their authored values vfx_list_assets. Files copied into the project outside the editor "
                "are listed after assets_rescan. Returns {assets:[{path, type, guid}], total, truncated}.";
            tool.inputSchema = schema::object({
                {"type", schema::enumString("Only this asset type. Omit for all.", assetTypeNames())},
                {"contains", schema::string("Case-insensitive substring filter on the path")},
                {"max", schema::integer("Maximum entries (1-5000). Default 500.")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string needle = lowercase(reader.optString("contains"));
                const int64_t max = reader.optInt("max", 500);
                if (max < 1 || max > maxListCap)
                {
                    throw ArgError("argument 'max' must be between 1 and 5000");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                std::vector<events::assetdb::AssetEntryData> entries;
                if (reader.has("type"))
                {
                    events::assetdb::GetAssetsByTypeQuery query;
                    query.type = parseAssetType(reader.requireString("type"));
                    entries = dispatcher.query(query);
                }
                else
                {
                    entries = dispatcher.query(events::assetdb::GetAllAssetsQuery{});
                }

                const fs::path root = assetsRootOnMain();
                std::vector<std::pair<std::string, const events::assetdb::AssetEntryData*>> matches;
                for (const events::assetdb::AssetEntryData& entry : entries)
                {
                    std::string path = projectRelative(root, fs::path(entry.path));
                    if (!needle.empty() && lowercase(path).find(needle) == std::string::npos)
                    {
                        continue;
                    }
                    matches.emplace_back(std::move(path), &entry);
                }
                std::sort(matches.begin(), matches.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });

                const std::size_t total = matches.size();
                nlohmann::json assets = nlohmann::json::array();
                for (std::size_t i = 0; i < total && i < static_cast<std::size_t>(max); ++i)
                {
                    const events::assetdb::AssetEntryData& entry = *matches[i].second;
                    assets.push_back({
                        {"path", matches[i].first},
                        {"type", resource::assetTypeName(entry.type)},
                        {"guid", entry.guid.toString()}
                    });
                }

                return ToolResult::ok({
                    {"assets", std::move(assets)},
                    {"total", total},
                    {"truncated", total > static_cast<std::size_t>(max)}
                });
            };
            registry.add(std::move(tool));
        }

        void registerAssetsImport(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "assets_import";
            tool.title = "Import assets";
            tool.description =
                "Import source files (any format the editor's Import dialog accepts: models, images, audio, "
                "fonts, ...) into the project with default import settings, like the editor's Import dialog. "
                "'files' are absolute paths anywhere on disk; 'targetDir' is a folder "
                "relative to the project asset root (created if missing). Blocks until the import finishes. "
                "A model with animations (FBX, GLB, DAE, ...) also writes one <model>_<take>.vfAnim clip per "
                "animation take next to its meshes, each listed as its own result with assetType Animation. "
                "Returns {results:[{source, output, assetType, success, error}], successCount, failureCount} "
                "with project-relative output paths.";
            tool.inputSchema = schema::object({
                {"files", schema::array(schema::string("Absolute source file path"), "Source files to import (max 64)")},
                {"targetDir", schema::string("Destination folder relative to the asset root, e.g. 'models/props'")}
            }, {"files", "targetDir"});
            tool.affinity = ThreadAffinity::Worker;
            tool.timeout = std::chrono::milliseconds(120000);
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::vector<std::string> files = reader.requireStringArray("files");
                const std::string targetDir = reader.requireString("targetDir");
                if (files.empty() || files.size() > maxImportFiles)
                {
                    throw ArgError("argument 'files' must hold 1-64 paths");
                }

                const fs::path root = pathFromUtf8(context.runOnMain([]() -> nlohmann::json
                {
                    return pathToUtf8(assetsRootOnMain());
                }).get<std::string>());

                std::string error;
                auto target = resolveInside(root, targetDir, error);
                if (!target)
                {
                    throw ArgError(error);
                }
                std::error_code ec;
                fs::create_directories(*target, ec);
                if (ec)
                {
                    return ToolResult::error("Failed to create '" + targetDir + "': " + ec.message());
                }

                // Sources may live anywhere but must be existing regular files. Engine
                // paths are narrow strings (the import dialog passes them the same way).
                std::vector<importConfig::ImportFiles> importFiles;
                std::vector<std::string> sourcePaths;
                for (const std::string& file : files)
                {
                    const fs::path source = pathFromUtf8(file);
                    if (!source.is_absolute())
                    {
                        throw ArgError("source '" + file + "' must be an absolute path");
                    }
                    if (!fs::is_regular_file(source, ec))
                    {
                        throw ArgError("source '" + file + "' does not exist or is not a file");
                    }
                    importFiles.emplace_back(source.string(), importConfig::ImportConfig{});
                    sourcePaths.push_back(source.string());
                }
                const std::string location = target->string();

                context.runOnMain([sourcePaths]() -> nlohmann::json
                {
                    events::resource::ImportStartedNotification notification;
                    notification.files = sourcePaths;
                    events::EventDispatcher::instance().publish(notification);
                    return nullptr;
                });

                controllers::ImportResult result;
                std::string importError;
                try
                {
                    controllers::Import::resetCancellation();

                    // Progress is published from the importing thread exactly like the
                    // editor's import dialog; ImportProgressWindow's handler is thread-safe.
                    auto progressCallback = [](std::string_view currentFile, uint32_t, uint32_t, float fileProgress)
                    {
                        events::resource::ImportProgressNotification notification;
                        notification.currentFile = std::string(currentFile);
                        notification.progress = fileProgress;
                        events::EventDispatcher::instance().publish(notification);
                    };
                    // Explicit target: the process-wide Import location tracks the
                    // content browser and must not be redirected by an agent import.
                    result = controllers::Import::importFilesInto(importFiles, location, progressCallback);
                }
                catch (const std::exception& e)
                {
                    importError = e.what();
                }

                // Always complete, so the content browser clears its in-flight state and
                // the asset database registers whatever was written.
                std::vector<services::ImportResult> completed;
                for (const controllers::ImportFileResult& fileResult : result.fileResults)
                {
                    services::ImportResult res;
                    res.sourcePath = fileResult.sourcePath;
                    res.outputPath = fileResult.outputPath;
                    // The importer types each output itself: a model's extracted clips are Animation, not Mesh.
                    res.assetType = fileResult.assetType;
                    res.success = fileResult.success;
                    res.errorMessage = fileResult.errorMessage;
                    completed.push_back(std::move(res));
                }
                bool notified = true;
                try
                {
                    context.runOnMain([completed]() -> nlohmann::json
                    {
                        events::resource::ImportCompletedNotification notification;
                        notification.results = completed;
                        events::EventDispatcher::instance().publish(notification);
                        return nullptr;
                    });
                }
                catch (const std::exception&)
                {
                    notified = false;  // timed out; the task still runs when the main thread frees up
                }

                if (!importError.empty())
                {
                    return ToolResult::error("Import failed: " + importError);
                }

                nlohmann::json results = nlohmann::json::array();
                for (const services::ImportResult& res : completed)
                {
                    nlohmann::json item{
                        {"source", res.sourcePath},
                        {"output", res.outputPath.empty() ? nlohmann::json(nullptr)
                                                          : nlohmann::json(projectRelative(root, fs::path(res.outputPath)))},
                        {"assetType", resource::assetTypeName(res.assetType)},
                        {"success", res.success}
                    };
                    if (!res.success)
                    {
                        item["error"] = res.errorMessage;
                    }
                    results.push_back(std::move(item));
                }

                nlohmann::json out{
                    {"results", std::move(results)},
                    {"successCount", result.successCount},
                    {"failureCount", result.failureCount}
                };
                if (!notified)
                {
                    out["warning"] = "editor main thread was busy; the asset browser may refresh late";
                }
                return ToolResult::ok(std::move(out));
            };
            registry.add(std::move(tool));
        }

        // VK-1657: the asset database only migrates an empty project when it loads, so files copied in
        // afterwards (the engine's VFX library) stay unknown until their .vfmeta sidecars are written.
        void registerAssetsRescan(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "assets_rescan";
            tool.title = "Rescan project assets";
            tool.description =
                "Register the project's asset files that have no .vfmeta sidecar yet, e.g. files copied into the "
                "project outside the editor (the engine's assets/vfx/library copied to <project>/assets/vfx/library): "
                "each gets a .vfmeta with a new GUID, so assets_list, vfx_list_assets and the asset fields find it. A "
                "file copied together with its .vfmeta is left alone (the database loads it at the next project_open). "
                "Not undoable (the .vfmeta files stay); refused in Play mode and while a scene load is pending. Returns "
                "{scanned (asset files found), metaFilesCreated, failures}.";
            tool.inputSchema = closedObject(nlohmann::json::object());
            tool.timeout = std::chrono::milliseconds(120000);
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {}, "assets_rescan");
                if (const std::optional<std::string> blocked = env::editBlocker("rescan the assets"))
                {
                    return ToolResult::error(*blocked);
                }
                const events::assetdb::RegenerateMetadataResult result =
                    events::EventDispatcher::instance().execute(events::assetdb::RegenerateMissingMetadataCommand{});

                nlohmann::json out{
                    {"scanned", result.assetsScanned},
                    {"metaFilesCreated", result.metaFilesCreated},
                    {"failures", result.failures}
                };
                std::string summary = "Scanned " + std::to_string(result.assetsScanned) + " asset file(s) and registered " +
                                      std::to_string(result.metaFilesCreated) + " new one(s)";
                summary += result.failures.empty() ? std::string(".")
                                                   : "; " + std::to_string(result.failures.size()) + " failure(s).";
                if (result.metaFilesCreated > 0)
                {
                    summary += " assets_list and vfx_list_assets list them now.";
                }
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }
    }

    void registerAssetTools(ToolRegistry& registry, const ToolContext& context)
    {
        registerAssetsList(registry);
        registerAssetsImport(registry, context);
        registerAssetsRescan(registry);
    }
}
