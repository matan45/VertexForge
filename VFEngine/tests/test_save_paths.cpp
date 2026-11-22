#include <doctest.h>

#include "events/EventDispatcher.hpp"
#include "events/project/ProjectEvents.hpp"
#include "impl/save/ConfigService.hpp"
#include "impl/save/SaveService.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// ============================================================
// VK-1684: SaveService and ConfigService keep their files beside the .vfproj (the Runtime) or in a
// state folder under it (the editor: .vertexforge, hidden from the content browser and never
// exported). GetProjectPathQuery answers the .vfproj FILE path; both services used to append to it
// ("Game.vfproj/saves"), so nothing could be written. The project folder has a Hebrew name, so the
// narrow paths run through the UTF-8 code page too.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // "שמירה" (saving), as UTF-16 and as UTF-8.
    const std::wstring kHebrewWide = L"שמירה";
    const std::string kHebrewUtf8 = "\xD7\xA9\xD7\x9E\xD7\x99\xD7\xA8\xD7\x94";

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

    // A project folder in the temp directory, answered by GetProjectPathQuery as <folder>/Game.vfproj.
    // Declare it after a DispatcherScope, which removes the handler again.
    struct TempProjectFolder
    {
        fs::path folder;
        fs::path projectFile;

        explicit TempProjectFolder(std::wstring_view name)
        {
            folder = fs::temp_directory_path() / (L"VertexForge_SavePaths_" + std::wstring(name) + L"_" + kHebrewWide);
            std::error_code ec;
            fs::remove_all(folder, ec);
            fs::create_directories(folder);
            projectFile = folder / L"Game.vfproj";

            events::EventDispatcher::instance().registerQueryHandler<events::project::GetProjectPathQuery>(
                [file = projectFile](const events::project::GetProjectPathQuery&) -> std::optional<std::string>
                {
                    return file.string();
                });
        }

        ~TempProjectFolder()
        {
            std::error_code ec;
            fs::remove_all(folder, ec);
        }
    };
}

TEST_SUITE("SavePaths")
{
    TEST_CASE("the editor's save slots live in the project's .vertexforge folder")
    {
        DispatcherScope dispatcherScope;
        TempProjectFolder project(L"EditorSlots");

        services::SaveService saves(nullptr, nullptr, fs::path(L".vertexforge"));
        REQUIRE(saves.createSlot("slot1"));

        CHECK(fs::is_directory(project.folder / L".vertexforge" / L"saves" / L"slot1"));
        CHECK_FALSE(fs::exists(project.folder / L"saves"));
        CHECK(saves.listSlots() == std::vector<std::string>{"slot1"});

        REQUIRE(saves.deleteSlot("slot1"));
        CHECK_FALSE(fs::exists(project.folder / L".vertexforge" / L"saves" / L"slot1"));
        CHECK(saves.listSlots().empty());
    }

    TEST_CASE("without a state folder the save slots live beside the .vfproj")
    {
        DispatcherScope dispatcherScope;
        TempProjectFolder project(L"RuntimeSlots");

        services::SaveService saves(nullptr, nullptr, fs::path{});
        REQUIRE(saves.createSlot("slot1"));

        CHECK(fs::is_directory(project.folder / L"saves" / L"slot1"));
        CHECK(saves.listSlots() == std::vector<std::string>{"slot1"});
    }

    TEST_CASE("without a project the save slots live in the folder the editor gave")
    {
        // VK-1732: an installed editor passes ~/.vertexforge/editor/saves, so a slot made with no project open
        // never lands in the install folder (its working directory).
        DispatcherScope dispatcherScope;
        events::EventDispatcher::instance().registerQueryHandler<events::project::GetProjectPathQuery>(
            [](const events::project::GetProjectPathQuery&) -> std::optional<std::string> { return std::nullopt; });

        const fs::path root = fs::temp_directory_path() / (L"VertexForge_SavePaths_NoProject_" + kHebrewWide);
        std::error_code ec;
        fs::remove_all(root, ec);
        const fs::path folder = root / L"saves";

        services::SaveService saves(nullptr, nullptr, fs::path(L".vertexforge"), folder);
        REQUIRE(saves.createSlot("slot1"));
        CHECK(fs::is_directory(folder / L"slot1"));
        CHECK(saves.listSlots() == std::vector<std::string>{"slot1"});
        REQUIRE(saves.deleteSlot("slot1"));
        CHECK_FALSE(fs::exists(folder / L"slot1"));

        fs::remove_all(root, ec);
    }

    TEST_CASE("the editor's config.json lives in the project's .vertexforge folder")
    {
        DispatcherScope dispatcherScope;
        TempProjectFolder project(L"EditorConfig");

        {
            services::ConfigService config{fs::path(L".vertexforge")};
            config.setInt("answer", 42);
        }

        CHECK(fs::is_regular_file(project.folder / L".vertexforge" / L"config.json"));
        CHECK_FALSE(fs::exists(project.folder / L"config.json"));

        services::ConfigService reloaded{fs::path(L".vertexforge")};
        CHECK(reloaded.getInt("answer", 0) == 42);
    }

    TEST_CASE("without a state folder config.json lives beside the .vfproj")
    {
        DispatcherScope dispatcherScope;
        TempProjectFolder project(L"RuntimeConfig");

        {
            services::ConfigService config{fs::path{}};
            config.setString("player", kHebrewUtf8);
        }

        CHECK(fs::is_regular_file(project.folder / L"config.json"));

        services::ConfigService reloaded{fs::path{}};
        CHECK(reloaded.getString("player", "") == kHebrewUtf8);
    }
}
