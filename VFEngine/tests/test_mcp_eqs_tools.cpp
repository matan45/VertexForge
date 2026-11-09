#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view and nlohmann::json through operator<<

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "tools/EQSToolSupport.hpp"

#include "data/DTOs.hpp"
#include "data/EQSData.hpp"
#include "eqs/EQSQuerySpec.hpp"
#include "eqs/EQSTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ai/EQSAuthoringEvents.hpp"
#include "events/ai/EQSEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "impl/ai/EQSAuthoringService.hpp"
#include "impl/ai/EQSServiceImpl.hpp"
#include "providers/ai/IEQSProvider.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// ============================================================
// VK-1660: the eqs_* MCP tools - eqs_list_queries (Worker), eqs_get_query / eqs_set_query over the real
// EQSServiceImpl + EQSAuthoringService on a temp project, and eqs_run's mapping of a run through fake
// handlers - plus the pure eqstool helpers.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;
    namespace eqstool = mcp::tools::eqstool;

    fs::path projectRoot()
    {
        return fs::temp_directory_path() / "vf_mcp_eqs_tools_tests";
    }

    fs::path templatesRoot()
    {
        return projectRoot() / "assets" / "eqs";
    }

    fs::path templatePath(const std::string& name)
    {
        return templatesRoot() / (name + ".vfEQSQuery");
    }

    std::string readAll(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    void writeAll(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    }

    bool contains(std::string_view text, std::string_view part)
    {
        return text.find(part) != std::string_view::npos;
    }

    // Lines of `text` that start with `prefix`.
    std::size_t countLines(const std::string& text, std::string_view prefix)
    {
        std::istringstream stream(text);
        std::size_t count = 0;
        for (std::string line; std::getline(stream, line);)
        {
            if (line.starts_with(prefix))
            {
                ++count;
            }
        }
        return count;
    }

    // Every param at its default: the nearest point of a ring around the querier.
    constexpr const char* kRingTemplate = R"({
        "description": "Nearest point of a ring",
        "generator": {"type": "Ring"},
        "tests": [{"type": "Distance", "score": {"mode": "InverseLinear"}}]
    })";

    // The registry, the main-thread queue and the mode / load flags every tool reads.
    struct ToolsFixture
    {
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        bool playing = false;
        bool loadPending = false;

        ToolsFixture()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.clear();
            std::error_code ec;
            fs::remove_all(projectRoot(), ec);
            fs::create_directories(templatesRoot(), ec);

            mcp::tools::registerEQSTools(registry, mcp::tools::ToolContext{queue});
            dispatcher.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playing; });
            dispatcher.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
        }

        ~ToolsFixture()
        {
            events::EventDispatcher::instance().clear();
            std::error_code ec;
            fs::remove_all(projectRoot(), ec);
        }

        ToolsFixture(const ToolsFixture&) = delete;
        ToolsFixture& operator=(const ToolsFixture&) = delete;

        static void registerProject(std::optional<fs::path> root)
        {
            events::EventDispatcher::instance().registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [root](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    if (!root.has_value())
                    {
                        return std::nullopt;
                    }
                    config::ProjectConfig config;
                    config.projectName = "McpEQSTools";
                    config.workingDirectory = root->string();
                    return config;
                });
        }

        const mcp::ToolDef& tool(const char* name) const
        {
            auto found = registry.find(name);
            REQUIRE(found.get() != nullptr);
            return *found; // the registry keeps it alive
        }

        mcp::ToolResult call(const char* name, const json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Main);
            return def.handler(args);
        }

        // Worker tools block on runOnMain; this thread plays the editor main thread.
        mcp::ToolResult callWorker(const char* name, const json& args)
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

        mcp::ToolResult invoke(const char* name, const json& args)
        {
            return tool(name).affinity == mcp::ThreadAffinity::Worker ? callWorker(name, args) : call(name, args);
        }

        // The message of the ArgError a call throws.
        std::string argError(const char* name, const json& args)
        {
            try
            {
                invoke(name, args);
            }
            catch (const mcp::ArgError& e)
            {
                return e.what();
            }
            catch (const std::exception& e)
            {
                return std::string("<not an ArgError: ") + e.what() + ">";
            }
            return "<no error>";
        }

        // The message of the engine failure (a std::runtime_error, never an ArgError) a call throws.
        std::string engineError(const char* name, const json& args)
        {
            try
            {
                invoke(name, args);
            }
            catch (const mcp::ArgError& e)
            {
                return std::string("<an ArgError: ") + e.what() + ">";
            }
            catch (const std::runtime_error& e)
            {
                return e.what();
            }
            catch (const std::exception& e)
            {
                return std::string("<not a runtime_error: ") + e.what() + ">";
            }
            return "<no error>";
        }
    };

    // The engine side of the template tools: what EQSAdapter::lookupQuery reports, over the temp folder.
    struct TemplateProvider : services::IEQSProvider
    {
        services::EQSQueryInfo lookupQuery(const std::string& queryName) override
        {
            services::EQSQueryInfo info;
            info.name = queryName;
            if (eqs::isValidQueryName(queryName))
            {
                info.path = templatePath(queryName).string();
            }
            std::error_code ec;
            info.exists = !info.path.empty() && fs::is_regular_file(fs::path(info.path), ec);
            if (info.exists)
            {
                eqs::EQSSpecParseResult parsed = eqs::parseQuerySpecText(readAll(fs::path(info.path)));
                info.valid = parsed.spec.has_value();
                info.errors = std::move(parsed.errors);
            }
            return info;
        }

        // Not used by the template tools.
        eqs::EQSQueryHandle submitQuery(const std::string&, const eqs::EQSContext&) override { return {}; }
        eqs::EQSResult getResult(eqs::EQSQueryHandle) const override { return {}; }
        void releaseQuery(eqs::EQSQueryHandle) override {}
        void releaseAll() override {}
        void update(float) override {}
        eqs::EQSDebugResult runQueryNow(const std::string&, const eqs::EQSContext&) override { return {}; }
        void invalidateQuery(const std::string&) override {}
    };

    // The real EQS service (listing, template info) and authoring service (load / save) on the temp project.
    struct TemplatesFixture : ToolsFixture
    {
        TemplateProvider provider;
        services::EQSServiceImpl eqsService{&provider, [] { return templatesRoot(); }};
        services::EQSAuthoringService authoring;

        TemplatesFixture()
        {
            eqsService.registerEventHandlers();
            authoring.registerEventHandlers();
            registerProject(projectRoot());
        }
    };

    constexpr uint64_t kQuerier = 10;
    constexpr uint64_t kTarget = 30;

    services::TransformData worldAt(glm::vec3 position, glm::vec3 rotation = glm::vec3(0.0f))
    {
        services::TransformData transform;
        transform.position = position;
        transform.rotation = rotation;
        return transform;
    }

    eqs::EQSDebugCandidate debugCandidate(glm::vec3 position, uint64_t entityId, int filteredByTest,
                                          std::vector<float> metrics, std::vector<float> scores, float total)
    {
        eqs::EQSDebugCandidate candidate;
        candidate.position = position;
        candidate.entityId = entityId;
        candidate.filteredByTest = filteredByTest;
        candidate.metrics = std::move(metrics);
        candidate.scores = std::move(scores);
        candidate.total = total;
        return candidate;
    }

    // eqs_run over fake handlers: entities, the querier's NavmeshAgent, the template's info and file, and a
    // RunEQSQueryNowQuery that records the context and answers `nextRun`.
    struct RunFixture : ToolsFixture
    {
        struct FakeEntity
        {
            std::string name;
            services::TransformData world;
        };

        std::map<uint64_t, FakeEntity> entities;
        std::optional<services::NavmeshAgentData> agent;
        services::EQSQueryInfo info;
        std::string templateText; // "" = the file cannot be read
        eqs::EQSDebugResult nextRun;
        std::vector<events::ai::RunEQSQueryNowQuery> runs;
        std::vector<events::ai::ShowEQSDebugCommand> shown;

        RunFixture()
        {
            info.path = "C:/project/assets/eqs/Hunt.vfEQSQuery";
            info.exists = true;
            info.valid = true;

            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.registerQueryHandler<events::scene::GetEntityQuery>(
                [this](const events::scene::GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    const auto it = entities.find(query.entity.id);
                    if (it == entities.end())
                    {
                        return std::nullopt;
                    }
                    services::EntityData data;
                    data.handle = query.entity;
                    data.name = it->second.name;
                    data.localTransform = it->second.world;
                    data.worldTransform = it->second.world;
                    return data;
                });
            dispatcher.registerQueryHandler<events::scene::GetNavmeshAgentDataQuery>(
                [this](const events::scene::GetNavmeshAgentDataQuery& query) -> std::optional<services::NavmeshAgentData>
                {
                    return query.entity.id == kQuerier ? agent : std::nullopt;
                });
            dispatcher.registerQueryHandler<events::ai::GetEQSQueryInfoQuery>(
                [this](const events::ai::GetEQSQueryInfoQuery& query)
                {
                    services::EQSQueryInfo answer = info;
                    answer.name = query.queryName;
                    return answer;
                });
            dispatcher.registerQueryHandler<events::ai::LoadEQSQueryAssetQuery>(
                [this](const events::ai::LoadEQSQueryAssetQuery&)
                {
                    services::EQSAssetLoadResult result;
                    if (templateText.empty())
                    {
                        result.error = "missing";
                        return result;
                    }
                    result.success = true;
                    result.text = templateText;
                    result.contentHash = 1;
                    return result;
                });
            dispatcher.registerQueryHandler<events::ai::RunEQSQueryNowQuery>(
                [this](const events::ai::RunEQSQueryNowQuery& query)
                {
                    runs.push_back(query);
                    return nextRun;
                });
            dispatcher.registerCommandHandler<events::ai::ShowEQSDebugCommand>(
                [this](const events::ai::ShowEQSDebugCommand& command)
                {
                    shown.push_back(command);
                });

            entities[kQuerier] = {"Guard", worldAt(glm::vec3(1.0f, 0.0f, 2.0f), glm::vec3(0.0f, 90.0f, 0.0f))};
            entities[kTarget] = {"Player", worldAt(glm::vec3(5.0f, 0.0f, 5.0f))};
        }
    };
}

TEST_SUITE("MCP EQS tools")
{
    TEST_CASE_FIXTURE(ToolsFixture, "the eqs tools register with their affinity, flags and closed schemas")
    {
        struct Expected
        {
            const char* name;
            mcp::ThreadAffinity affinity;
            bool readOnly;
            bool destructive;
            std::vector<std::string> required;
        };
        const std::vector<Expected> expected{
            {"eqs_list_queries", mcp::ThreadAffinity::Worker, true, false, {}},
            {"eqs_get_query", mcp::ThreadAffinity::Main, true, false, {"name"}},
            {"eqs_set_query", mcp::ThreadAffinity::Main, false, true, {"name", "query"}},
            {"eqs_run", mcp::ThreadAffinity::Main, true, false, {"name", "querier"}},
        };
        CHECK(registry.size() == expected.size());
        for (const Expected& entry : expected)
        {
            INFO(entry.name);
            const mcp::ToolDef& def = tool(entry.name);
            CHECK(def.affinity == entry.affinity);
            CHECK(def.readOnly == entry.readOnly);
            CHECK(def.destructive == entry.destructive);
            CHECK(def.inputSchema.at("additionalProperties") == false);
            const json required = def.inputSchema.contains("required") ? def.inputSchema.at("required") : json::array();
            CHECK(required == json(entry.required));
        }

        CHECK(tool("eqs_list_queries").timeout.count() == 10000);
        CHECK(tool("eqs_set_query").timeout.count() == 15000);
        CHECK(tool("eqs_get_query").inputSchema.at("properties").at("name").at("pattern") ==
              "^[A-Za-z][A-Za-z0-9_]{0,63}$");
        const json& run = tool("eqs_run").inputSchema.at("properties");
        CHECK(run.at("target").at("anyOf").size() == 2);
        CHECK(run.at("maxCandidates").at("minimum") == 1);
        CHECK(run.at("maxCandidates").at("maximum") == 200);
        CHECK(run.at("showSeconds").at("maximum") == 60.0);
    }

    TEST_CASE("eqstool content hashes are 16 hex digits and parse back")
    {
        CHECK(eqstool::hashToHex(0) == "0000000000000000");
        CHECK(eqstool::hashToHex(0xCBF29CE484222325ull) == "cbf29ce484222325");
        for (uint64_t value : {uint64_t{0}, uint64_t{1}, uint64_t{0x0123456789ABCDEFull}, ~uint64_t{0}})
        {
            const std::optional<uint64_t> parsed = eqstool::hexToHash(eqstool::hashToHex(value));
            REQUIRE(parsed.has_value());
            CHECK(*parsed == value);
        }

        const std::optional<uint64_t> upper = eqstool::hexToHash("CBF29CE484222325");
        REQUIRE(upper.has_value());
        CHECK(*upper == 0xCBF29CE484222325ull);
        const std::optional<uint64_t> shortHex = eqstool::hexToHash("ff");
        REQUIRE(shortHex.has_value());
        CHECK(*shortHex == 255u);

        for (const char* bad : {"", "0x12", "12 ", " 12", "-1", "+1", "xyz", "12g", "00000000000000001"})
        {
            INFO(bad);
            CHECK_FALSE(eqstool::hexToHash(bad).has_value());
        }
    }

    TEST_CASE("eqstool lists at most 20 spec errors and counts the rest")
    {
        std::vector<eqs::EQSSpecError> errors;
        for (int i = 0; i < 20; ++i)
        {
            errors.push_back({std::format("tests[{}]", i), "needs 'filter' or 'score'"});
        }
        CHECK(countLines(eqstool::specErrorLines(errors), "- ") == 20);
        CHECK_FALSE(contains(eqstool::specErrorLines(errors), "more"));
        CHECK(eqstool::specErrorsJson(errors).size() == 20);

        errors.push_back({"", "not valid JSON"});
        const std::string lines = eqstool::specErrorLines(errors);
        CHECK(countLines(lines, "- ") == 21);
        CHECK(contains(lines, "- ... and 1 more"));
        CHECK(eqstool::specErrorsJson(errors).size() == 20);
        CHECK(eqstool::specErrorText(errors.front()) == "tests[0]: needs 'filter' or 'score'");
        CHECK(eqstool::specErrorText(errors.back()) == "not valid JSON");
    }

    TEST_CASE("eqstool keys repeated test types apart and reads purposes only from a matching spec")
    {
        const std::vector<eqstool::RunTest> unknown = eqstool::runTests({"Trace", "Distance", "Trace"}, nullptr);
        REQUIRE(unknown.size() == 3);
        CHECK(unknown[0].key == "Trace#1");
        CHECK(unknown[1].key == "Distance");
        CHECK(unknown[2].key == "Trace#2");
        CHECK(unknown[2].type == "Trace");
        CHECK_FALSE(unknown[0].purpose.has_value());

        const eqs::EQSSpecParseResult parsed = eqs::parseQuerySpecText(R"({
            "generator": {"type": "Ring"},
            "tests": [{"type": "Trace", "filter": {}}, {"type": "Distance", "score": {}},
                      {"type": "Trace", "filter": {"max": 0}}]
        })");
        REQUIRE(parsed.spec.has_value());
        const std::vector<eqstool::RunTest> known = eqstool::runTests({"Trace", "Distance", "Trace"}, &*parsed.spec);
        REQUIRE(known.size() == 3);
        REQUIRE(known[0].purpose.has_value());
        CHECK(*known[0].purpose == eqs::EQSTestPurpose::Filter);
        REQUIRE(known[1].purpose.has_value());
        CHECK(*known[1].purpose == eqs::EQSTestPurpose::Score);
        REQUIRE(known[2].purpose.has_value());
        CHECK(*known[2].purpose == eqs::EQSTestPurpose::Filter);

        // Other types, or another count: the spec does not describe the run.
        for (const eqstool::RunTest& test : eqstool::runTests({"Distance", "Trace", "Trace"}, &*parsed.spec))
        {
            CHECK_FALSE(test.purpose.has_value());
        }
        for (const eqstool::RunTest& test : eqstool::runTests({"Trace", "Distance"}, &*parsed.spec))
        {
            CHECK_FALSE(test.purpose.has_value());
        }
    }

    TEST_CASE_FIXTURE(TemplatesFixture, "eqs_list_queries lists the project's templates with the catalogue")
    {
        writeAll(templatePath("NearestPoint"), kRingTemplate);
        writeAll(templatePath("Broken"), R"({"description": "A typo", "generator": {"type": "Rnig"}, "tests": []})");
        writeAll(templatesRoot() / "notes.txt", "not a template");
        writeAll(templatesRoot() / "1Bad.vfEQSQuery", kRingTemplate); // not a query name: never listed

        const mcp::ToolResult result = callWorker("eqs_list_queries", json::object());
        REQUIRE_MESSAGE(!result.isError, result.text);
        json out = result.structured;
        CHECK(out["directory"] == "assets/eqs");
        REQUIRE(out["queries"].size() == 2);

        json broken = out["queries"][0];
        CHECK(broken["name"] == "Broken");
        CHECK(broken["path"] == "assets/eqs/Broken.vfEQSQuery");
        CHECK(broken["valid"] == false);
        CHECK_MESSAGE(contains(broken["error"].get<std::string>(), "generator.type: unknown generator type 'Rnig'"),
                      broken["error"]);
        CHECK(broken["errorCount"] == 1);
        CHECK(broken["description"] == "A typo");
        CHECK_FALSE(broken.contains("generator"));

        json nearest = out["queries"][1];
        CHECK(nearest["name"] == "NearestPoint");
        CHECK(nearest["path"] == "assets/eqs/NearestPoint.vfEQSQuery");
        CHECK(nearest["valid"] == true);
        CHECK_FALSE(nearest.contains("error"));
        CHECK(nearest["description"] == "Nearest point of a ring");
        // Canonical: every param written out at its default.
        CHECK(nearest["generator"]["type"] == "Ring");
        CHECK(nearest["generator"]["params"]["center"] == "querier");
        CHECK(nearest["generator"]["params"]["radius"] == 10.0);
        CHECK(nearest["generator"]["params"]["pointCount"] == 12);
        REQUIRE(nearest["tests"].size() == 1);
        CHECK(nearest["tests"][0]["type"] == "Distance");
        CHECK(nearest["tests"][0]["params"]["horizontal"] == true);
        CHECK(nearest["tests"][0]["score"]["mode"] == "InverseLinear");
        CHECK(nearest["tests"][0]["score"]["combine"] == "Multiply");
        CHECK(nearest["maxResults"] == 1);
        CHECK(contains(nearest["summary"].get<std::string>(), "score Distance InverseLinear"));
        CHECK(contains(result.text, "2 EQS query templates in assets/eqs (1 invalid)"));

        // The catalogue mirrors eqs::generatorTypes() / testTypes().
        json catalog = out["catalog"];
        REQUIRE(catalog["generators"].size() == eqs::generatorTypes().size());
        REQUIRE(catalog["tests"].size() == eqs::testTypes().size());
        auto paramOf = [](json& type, const std::string& name) -> json
        {
            for (json& param : type["params"])
            {
                if (param["name"] == name)
                {
                    return param;
                }
            }
            return nullptr;
        };
        json ring = catalog["generators"][0];
        CHECK(ring["type"] == "Ring");
        json radius = paramOf(ring, "radius");
        CHECK(radius["kind"] == "float");
        CHECK(radius["required"] == false);
        CHECK(radius["min"] == 0.1);
        CHECK(radius["max"] == 500.0);
        CHECK(radius["default"] == 10.0);
        json center = paramOf(ring, "center");
        CHECK(center["kind"] == "enum");
        CHECK(center["values"] == json::array({"querier", "target"}));
        CHECK(center["default"] == "querier");

        json entities = catalog["generators"][3];
        CHECK(entities["type"] == "Entities");
        json name = paramOf(entities, "name");
        CHECK(name["kind"] == "string");
        CHECK(name["required"] == true);
        CHECK_FALSE(name.contains("default"));
        json maxCount = paramOf(entities, "maxCount");
        CHECK(maxCount["kind"] == "int");
        CHECK(maxCount["min"] == 1);
        CHECK(maxCount["max"] == 256);
        CHECK(maxCount["default"] == 64);

        json trace = catalog["tests"][4];
        CHECK(trace["type"] == "Trace");
        CHECK(trace["boolean"] == true);
        CHECK(paramOf(trace, "eyeHeight")["default"] == 1.6);

        json scoring = catalog["scoring"];
        CHECK(scoring["version"] == 1);
        CHECK(scoring["score"]["defaultMode"] == "Linear");
        CHECK(scoring["score"]["defaultCombine"] == "Multiply");
        CHECK(scoring["score"]["weight"]["max"] == 10.0);
        CHECK(scoring["score"]["weight"]["default"] == 1.0);
        CHECK(scoring["limits"]["maxCandidates"] == 1024);
        CHECK(scoring["limits"]["maxCandidatesWithPathCost"] == 256);
        CHECK(scoring["limits"]["maxResults"]["max"] == 64);
        // Every mode and combination the catalogue names is accepted and written back as named.
        REQUIRE(scoring["score"]["modes"].size() == 4);
        REQUIRE(scoring["score"]["combine"].size() == 2);
        for (const json& mode : scoring["score"]["modes"])
        {
            for (const json& combine : scoring["score"]["combine"])
            {
                json document = json::parse(kRingTemplate);
                document["tests"][0]["score"] = {{"mode", mode}, {"combine", combine}};
                const eqs::EQSSpecParseResult parsed = eqs::parseQuerySpec(document);
                REQUIRE(parsed.spec.has_value());
                json canonical = eqs::querySpecToJson(*parsed.spec);
                CHECK(canonical["tests"][0]["score"]["mode"] == mode);
                CHECK(canonical["tests"][0]["score"]["combine"] == combine);
            }
        }

        const mcp::ToolResult bare = callWorker("eqs_list_queries", {{"includeCatalog", false}});
        REQUIRE_FALSE(bare.isError);
        CHECK_FALSE(bare.structured.contains("catalog"));
        CHECK(bare.structured.at("queries").size() == 2);

        CHECK(contains(argError("eqs_list_queries", {{"catalog", true}}), "unknown argument 'catalog'"));
    }

    TEST_CASE_FIXTURE(TemplatesFixture, "eqs_list_queries reports an empty folder")
    {
        const mcp::ToolResult result = callWorker("eqs_list_queries", {{"includeCatalog", false}});
        REQUIRE_MESSAGE(!result.isError, result.text);
        CHECK(result.structured.at("directory") == "assets/eqs");
        CHECK(result.structured.at("queries").empty());
    }

    TEST_CASE_FIXTURE(ToolsFixture, "the template tools need a loaded project")
    {
        registerProject(std::nullopt);
        CHECK(contains(engineError("eqs_list_queries", json::object()), "No project is loaded"));
        CHECK(contains(engineError("eqs_get_query", {{"name", "Cover"}}), "No project is loaded"));
        CHECK(contains(engineError("eqs_set_query", {{"name", "Cover"}, {"query", json::parse(kRingTemplate)}}),
                       "No project is loaded"));
    }

    TEST_CASE_FIXTURE(TemplatesFixture, "eqs_set_query and eqs_get_query round-trip a template under the hash guard")
    {
        const json query = json::parse(kRingTemplate);
        const eqs::EQSSpecParseResult expectedSpec = eqs::parseQuerySpec(query);
        REQUIRE(expectedSpec.spec.has_value());

        const mcp::ToolResult created = call("eqs_set_query", {{"name", "Cover"}, {"query", query}, {"createNew", true}});
        REQUIRE_MESSAGE(!created.isError, created.text);
        json out = created.structured;
        CHECK(out["name"] == "Cover");
        CHECK(out["path"] == "assets/eqs/Cover.vfEQSQuery");
        CHECK(out["created"] == true);
        CHECK(out["warnings"].empty());
        const std::string firstHash = out["contentHash"].get<std::string>();
        CHECK(firstHash.size() == 16);
        REQUIRE(fs::exists(templatePath("Cover")));
        // Written in canonical form by the authoring service.
        CHECK(readAll(templatePath("Cover")) == eqs::querySpecToText(*expectedSpec.spec));
        CHECK(contains(created.text, "Created the EQS query 'Cover'"));

        const mcp::ToolResult got = call("eqs_get_query", {{"name", "Cover"}});
        REQUIRE_MESSAGE(!got.isError, got.text);
        json read = got.structured;
        CHECK(read["name"] == "Cover");
        CHECK(read["path"] == "assets/eqs/Cover.vfEQSQuery");
        CHECK(read["contentHash"] == firstHash);
        CHECK(read["valid"] == true);
        CHECK(read["query"] == eqs::querySpecToJson(*expectedSpec.spec));
        CHECK(read["query"]["version"] == 1);
        CHECK(read["query"]["generator"]["params"]["ringCount"] == 1);

        // createNew never replaces a template.
        const std::string before = readAll(templatePath("Cover"));
        CHECK(contains(engineError("eqs_set_query", {{"name", "Cover"}, {"query", query}, {"createNew", true}}),
                       "already exists"));
        CHECK(readAll(templatePath("Cover")) == before);

        // An edit of what eqs_get_query returned, under the hash it was read with.
        json edited = read["query"];
        edited["maxResults"] = 3;
        const mcp::ToolResult replaced = call("eqs_set_query", {{"name", "Cover"}, {"query", edited},
                                                                {"expectedContentHash", firstHash}});
        REQUIRE_MESSAGE(!replaced.isError, replaced.text);
        CHECK(replaced.structured.at("created") == false);
        const std::string secondHash = replaced.structured.at("contentHash").get<std::string>();
        CHECK(secondHash != firstHash);
        const mcp::ToolResult reread = call("eqs_get_query", {{"name", "Cover"}});
        CHECK(reread.structured.at("contentHash") == secondHash);
        CHECK(reread.structured.at("query").at("maxResults") == 3);

        // The first hash is stale now: refused, nothing written.
        const std::string current = readAll(templatePath("Cover"));
        const std::string stale = engineError("eqs_set_query", {{"name", "Cover"}, {"query", query},
                                                                {"expectedContentHash", firstHash}});
        CHECK_MESSAGE(contains(stale, "changed since it was read"), stale);
        CHECK(contains(stale, secondHash));
        CHECK(readAll(templatePath("Cover")) == current);

        // A hash guard on a template that does not exist.
        CHECK(contains(engineError("eqs_set_query", {{"name", "Missing"}, {"query", query},
                                                     {"expectedContentHash", firstHash}}),
                       "does not exist"));
        CHECK_FALSE(fs::exists(templatePath("Missing")));

        // Without a guard an existing template is replaced; the same spec gives the same canonical bytes.
        const mcp::ToolResult unguarded = call("eqs_set_query", {{"name", "Cover"}, {"query", query}});
        REQUIRE_MESSAGE(!unguarded.isError, unguarded.text);
        CHECK(unguarded.structured.at("created") == false);
        CHECK(unguarded.structured.at("contentHash") == firstHash);

        // A missing template.
        CHECK(contains(engineError("eqs_get_query", {{"name", "Nope"}}), "There is no EQS query 'Nope'"));
    }

    TEST_CASE_FIXTURE(TemplatesFixture, "eqs_set_query refuses invalid queries, names and guards, writing nothing")
    {
        const json query = json::parse(kRingTemplate);

        // Every strict error, each with its path inside 'query'.
        json bad = query;
        bad["generator"]["params"] = {{"radius", 9000}, {"pointCount", 2.5}};
        bad["tests"][0]["params"] = {{"radius", 1}};
        bad["extra"] = true;
        const std::string message = argError("eqs_set_query", {{"name", "Bad"}, {"query", bad}});
        CHECK_MESSAGE(contains(message, "the query has 4 error(s), so nothing was written"), message);
        CHECK(contains(message, "- extra: unknown key 'extra'"));
        CHECK(contains(message, "- generator.params.radius: must be between"));
        CHECK(contains(message, "- generator.params.pointCount: must be a whole number"));
        CHECK(contains(message, "- tests[0].params.radius: unknown parameter 'radius'"));
        CHECK_FALSE(fs::exists(templatePath("Bad")));

        // At most 20 listed.
        json many = query;
        for (int i = 0; i < 25; ++i)
        {
            many[std::format("k{:02}", i)] = i;
        }
        const std::string capped = argError("eqs_set_query", {{"name", "Many"}, {"query", many}});
        CHECK_MESSAGE(contains(capped, "the query has 25 error(s)"), capped);
        CHECK(countLines(capped, "- ") == 21);
        CHECK(contains(capped, "- ... and 5 more"));
        CHECK_FALSE(fs::exists(templatePath("Many")));

        // Names.
        for (const std::string& name : {std::string(""), std::string("1abc"), std::string("a-b"),
                                        std::string("has space"), std::string("a.b"), std::string(65, 'a')})
        {
            INFO(name);
            CHECK(contains(argError("eqs_set_query", {{"name", name}, {"query", query}}),
                           "argument 'name' must be a template name"));
        }
        CHECK(contains(argError("eqs_set_query", {{"query", query}}), "'name'"));
        CHECK(contains(argError("eqs_get_query", {{"name", "a/b"}}), "argument 'name' must be a template name"));

        // The query argument, the guards and unknown arguments.
        CHECK(contains(argError("eqs_set_query", {{"name", "Q"}, {"query", json::array()}}),
                       "argument 'query' must be the template object"));
        CHECK(contains(argError("eqs_set_query", {{"name", "Q"}, {"query", "Ring"}}), "argument 'query'"));
        CHECK(contains(argError("eqs_set_query", {{"name", "Q"}, {"query", query}, {"expectedContentHash", "xyz"}}),
                       "expectedContentHash"));
        CHECK(contains(argError("eqs_set_query", {{"name", "Q"}, {"query", query}, {"expectedContentHash", 12}}),
                       "must be a string"));
        CHECK(contains(argError("eqs_set_query", {{"name", "Q"}, {"query", query}, {"createNew", true},
                                                  {"expectedContentHash", "00"}}),
                       "exclude each other"));
        CHECK(contains(argError("eqs_set_query", {{"name", "Q"}, {"query", query}, {"path", "x"}}),
                       "unknown argument 'path'"));
        CHECK_FALSE(fs::exists(templatePath("Q")));

        // 64 characters is the longest name.
        const std::string longest = "A" + std::string(63, 'b');
        const mcp::ToolResult saved = call("eqs_set_query", {{"name", longest}, {"query", query}});
        REQUIRE_MESSAGE(!saved.isError, saved.text);
        CHECK(fs::exists(templatePath(longest)));
    }

    TEST_CASE_FIXTURE(TemplatesFixture, "eqs_set_query warns about Play mode and a query without a score test")
    {
        json filtersOnly = json::parse(kRingTemplate);
        json onNavMesh = {{"type", "OnNavMesh"}, {"filter", json::object()}};
        filtersOnly["tests"] = json::array({onNavMesh});

        playing = true;
        const mcp::ToolResult result = call("eqs_set_query", {{"name", "Spots"}, {"query", filtersOnly}});
        REQUIRE_MESSAGE(!result.isError, result.text);
        const json warnings = result.structured.at("warnings");
        REQUIRE(warnings.size() == 2);
        CHECK(contains(warnings[0].get<std::string>(), "Play mode"));
        CHECK(contains(warnings[1].get<std::string>(), "no score test"));
        CHECK(contains(result.text, "Warning: Play mode"));
        CHECK(fs::exists(templatePath("Spots")));
    }

    TEST_CASE_FIXTURE(TemplatesFixture, "eqs_get_query reports an invalid template with its errors and text")
    {
        const std::string text =
            R"({"generator": {"type": "Ring", "params": {"radius": -1}}, "tests": [{"type": "Distance"}]})";
        writeAll(templatePath("Broken"), text);

        const mcp::ToolResult result = call("eqs_get_query", {{"name", "Broken"}});
        REQUIRE_MESSAGE(!result.isError, result.text);
        json out = result.structured;
        CHECK(out["valid"] == false);
        CHECK(out["text"] == text);
        CHECK(out["contentHash"].get<std::string>().size() == 16);
        CHECK_FALSE(out.contains("query"));
        CHECK(out["errorCount"] == 2);
        REQUIRE(out["errors"].size() == 2);
        CHECK(out["errors"][0]["path"] == "generator.params.radius");
        CHECK(out["errors"][1]["path"] == "tests[0]");
        CHECK(out["errors"][1]["message"] == "needs 'filter' or 'score'");

        // Its hash guards the fix.
        const mcp::ToolResult fixed = call("eqs_set_query", {{"name", "Broken"}, {"query", json::parse(kRingTemplate)},
                                                             {"expectedContentHash", out["contentHash"]}});
        REQUIRE_MESSAGE(!fixed.isError, fixed.text);
        CHECK(call("eqs_get_query", {{"name", "Broken"}}).structured.at("valid") == true);
    }

    TEST_CASE_FIXTURE(RunFixture, "eqs_run builds the querier's context and maps every candidate")
    {
        constexpr float nan = eqs::kNoMetric;
        entities[21] = {"Enemy_A", worldAt(glm::vec3(3.0f, 0.0f, 2.0f))};
        entities[22] = {"Enemy_B", worldAt(glm::vec3(2.0f, 0.0f, 2.0f))};
        entities[23] = {"Enemy_C", worldAt(glm::vec3(9.0f, 0.0f, 9.0f))};
        services::NavmeshAgentData navAgent;
        navAgent.radius = 0.5f;
        navAgent.height = 1.8f;
        agent = navAgent;
        templateText = R"({
            "generator": {"type": "Entities", "params": {"name": "Enemy", "match": "prefix"}},
            "tests": [{"type": "OnNavMesh", "filter": {}}, {"type": "Trace", "filter": {}},
                      {"type": "Distance", "score": {"mode": "InverseLinear"}}]
        })";

        nextRun.testNames = {"OnNavMesh", "Trace", "Distance"};
        nextRun.candidates = {
            debugCandidate({3.0f, 0.0f, 2.0f}, 21, -1, {1.0f, 1.0f, 4.0f}, {0.0f, 0.0f, 0.5f}, 0.5f),
            debugCandidate({2.0f, 0.0f, 2.0f}, 22, -1, {1.0f, 1.0f, 2.0f}, {0.0f, 0.0f, 1.0f}, 1.0f),
            debugCandidate({9.0f, 0.0f, 9.0f}, 23, 0, {0.0f, nan, nan}, {0.0f, 0.0f, 0.0f}, 0.0f),
            debugCandidate({8.0f, 0.0f, 8.0f}, 24, 1, {1.0f, 0.0f, nan}, {0.0f, 0.0f, 0.0f}, 0.0f),
            debugCandidate({7.0f, 0.0f, 7.0f}, 25, 1, {1.0f, 0.0f, nan}, {0.0f, 0.0f, 0.0f}, 0.0f),
        };
        nextRun.result.status = eqs::EQSQueryStatus::Completed;
        nextRun.result.candidates = {eqs::EQSCandidate{glm::vec3(2.0f, 0.0f, 2.0f), 22, 1.0f, false}};
        nextRun.elapsedMs = 1.23456;

        const mcp::ToolResult result = call("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}, {"target", kTarget},
                                                        {"maxCandidates", 3}, {"showSeconds", 5}});
        REQUIRE_MESSAGE(!result.isError, result.text);

        // The context the BT adapter would build.
        REQUIRE(runs.size() == 1);
        CHECK(runs[0].queryName == "Hunt");
        const eqs::EQSContext& context = runs[0].context;
        CHECK(context.querierEntityId == kQuerier);
        CHECK(context.querierPosition == glm::vec3(1.0f, 0.0f, 2.0f));
        // Yaw 90 degrees turns +Z to +X.
        CHECK(context.querierForward.x == doctest::Approx(1.0f));
        CHECK(context.querierForward.y == 0.0f);
        CHECK(context.querierForward.z == doctest::Approx(0.0f));
        CHECK(context.agentRadius == 0.5f);
        CHECK(context.agentHeight == 1.8f);
        CHECK(context.hasTarget);
        CHECK(context.targetEntityId == kTarget);
        CHECK(context.targetPosition == glm::vec3(5.0f, 0.0f, 5.0f));
        CHECK(context.seed == kQuerier);

        json out = result.structured;
        CHECK(out["name"] == "Hunt");
        CHECK(out["status"] == "Completed");
        CHECK_FALSE(out.contains("error"));
        CHECK(out["best"]["entity"]["id"] == 22);
        CHECK(out["best"]["entity"]["name"] == "Enemy_B");
        CHECK(out["best"]["position"] == json::array({2.0, 0.0, 2.0}));
        CHECK(out["best"]["score"] == 1.0);

        CHECK(out["counts"]["generated"] == 5);
        CHECK(out["counts"]["filteredBy"] == json::object({{"OnNavMesh", 1}, {"Trace", 2}}));
        CHECK(out["counts"]["scored"] == 2);
        CHECK(out["counts"]["results"] == 1);

        REQUIRE(out["tests"].size() == 3);
        CHECK(out["tests"][0] == json::object({{"key", "OnNavMesh"}, {"type", "OnNavMesh"}, {"purpose", "filter"}}));
        CHECK(out["tests"][1]["purpose"] == "filter");
        CHECK(out["tests"][2]["purpose"] == "score");

        // The kept ones by total, then the first dropped one; cut to maxCandidates.
        REQUIRE(out["candidates"].size() == 3);
        json first = out["candidates"][0];
        CHECK(first["entity"]["id"] == 22);
        CHECK(first["total"] == 1.0);
        CHECK_FALSE(first.contains("filteredBy"));
        CHECK(first["metrics"] == json::object({{"OnNavMesh", 1.0}, {"Trace", 1.0}, {"Distance", 2.0}}));
        CHECK(first["scores"] == json::object({{"Distance", 1.0}}));
        json second = out["candidates"][1];
        CHECK(second["entity"]["name"] == "Enemy_A");
        CHECK(second["total"] == 0.5);
        json dropped = out["candidates"][2];
        CHECK(dropped["entity"]["id"] == 23);
        CHECK(dropped["filteredBy"] == "OnNavMesh");
        CHECK(dropped["metrics"] == json::object({{"OnNavMesh", 0.0}})); // Trace and Distance never measured it
        CHECK(dropped["scores"].empty());
        CHECK(dropped["total"] == 0.0);

        CHECK(out["timeMs"].get<double>() == doctest::Approx(1.235));
        CHECK(out["context"]["querier"]["name"] == "Guard");
        CHECK(out["context"]["target"]["name"] == "Player");
        CHECK(out["context"]["target"]["position"] == json::array({5.0, 0.0, 5.0}));
        CHECK(out["context"]["agentRadius"] == 0.5);

        const std::string notes = out["notes"].dump();
        CHECK_MESSAGE(contains(notes, "Edit mode: physics has no entity colliders"), notes);
        CHECK(contains(notes, "candidates lists 3 of the 5 candidates"));
        CHECK(contains(notes, "drawn in the viewport for 5 s"));
        CHECK_FALSE(contains(notes, "no NavmeshAgent"));
        CHECK_FALSE(contains(notes, "does not match"));

        REQUIRE(shown.size() == 1);
        CHECK(shown[0].seconds == 5.0f);
        CHECK(shown[0].result.candidates.size() == 5);
        CHECK(contains(result.text, "best is entity 22 'Enemy_B'"));
        CHECK(contains(result.text, "5 generated, 2 kept"));
    }

    TEST_CASE_FIXTURE(RunFixture, "eqs_run reports points, unknown purposes and the querier's missing agent")
    {
        constexpr float nan = eqs::kNoMetric;
        playing = true;
        // The file no longer has the run's tests (edited after the engine cached it).
        templateText = kRingTemplate;
        nextRun.testNames = {"PathCost", "Distance"};
        nextRun.candidates = {
            debugCandidate({0.0f, 0.0f, 5.0f}, eqs::kNoEntity, -1, {nan, 5.0f}, {0.0f, 0.25f}, 0.0f),
            debugCandidate({0.0f, 0.0f, 3.0f}, eqs::kNoEntity, -1, {3.0f, 3.0f}, {1.0f, 1.0f}, 1.0f),
        };
        nextRun.result.status = eqs::EQSQueryStatus::Completed;
        nextRun.result.candidates = {eqs::EQSCandidate{glm::vec3(0.0f, 0.0f, 3.0f), eqs::kNoEntity, 1.0f, false}};

        const mcp::ToolResult result = call("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}, {"show", false}});
        REQUIRE_MESSAGE(!result.isError, result.text);
        CHECK(shown.empty());
        REQUIRE(runs.size() == 1);
        CHECK_FALSE(runs[0].context.hasTarget);
        CHECK(runs[0].context.agentRadius == eqs::EQSContext{}.agentRadius);

        json out = result.structured;
        CHECK_FALSE(out["best"].contains("entity"));
        CHECK(out["best"]["position"] == json::array({0.0, 0.0, 3.0}));
        CHECK(out["context"]["target"].is_null());
        CHECK(out["tests"][0]["purpose"].is_null());
        REQUIRE(out["candidates"].size() == 2);
        json best = out["candidates"][0];
        CHECK_FALSE(best.contains("entity"));
        CHECK(best["metrics"] == json::object({{"PathCost", 3.0}, {"Distance", 3.0}}));
        CHECK(best["scores"] == json::object({{"PathCost", 1.0}, {"Distance", 1.0}})); // purposes unknown: every test
        CHECK(out["candidates"][1]["metrics"] == json::object({{"Distance", 5.0}}));

        const std::string notes = out["notes"].dump();
        CHECK_MESSAGE(contains(notes, "does not match the copy the engine ran"), notes);
        CHECK(contains(notes, "no NavmeshAgent"));
        CHECK_FALSE(contains(notes, "Edit mode"));
        CHECK_FALSE(contains(notes, "drawn in the viewport"));
        CHECK(contains(result.text, "best is the point"));
    }

    TEST_CASE_FIXTURE(RunFixture, "eqs_run reports a failed run in-band and hints at a missing target")
    {
        templateText = kRingTemplate;
        nextRun.testNames = {"Distance"};
        nextRun.result.status = eqs::EQSQueryStatus::Failed;
        nextRun.result.error = "the Ring generator needs a target (the query was run without one)";

        const mcp::ToolResult result = call("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}});
        CHECK(result.isError);
        json out = result.structured;
        CHECK(out["status"] == "Failed");
        CHECK(out["error"] == nextRun.result.error);
        CHECK(out["best"].is_null());
        CHECK(out["candidates"].empty());
        CHECK(out["counts"]["generated"] == 0);
        CHECK(contains(out["notes"].dump(), "pass target"));
        CHECK(contains(result.text, "EQS query 'Hunt' failed"));
    }

    TEST_CASE_FIXTURE(RunFixture, "eqs_run refuses a pending load, bad arguments and missing or invalid templates")
    {
        loadPending = true;
        const mcp::ToolResult pending = call("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}});
        CHECK(pending.isError);
        CHECK(contains(pending.text, "scene load"));
        loadPending = false;

        CHECK(contains(engineError("eqs_run", {{"name", "Hunt"}, {"querier", 99}}), "Entity 99"));
        CHECK(contains(engineError("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}, {"target", 77}}), "Entity 77"));
        CHECK(contains(argError("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}, {"target", "Player"}}),
                       "must be an entity id or a world point"));
        CHECK(contains(argError("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}, {"target", json::array({1, 2})}}),
                       "[x, y, z]"));
        CHECK(contains(argError("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}, {"maxCandidates", 0}}),
                       "between 1 and 200"));
        CHECK(contains(argError("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}, {"maxCandidates", 201}}),
                       "between 1 and 200"));
        CHECK(contains(argError("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}, {"showSeconds", 0.5}}),
                       "argument 'showSeconds' must be between"));
        CHECK(contains(argError("eqs_run", {{"name", "bad name"}, {"querier", kQuerier}}),
                       "argument 'name' must be a template name"));
        CHECK(contains(argError("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}, {"radius", 3}}),
                       "unknown argument 'radius'"));
        CHECK(contains(argError("eqs_run", {{"name", "Hunt"}}), "'querier'"));

        info.exists = false;
        CHECK(contains(engineError("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}}), "There is no EQS query 'Hunt'"));
        info.exists = true;
        info.valid = false;
        info.errors = {{"generator.type", "unknown generator type 'Rnig'"}};
        const std::string invalid = engineError("eqs_run", {{"name", "Hunt"}, {"querier", kQuerier}});
        CHECK_MESSAGE(contains(invalid, "is invalid"), invalid);
        CHECK(contains(invalid, "generator.type: unknown generator type 'Rnig'"));

        CHECK(runs.empty());
        CHECK(shown.empty());
    }
}
