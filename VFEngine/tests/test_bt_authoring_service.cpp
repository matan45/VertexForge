#include <doctest.h>
#include "impl/ai/BehaviorTreeAuthoringService.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ai/BehaviorTreeAuthoringEvents.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "archive/VFPakFormat.hpp"
#include "asset/AssetDatabase.hpp"
#include "behaviortree/BehaviorTreeAsset.hpp"
#include "behaviortree/BehaviorTreeTypes.hpp"
#include "behaviortree/BehaviorTreeValidation.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

// ============================================================
// VK-1660: BehaviorTreeAuthoringService - LoadBehaviorTreeAssetQuery / SaveBehaviorTreeAssetCommand,
// the validated .vfBehaviorTree write path of the MCP bt_* tools.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    namespace bt = behaviortree;
    using Severity = bt::validation::Severity;

    fs::path authoringRoot()
    {
        return fs::temp_directory_path() / "vf_bt_authoring_service_tests";
    }

    fs::path testScriptsRoot()
    {
        return authoringRoot() / "scripts";
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

    uint64_t hashOfFile(const fs::path& path)
    {
        const std::string bytes = readAll(path);
        return archive::hashBytes(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    }

    // Root(1) -> Sequence(2) -> Wait(3): passes engine and strict validation.
    bt::BehaviorTreeData makeTree(float waitSeconds = 1.5f)
    {
        bt::BehaviorTreeData data = bt::BehaviorTreeAsset::createDefault("AuthoringTest");
        bt::BTNode wait;
        wait.id = data.graph.nextNodeId++;
        wait.type = bt::BTNodeType::Wait;
        wait.name = "Wait";
        wait.properties["duration"] = waitSeconds;
        data.graph.nodes.push_back(wait);

        bt::BTLink link;
        link.id = data.graph.nextLinkId++;
        link.sourceNodeId = 2;
        link.targetNodeId = wait.id;
        data.graph.links.push_back(link);
        return data;
    }

    bool hasDiagnostic(const std::vector<bt::validation::Diagnostic>& diagnostics, Severity severity,
                       const std::string& text)
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const bt::validation::Diagnostic& d)
        {
            return d.severity == severity && d.message.find(text) != std::string::npos;
        });
    }

    // A clean dispatcher, asset database and temp folder per case; records what a save publishes.
    struct AuthoringFixture
    {
        services::BehaviorTreeAuthoringService service{[] { return testScriptsRoot(); }};
        std::vector<std::string> savedPaths;
        std::vector<std::string> reloadedPaths;

        AuthoringFixture()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.clear();
            asset::AssetDatabase::instance().clear();
            std::error_code ec;
            fs::remove_all(authoringRoot(), ec);
            fs::create_directories(testScriptsRoot(), ec);

            service.registerEventHandlers();
            dispatcher.subscribe<events::resource::AssetSavedNotification>(
                [this](const events::resource::AssetSavedNotification& notification)
                {
                    savedPaths.push_back(notification.filePath);
                });
            dispatcher.registerCommandHandler<events::ai::ReloadBehaviorTreeAssetCommand>(
                [this](const events::ai::ReloadBehaviorTreeAssetCommand& cmd)
                {
                    reloadedPaths.push_back(cmd.treePath);
                });
        }

        ~AuthoringFixture()
        {
            service.unregisterEventHandlers();
            events::EventDispatcher::instance().clear();
            asset::AssetDatabase::instance().clear();
            std::error_code ec;
            fs::remove_all(authoringRoot(), ec);
        }

        AuthoringFixture(const AuthoringFixture&) = delete;
        AuthoringFixture& operator=(const AuthoringFixture&) = delete;

        services::BehaviorTreeAssetSaveResult save(const fs::path& path, const bt::BehaviorTreeData& data,
                                                   std::optional<uint64_t> expectedContentHash = std::nullopt,
                                                   bool createNew = false,
                                                   std::optional<std::unordered_set<uint32_t>> strictNodes = std::nullopt)
        {
            events::ai::SaveBehaviorTreeAssetCommand cmd;
            cmd.path = path.string();
            cmd.data = data;
            cmd.expectedContentHash = expectedContentHash;
            cmd.createNew = createNew;
            cmd.strictNodes = std::move(strictNodes);
            return events::EventDispatcher::instance().execute(cmd);
        }

        services::BehaviorTreeAssetLoadResult load(const fs::path& path)
        {
            events::ai::LoadBehaviorTreeAssetQuery query;
            query.path = path.string();
            return events::EventDispatcher::instance().query(query);
        }
    };
}

TEST_SUITE("BTAuthoringService")
{
    TEST_CASE_FIXTURE(AuthoringFixture, "a new tree is written as serialized, hashed, announced and reloaded")
    {
        const fs::path path = authoringRoot() / "ai" / "Enemy.vfBehaviorTree";
        const bt::BehaviorTreeData data = makeTree();

        const auto result = save(path, data, std::nullopt, true);
        REQUIRE_MESSAGE(result.success, result.error);
        CHECK(result.created);
        CHECK(result.error.empty());
        CHECK_FALSE(hasDiagnostic(result.diagnostics, Severity::Error, ""));
        CHECK(readAll(path) == bt::BehaviorTreeAsset::serialize(data));
        CHECK(result.contentHash == hashOfFile(path));

        REQUIRE(savedPaths.size() == 1);
        CHECK(savedPaths[0] == path.string()); // the absolute path, as given
        REQUIRE(reloadedPaths.size() == 1);
        CHECK(fs::equivalent(fs::path(reloadedPaths[0]), path)); // the asset database's form of the same file

        const auto loaded = load(path);
        REQUIRE_MESSAGE(loaded.success, loaded.error);
        CHECK(loaded.contentHash == result.contentHash);
        CHECK(bt::BehaviorTreeAsset::serialize(loaded.data) == bt::BehaviorTreeAsset::serialize(data));
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "only absolute .vfBehaviorTree paths are accepted")
    {
        const auto empty = save(fs::path(), makeTree());
        CHECK_FALSE(empty.success);
        CHECK_FALSE(empty.error.empty());

        const auto relative = save(fs::path("ai") / "Enemy.vfBehaviorTree", makeTree());
        CHECK_FALSE(relative.success);
        CHECK(relative.error.find("absolute") != std::string::npos);

        const auto otherAsset = save(authoringRoot() / "Enemy.json", makeTree());
        CHECK_FALSE(otherAsset.success);
        CHECK(otherAsset.error.find(".vfBehaviorTree") != std::string::npos);
        CHECK_FALSE(fs::exists(authoringRoot() / "Enemy.json"));

        // The extension is matched in any case.
        const auto upper = save(authoringRoot() / "Upper.VFBEHAVIORTREE", makeTree());
        CHECK_MESSAGE(upper.success, upper.error);

        CHECK(savedPaths.size() == 1);
        CHECK(reloadedPaths.size() == 1);
        CHECK_FALSE(load(authoringRoot() / "Enemy.json").success);
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "createNew refuses an existing file")
    {
        const fs::path path = authoringRoot() / "Existing.vfBehaviorTree";
        REQUIRE(save(path, makeTree(1.0f)).success);
        const std::string before = readAll(path);

        const auto again = save(path, makeTree(2.0f), std::nullopt, true);
        CHECK_FALSE(again.success);
        CHECK(again.error.find("already exists") != std::string::npos);
        CHECK(readAll(path) == before);
        CHECK(savedPaths.size() == 1);
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "expectedContentHash guards against edits made elsewhere")
    {
        const fs::path path = authoringRoot() / "Guarded.vfBehaviorTree";
        const auto first = save(path, makeTree(1.0f));
        REQUIRE(first.success);
        CHECK(first.created);

        const auto second = save(path, makeTree(2.0f), first.contentHash);
        REQUIRE_MESSAGE(second.success, second.error);
        CHECK_FALSE(second.created);
        CHECK(second.contentHash != first.contentHash);
        CHECK(second.contentHash == hashOfFile(path));

        const std::string before = readAll(path);
        const auto stale = save(path, makeTree(3.0f), first.contentHash);
        CHECK_FALSE(stale.success);
        CHECK(stale.error.find("changed on disk") != std::string::npos);
        CHECK(readAll(path) == before);

        const fs::path gone = authoringRoot() / "Gone.vfBehaviorTree";
        const auto missing = save(gone, makeTree(), second.contentHash);
        CHECK_FALSE(missing.success);
        CHECK(missing.error.find("no longer exists") != std::string::npos);
        CHECK_FALSE(fs::exists(gone));
        CHECK(savedPaths.size() == 2);
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "validation errors refuse the save and write nothing")
    {
        const fs::path path = authoringRoot() / "Invalid.vfBehaviorTree";

        bt::BehaviorTreeData orphaned = makeTree();
        orphaned.graph.links.clear(); // engine validation: Root and Sequence without children
        const auto engine = save(path, orphaned);
        CHECK_FALSE(engine.success);
        CHECK(engine.error.find("validation error") != std::string::npos);
        CHECK(hasDiagnostic(engine.diagnostics, Severity::Error, "Root node has no child"));

        bt::BehaviorTreeData unknownParam = makeTree();
        unknownParam.graph.findNodeById(3)->properties["speed"] = 1.0f; // strict catalogue
        const auto strict = save(path, unknownParam);
        CHECK_FALSE(strict.success);
        CHECK(hasDiagnostic(strict.diagnostics, Severity::Error, "unknown parameter 'speed'"));

        bt::BehaviorTreeData missingSubTree = makeTree();
        bt::BTNode* sub = missingSubTree.graph.findNodeById(3);
        sub->type = bt::BTNodeType::SubTree;
        sub->name = "Sub";
        sub->properties.clear();
        sub->properties["treePath"] = (authoringRoot() / "Child.vfBehaviorTree").string();
        const auto noChild = save(path, missingSubTree);
        CHECK_FALSE(noChild.success);
        CHECK(hasDiagnostic(noChild.diagnostics, Severity::Error, "does not exist"));

        CHECK_FALSE(fs::exists(path));
        CHECK(savedPaths.empty());
        CHECK(reloadedPaths.empty());

        // Once the child exists, the parent saves.
        REQUIRE(save(authoringRoot() / "Child.vfBehaviorTree", makeTree()).success);
        const auto withChild = save(path, missingSubTree);
        CHECK_MESSAGE(withChild.success, withChild.error);
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "strictNodes scopes the per-node strict checks")
    {
        bt::BehaviorTreeData data = makeTree();
        data.graph.findNodeById(3)->properties["speed"] = 1.0f; // a strict finding on node 3 only
        const fs::path path = authoringRoot() / "Scoped.vfBehaviorTree";

        CHECK_FALSE(save(path, data).success);                                                  // every node
        CHECK_FALSE(save(path, data, std::nullopt, false, std::unordered_set<uint32_t>{3}).success); // node 3 touched
        CHECK_FALSE(fs::exists(path));

        const auto untouched = save(path, data, std::nullopt, false, std::unordered_set<uint32_t>{2});
        REQUIRE_MESSAGE(untouched.success, untouched.error);
        const auto none = save(path, data, untouched.contentHash, false, std::unordered_set<uint32_t>{});
        CHECK_MESSAGE(none.success, none.error);
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "a ScriptTask must name an @Script class with tick(float)")
    {
        writeAll(testScriptsRoot() / "game" / "ai" / "Attack.mt",
                 "@Script\npublic class Attack extends Behaviour {\n"
                 "    public function tick(float dt): string { return \"running\"; }\n}\n");
        writeAll(testScriptsRoot() / "game" / "ai" / "NotATask.mt",
                 "@Script\npublic class NotATask extends Behaviour {\n"
                 "    public function onUpdate(float dt): void {}\n}\n");

        bt::BehaviorTreeData data = makeTree();
        bt::BTNode* task = data.graph.findNodeById(3);
        task->type = bt::BTNodeType::ScriptTask;
        task->name = "Attack";
        task->properties.clear();
        task->scriptPath = "game/ai/Attack.mt";
        task->scriptClassName = "Attack";

        const fs::path path = authoringRoot() / "Scripted.vfBehaviorTree";
        const auto ok = save(path, data);
        REQUIRE_MESSAGE(ok.success, ok.error);

        task->scriptClassName = "Wrong";
        const auto mismatch = save(path, data, ok.contentHash);
        CHECK_FALSE(mismatch.success);
        CHECK(hasDiagnostic(mismatch.diagnostics, Severity::Error, "does not match the class 'Attack'"));

        task->scriptPath = "game/ai/NotATask.mt";
        task->scriptClassName = "NotATask";
        const auto notTask = save(path, data, ok.contentHash);
        CHECK_FALSE(notTask.success);
        CHECK(hasDiagnostic(notTask.diagnostics, Severity::Error, "is not a ScriptTask"));

        task->scriptPath = "game/ai/Missing.mt";
        CHECK_FALSE(save(path, data, ok.contentHash).success);
        CHECK(hashOfFile(path) == ok.contentHash);

        CHECK(service.scriptTaskClass("game/ai/Attack.mt") == std::optional<std::string>("Attack"));
        CHECK_FALSE(service.scriptTaskClass("game/ai/NotATask.mt").has_value());
        CHECK_FALSE(service.scriptTaskClass("").has_value());
        // A path that does not exist under the scripts root is tried as given (e.g. absolute).
        const std::string absolute = (testScriptsRoot() / "game" / "ai" / "Attack.mt").string();
        CHECK(service.scriptTaskClass(absolute) == std::optional<std::string>("Attack"));
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "load reports files that are missing or not behavior trees")
    {
        const auto missing = load(authoringRoot() / "Missing.vfBehaviorTree");
        CHECK_FALSE(missing.success);
        CHECK(missing.error.find("does not exist") != std::string::npos);

        const fs::path junk = authoringRoot() / "Junk.vfBehaviorTree";
        writeAll(junk, "{ not json");
        const auto bad = load(junk);
        CHECK_FALSE(bad.success);
        CHECK(bad.error.find("not a behavior tree") != std::string::npos);

        writeAll(junk, "[1, 2, 3]");
        CHECK_FALSE(load(junk).success);
    }
}
