#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "tools/ExportTracker.hpp"
#include "tools/PathSandbox.hpp"

#include "events/EventDispatcher.hpp"
#include "events/project/ExportEvents.hpp"
#include "events/project/ProjectEvents.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using events::gameExport::ExportCompletedNotification;
    using events::gameExport::ExportGameCommand;
    using events::gameExport::ExportProgressNotification;
    using events::gameExport::ExportStartedNotification;
    using mcp::tools::ExportTracker;

    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }
    };

    fs::path makeTestDirectory(const char* name)
    {
        auto directory = fs::temp_directory_path() / name;
        fs::remove_all(directory);
        fs::create_directories(directory);
        return fs::weakly_canonical(directory);
    }

    void publishStarted(const std::string& outputDirectory)
    {
        ExportStartedNotification notification;
        notification.outputDirectory = outputDirectory;
        events::EventDispatcher::instance().publish(notification);
    }

    void publishProgress(float progress, const std::string& step)
    {
        ExportProgressNotification notification;
        notification.progress = progress;
        notification.currentStep = step;
        events::EventDispatcher::instance().publish(notification);
    }

    void publishCompleted(bool success, const std::string& error, std::vector<std::string> warnings = {},
                          const std::string& outputPath = {})
    {
        ExportCompletedNotification notification;
        notification.success = success;
        notification.errorMessage = error;
        notification.warnings = std::move(warnings);
        notification.outputPath = outputPath;
        events::EventDispatcher::instance().publish(notification);
    }

    // Fake project on disk: <root>/Game.vfproject with the asset root <root>/Assets.
    // The fake ExportGameCommand handler runs `behaviour` (default: refuse).
    struct ExportFixture
    {
        DispatcherScope dispatcherScope;
        fs::path projectRoot;
        fs::path assetsRoot;
        int executeCount = 0;
        std::optional<ExportGameCommand> lastCommand;
        std::function<bool(const ExportGameCommand&)> behaviour = [](const ExportGameCommand&) { return false; };
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        ExportFixture()
        {
            projectRoot = makeTestDirectory("VertexForge_McpExport_Project");
            assetsRoot = projectRoot / "Assets";
            fs::create_directories(assetsRoot);

            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig project;
                    project.projectName = "Game";
                    project.version = "1.0";
                    project.workingDirectory = assetsRoot.string();
                    project.startupScene = "scenes/Main.vfscene";
                    return project;
                });
            dispatcher.registerQueryHandler<events::project::GetProjectPathQuery>(
                [this](const events::project::GetProjectPathQuery&) -> std::optional<std::string>
                {
                    return (projectRoot / "Game.vfproject").string();
                });
            dispatcher.registerCommandHandler<ExportGameCommand>(
                [this](const ExportGameCommand& command) -> bool
                {
                    ++executeCount;
                    lastCommand = command;
                    return behaviour(command);
                });

            mcp::tools::registerExportTools(registry, mcp::tools::ToolContext{queue});
        }

        const mcp::ToolDef& tool(const char* name) const
        {
            auto found = registry.find(name);
            REQUIRE(found != nullptr);
            return *found;
        }

        // Worker-affinity tools block on runOnMain; play the editor main thread here.
        mcp::ToolResult callWorker(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Worker);
            auto future = std::async(std::launch::async, [&def, args]() { return def.handler(args); });
            while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            {
                queue.drain(std::chrono::milliseconds(8));
            }
            return future.get();
        }

        nlohmann::json status()
        {
            mcp::ToolResult result = callWorker("game_export_status", nlohmann::json::object());
            REQUIRE_FALSE(result.isError);
            return result.structured;
        }
    };

    std::string utf8(const fs::path& path)
    {
        return mcp::tools::pathToUtf8(path);
    }
}

TEST_SUITE("MCP export tools")
{
    TEST_CASE("ExportTracker follows a successful export through its notifications")
    {
        DispatcherScope scope;
        auto tracker = ExportTracker::create();

        CHECK(tracker->snapshot().state == ExportTracker::State::Idle);
        CHECK(tracker->snapshot().startCount == 0);

        publishStarted("C:/Out");
        ExportTracker::Snapshot snapshot = tracker->snapshot();
        CHECK(snapshot.state == ExportTracker::State::Preparing);
        CHECK(snapshot.startCount == 1);
        CHECK(snapshot.outputDirectory == "C:/Out");
        CHECK(snapshot.progress == doctest::Approx(0.0f));

        publishProgress(0.4f, "Packing assets");
        snapshot = tracker->snapshot();
        CHECK(snapshot.state == ExportTracker::State::Exporting);
        CHECK(snapshot.progress == doctest::Approx(0.4f));
        CHECK(snapshot.step == "Packing assets");

        publishCompleted(true, "", {"missing icon"}, "C:/Out");
        snapshot = tracker->snapshot();
        CHECK(snapshot.state == ExportTracker::State::Done);
        CHECK(snapshot.success);
        CHECK(snapshot.error.empty());
        REQUIRE(snapshot.warnings.size() == 1);
        CHECK(snapshot.warnings[0] == "missing icon");
        CHECK(snapshot.outputPath == "C:/Out");
        CHECK(snapshot.progress == doctest::Approx(1.0f));

        // A new export clears the previous run's result.
        publishStarted("C:/Out2");
        snapshot = tracker->snapshot();
        CHECK(snapshot.state == ExportTracker::State::Preparing);
        CHECK(snapshot.startCount == 2);
        CHECK(snapshot.outputDirectory == "C:/Out2");
        CHECK(snapshot.warnings.empty());
        CHECK_FALSE(snapshot.success);
        CHECK(snapshot.progress == doctest::Approx(0.0f));
    }

    TEST_CASE("ExportTracker records a failure before the export thread starts")
    {
        DispatcherScope scope;
        auto tracker = ExportTracker::create();

        // ExportHandler::publishFailure: Started, then Completed(false) with no progress.
        publishStarted("C:/Out");
        publishCompleted(false, "Script build failed", {}, "C:/Out");

        const ExportTracker::Snapshot snapshot = tracker->snapshot();
        CHECK(snapshot.state == ExportTracker::State::Done);
        CHECK_FALSE(snapshot.success);
        CHECK(snapshot.error == "Script build failed");
        CHECK(snapshot.progress == doctest::Approx(0.0f));
    }

    TEST_CASE("Notifications after the tracker is destroyed are harmless")
    {
        DispatcherScope scope;
        auto tracker = ExportTracker::create();
        std::weak_ptr<ExportTracker> weak = tracker;
        tracker.reset();
        REQUIRE(weak.expired());

        publishStarted("C:/Out");
        publishProgress(0.5f, "step");
        publishCompleted(false, "late");
        CHECK(weak.expired());
    }

    TEST_CASE("A callback copied before the tracker died is a no-op")
    {
        DispatcherScope scope;
        std::shared_ptr<ExportTracker> tracker;

        // Subscribed first, so publish() has already copied the tracker's callback
        // when this one destroys the tracker - the export-thread race in miniature.
        auto& dispatcher = events::EventDispatcher::instance();
        events::ScopedSubscription killer(dispatcher.subscribe<ExportCompletedNotification>(
            [&tracker](const ExportCompletedNotification&) { tracker.reset(); }));

        tracker = ExportTracker::create();
        std::weak_ptr<ExportTracker> weak = tracker;

        publishCompleted(true, "");
        CHECK(weak.expired());
    }

    TEST_CASE("game_export_status reports the tracker state")
    {
        ExportFixture fixture;

        const mcp::ToolDef& def = fixture.tool("game_export_status");
        CHECK(def.readOnly);

        nlohmann::json idle = fixture.status();
        CHECK(idle["state"] == "idle");
        CHECK(idle["progress"].get<double>() == doctest::Approx(0.0));
        CHECK(idle["step"] == "");
        CHECK_FALSE(idle.contains("success"));
        CHECK_FALSE(idle.contains("outputDirectory"));

        const std::string out = utf8(fixture.projectRoot / "Build");
        publishStarted(out);
        nlohmann::json preparing = fixture.status();
        CHECK(preparing["state"] == "preparing");
        CHECK(preparing["outputDirectory"] == out);
        CHECK_FALSE(preparing.contains("success"));

        publishProgress(0.25f, "Compiling shaders");
        nlohmann::json exporting = fixture.status();
        CHECK(exporting["state"] == "exporting");
        CHECK(exporting["progress"].get<double>() == doctest::Approx(0.25));
        CHECK(exporting["step"] == "Compiling shaders");

        publishCompleted(false, "disk full", {"w1", "w2"}, out);
        nlohmann::json done = fixture.status();
        CHECK(done["state"] == "done");
        CHECK(done["success"] == false);
        CHECK(done["error"] == "disk full");
        CHECK(done["warnings"] == nlohmann::json::array({"w1", "w2"}));
        CHECK(done["outputPath"] == out);
    }

    TEST_CASE("game_export validates outputDirectory")
    {
        ExportFixture fixture;

        const mcp::ToolDef& def = fixture.tool("game_export");
        CHECK(def.destructive);
        CHECK(def.affinity == mcp::ThreadAffinity::Worker);

        CHECK_THROWS_AS(fixture.callWorker("game_export", {{"outputDirectory", "relative/Build"}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("game_export", {{"outputDirectory", ""}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("game_export", nlohmann::json::object()), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("game_export", {{"outputDirectory", utf8(fixture.projectRoot / "Build")},
                                                           {"alwaysIncludePatterns", "*.png"}}),
                        mcp::ArgError);
        CHECK(fixture.executeCount == 0);
    }

    TEST_CASE("game_export refuses a clean build into the project or a parent of it")
    {
        ExportFixture fixture;

        SUBCASE("project directory")
        {
            mcp::ToolResult result = fixture.callWorker("game_export", {
                {"outputDirectory", utf8(fixture.projectRoot)}, {"cleanBuild", true}});
            CHECK(result.isError);
            CHECK(result.text.find("cleanBuild refused") != std::string::npos);
        }
        SUBCASE("asset root")
        {
            mcp::ToolResult result = fixture.callWorker("game_export", {
                {"outputDirectory", utf8(fixture.assetsRoot)}, {"cleanBuild", true}});
            CHECK(result.isError);
        }
        SUBCASE("parent of the project directory")
        {
            mcp::ToolResult result = fixture.callWorker("game_export", {
                {"outputDirectory", utf8(fixture.projectRoot.parent_path())}, {"cleanBuild", true}});
            CHECK(result.isError);
        }
        SUBCASE("dotted path resolving to the project directory")
        {
            mcp::ToolResult result = fixture.callWorker("game_export", {
                {"outputDirectory", utf8(fixture.projectRoot / "Assets" / "..")}, {"cleanBuild", true}});
            CHECK(result.isError);
        }
        CHECK(fixture.executeCount == 0);
    }

    TEST_CASE("game_export allows a non-clean build into the project directory and a clean build beside it")
    {
        ExportFixture fixture;
        fixture.behaviour = [](const ExportGameCommand&) { return true; };

        mcp::ToolResult inside = fixture.callWorker("game_export", {{"outputDirectory", utf8(fixture.projectRoot)}});
        CHECK_FALSE(inside.isError);

        const fs::path build = fixture.projectRoot / "Build";
        mcp::ToolResult clean = fixture.callWorker("game_export", {
            {"outputDirectory", utf8(build)}, {"cleanBuild", true}});
        CHECK_FALSE(clean.isError);
        CHECK(fixture.executeCount == 2);
    }

    TEST_CASE("game_export maps its arguments onto ExportGameCommand")
    {
        ExportFixture fixture;
        fixture.behaviour = [](const ExportGameCommand& command)
        {
            publishStarted(command.outputDirectory);
            return true;
        };

        const fs::path build = fixture.projectRoot / "Build";

        SUBCASE("defaults")
        {
            mcp::ToolResult result = fixture.callWorker("game_export", {{"outputDirectory", utf8(build)}});
            REQUIRE_FALSE(result.isError);
            CHECK(result.structured["started"] == true);
            CHECK(result.structured["outputDirectory"] == utf8(build));
            CHECK(result.structured.contains("hint"));

            REQUIRE(fixture.lastCommand.has_value());
            CHECK(fs::path(fixture.lastCommand->outputDirectory) == build);
            CHECK_FALSE(fixture.lastCommand->cleanBuild);
            CHECK(fixture.lastCommand->verifyIntegrity);
            CHECK(fixture.lastCommand->buildScripts);
            CHECK_FALSE(fixture.lastCommand->stripUnreferencedAssets);
            CHECK(fixture.lastCommand->alwaysIncludePatterns.empty());

            CHECK(fixture.status()["state"] == "preparing");
        }
        SUBCASE("explicit")
        {
            mcp::ToolResult result = fixture.callWorker("game_export", {
                {"outputDirectory", utf8(build)},
                {"cleanBuild", true},
                {"verifyIntegrity", false},
                {"buildScripts", false},
                {"stripUnreferencedAssets", true},
                {"alwaysIncludePatterns", {"textures/ui/**", "*.vfFont"}}
            });
            REQUIRE_FALSE(result.isError);

            REQUIRE(fixture.lastCommand.has_value());
            CHECK(fixture.lastCommand->cleanBuild);
            CHECK_FALSE(fixture.lastCommand->verifyIntegrity);
            CHECK_FALSE(fixture.lastCommand->buildScripts);
            CHECK(fixture.lastCommand->stripUnreferencedAssets);
            CHECK(fixture.lastCommand->alwaysIncludePatterns == std::vector<std::string>{"textures/ui/**", "*.vfFont"});
        }
    }

    TEST_CASE("game_export reports a refused command")
    {
        ExportFixture fixture;

        mcp::ToolResult result = fixture.callWorker("game_export", {
            {"outputDirectory", utf8(fixture.projectRoot / "Build")}});
        CHECK(result.isError);
        CHECK(result.text == "export not started: an export is already running or no project is loaded");
        CHECK(fixture.executeCount == 1);
    }

    TEST_CASE("A refused command leaves a running export's status alone")
    {
        ExportFixture fixture;

        publishStarted("C:/Running");
        publishProgress(0.6f, "Packing");

        mcp::ToolResult result = fixture.callWorker("game_export", {
            {"outputDirectory", utf8(fixture.projectRoot / "Build")}});
        CHECK(result.isError);

        nlohmann::json status = fixture.status();
        CHECK(status["state"] == "exporting");
        CHECK(status["progress"].get<double>() == doctest::Approx(0.6));
        CHECK(status["step"] == "Packing");
    }

    TEST_CASE("game_export returns a pre-thread failure's message")
    {
        ExportFixture fixture;
        fixture.behaviour = [](const ExportGameCommand& command)
        {
            // ExportHandler: Started, then publishFailure when the script build fails.
            publishStarted(command.outputDirectory);
            publishCompleted(false, "Script build failed", {}, command.outputDirectory);
            return false;
        };

        mcp::ToolResult result = fixture.callWorker("game_export", {
            {"outputDirectory", utf8(fixture.projectRoot / "Build")}});
        CHECK(result.isError);
        CHECK(result.text == "export failed before it started: Script build failed");

        nlohmann::json status = fixture.status();
        CHECK(status["state"] == "done");
        CHECK(status["success"] == false);
        CHECK(status["error"] == "Script build failed");
    }

    TEST_CASE("A stale failure does not masquerade as this call's failure")
    {
        ExportFixture fixture;

        // A previous export failed; this call is refused without publishing anything.
        publishStarted("C:/Old");
        publishCompleted(false, "old failure");

        mcp::ToolResult result = fixture.callWorker("game_export", {
            {"outputDirectory", utf8(fixture.projectRoot / "Build")}});
        CHECK(result.isError);
        CHECK(result.text == "export not started: an export is already running or no project is loaded");
    }
}
