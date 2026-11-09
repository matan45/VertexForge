#include <doctest.h>
#include "impl/ai/EQSServiceImpl.hpp"
#include "providers/ai/IEQSProvider.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ai/EQSEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/render/DebugDrawEvents.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

// ============================================================
// VK-1660: EQSServiceImpl - the EQS events over the provider, the EQS frame task (queries only while
// simulating), the debug overlay, template invalidation on save and the template listing.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    fs::path serviceRoot()
    {
        return fs::temp_directory_path() / "vf_eqs_service_tests";
    }

    void writeAll(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    }

    struct FakeEQSProvider : services::IEQSProvider
    {
        int updateCalls = 0;
        float lastBudget = 0.0f;
        int releaseAllCalls = 0;
        std::vector<std::string> submitted;
        std::vector<uint64_t> released;
        std::vector<std::string> invalidated;
        std::vector<std::string> looked;
        eqs::EQSQueryHandle nextHandle{7};

        eqs::EQSQueryHandle submitQuery(const std::string& queryName, const eqs::EQSContext&) override
        {
            submitted.push_back(queryName);
            return nextHandle;
        }

        eqs::EQSResult getResult(eqs::EQSQueryHandle handle) const override
        {
            eqs::EQSResult result;
            result.status = handle == nextHandle ? eqs::EQSQueryStatus::Completed : eqs::EQSQueryStatus::Failed;
            return result;
        }

        void releaseQuery(eqs::EQSQueryHandle handle) override { released.push_back(handle.id); }
        void releaseAll() override { ++releaseAllCalls; }

        void update(float budgetMs) override
        {
            ++updateCalls;
            lastBudget = budgetMs;
        }

        eqs::EQSDebugResult runQueryNow(const std::string& queryName, const eqs::EQSContext&) override
        {
            eqs::EQSDebugResult debug;
            debug.result.status = eqs::EQSQueryStatus::Completed;
            debug.testNames = {queryName};
            return debug;
        }

        services::EQSQueryInfo lookupQuery(const std::string& queryName) override
        {
            looked.push_back(queryName);
            services::EQSQueryInfo info;
            info.name = queryName;
            info.exists = true;
            info.valid = true;
            return info;
        }

        void invalidateQuery(const std::string& queryName) override { invalidated.push_back(queryName); }
    };

    // A clean dispatcher and temp template folder per case; records the overlay's spheres.
    struct ServiceFixture
    {
        FakeEQSProvider provider;
        services::EQSServiceImpl service{&provider, [] { return serviceRoot(); }};
        std::vector<events::debugdraw::DrawSphereCommand> spheres;

        ServiceFixture()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.clear();
            std::error_code ec;
            fs::remove_all(serviceRoot(), ec);
            fs::create_directories(serviceRoot(), ec);

            service.registerEventHandlers();
            dispatcher.registerCommandHandler<events::debugdraw::DrawSphereCommand>(
                [this](const events::debugdraw::DrawSphereCommand& cmd)
                {
                    spheres.push_back(cmd);
                });
        }

        ~ServiceFixture()
        {
            // The service unregisters its own handlers when it is destroyed, after this body.
            events::EventDispatcher::instance().unregisterCommandHandler<events::debugdraw::DrawSphereCommand>();
            std::error_code ec;
            fs::remove_all(serviceRoot(), ec);
        }

        ServiceFixture(const ServiceFixture&) = delete;
        ServiceFixture& operator=(const ServiceFixture&) = delete;

        static void showOverlay(float seconds)
        {
            eqs::EQSDebugResult run;
            run.result.status = eqs::EQSQueryStatus::Completed;
            eqs::EQSCandidate best;
            best.position = glm::vec3(1.0f, 0.0f, 0.0f);
            best.totalScore = 1.0f;
            run.result.candidates.push_back(best);

            eqs::EQSDebugCandidate kept;
            kept.position = glm::vec3(1.0f, 0.0f, 0.0f);
            kept.total = 1.0f;
            eqs::EQSDebugCandidate dropped;
            dropped.position = glm::vec3(2.0f, 0.0f, 0.0f);
            dropped.filteredByTest = 0;
            run.candidates = {kept, dropped};
            run.testNames = {"OnNavMesh"};

            events::ai::ShowEQSDebugCommand show;
            show.result = run;
            show.seconds = seconds;
            events::EventDispatcher::instance().execute(show);
        }

        static void publishSaved(const fs::path& path)
        {
            events::resource::AssetSavedNotification notification;
            notification.filePath = path.string();
            events::EventDispatcher::instance().publish(notification);
        }

        static void publishMode(services::EditorMode previous, services::EditorMode current)
        {
            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = previous;
            notification.currentMode = current;
            events::EventDispatcher::instance().publish(notification);
        }
    };
}

TEST_SUITE("EQSService")
{
    TEST_CASE_FIXTURE(ServiceFixture, "queued queries run only while simulating, within the frame budget")
    {
        service.update(0.016f, false);
        CHECK(provider.updateCalls == 0);

        service.update(0.016f, true);
        CHECK(provider.updateCalls == 1);
        CHECK(provider.lastBudget == doctest::Approx(services::EQSServiceImpl::kFrameBudgetMs));
    }

    TEST_CASE_FIXTURE(ServiceFixture, "the debug overlay draws a run in Edit mode until it expires")
    {
        showOverlay(1.0f);

        service.update(0.5f, false);
        REQUIRE(spheres.size() == 3); // the kept and the dropped candidate, then the best marker
        CHECK(spheres[0].radius > spheres[1].radius); // kept larger than dropped
        CHECK(spheres[2].radius > spheres[0].radius); // the best one largest
        CHECK(provider.updateCalls == 0);

        spheres.clear();
        service.update(0.6f, false); // 1.1 s in total: expired
        CHECK(spheres.empty());
        service.update(0.1f, false);
        CHECK(spheres.empty());
    }

    TEST_CASE_FIXTURE(ServiceFixture, "an overlay with no debug-draw handler switches itself off instead of throwing")
    {
        events::EventDispatcher::instance().unregisterCommandHandler<events::debugdraw::DrawSphereCommand>();
        showOverlay(5.0f);

        CHECK_NOTHROW(service.update(0.1f, false));
        CHECK_NOTHROW(service.update(0.1f, true));
        CHECK(provider.updateCalls == 1);
    }

    TEST_CASE_FIXTURE(ServiceFixture, "saving a template drops its cached copy; other saves do not")
    {
        publishSaved(serviceRoot() / "Cover.vfEQSQuery");
        publishSaved(serviceRoot() / "Flank.VFEQSQUERY");
        publishSaved(serviceRoot() / "Guard.vfBehaviorTree");

        CHECK(provider.invalidated == std::vector<std::string>{"Cover", "Flank"});
    }

    TEST_CASE_FIXTURE(ServiceFixture, "leaving Play releases every query; entering it does not")
    {
        publishMode(services::EditorMode::Edit, services::EditorMode::Play);
        CHECK(provider.releaseAllCalls == 0);

        publishMode(services::EditorMode::Play, services::EditorMode::Edit);
        CHECK(provider.releaseAllCalls == 1);
    }

    TEST_CASE_FIXTURE(ServiceFixture, "ListEQSQueriesQuery lists the folder's valid template names, sorted")
    {
        writeAll(serviceRoot() / "Zeta.vfEQSQuery", "{}");
        writeAll(serviceRoot() / "alpha.vfeqsquery", "{}");
        writeAll(serviceRoot() / "9Bad.vfEQSQuery", "{}");
        writeAll(serviceRoot() / "Notes.txt", "x");
        std::error_code ec;
        fs::create_directories(serviceRoot() / "Folder.vfEQSQuery", ec); // a directory, not a template

        const auto infos = events::EventDispatcher::instance().query(events::ai::ListEQSQueriesQuery{});
        REQUIRE(infos.size() == 2);
        CHECK(infos[0].name == "Zeta"); // std::string order: upper case before lower case
        CHECK(infos[1].name == "alpha");
        CHECK(provider.looked == std::vector<std::string>{"Zeta", "alpha"});
    }

    TEST_CASE_FIXTURE(ServiceFixture, "a missing template folder lists nothing")
    {
        std::error_code ec;
        fs::remove_all(serviceRoot(), ec);
        CHECK(events::EventDispatcher::instance().query(events::ai::ListEQSQueriesQuery{}).empty());
        CHECK(provider.looked.empty());
    }

    TEST_CASE_FIXTURE(ServiceFixture, "submit, result, cancel, run-now and info reach the provider")
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ai::SubmitEQSQueryCommand submit;
        submit.queryName = "NearestEnemy";
        const eqs::EQSQueryHandle handle = dispatcher.execute(submit);
        CHECK(handle == provider.nextHandle);
        CHECK(provider.submitted == std::vector<std::string>{"NearestEnemy"});

        events::ai::GetEQSQueryResultQuery get;
        get.handle = handle;
        CHECK(dispatcher.query(get).status == eqs::EQSQueryStatus::Completed);

        events::ai::CancelEQSQueryCommand cancel;
        cancel.handle = handle;
        dispatcher.execute(cancel);
        CHECK(provider.released == std::vector<uint64_t>{handle.id});

        events::ai::RunEQSQueryNowQuery run;
        run.queryName = "Cover";
        CHECK(dispatcher.query(run).testNames == std::vector<std::string>{"Cover"});

        events::ai::GetEQSQueryInfoQuery info;
        info.queryName = "Cover";
        CHECK(dispatcher.query(info).name == "Cover");
    }
}
