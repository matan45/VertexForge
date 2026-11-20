// VK-1689: component_* asset fields (ComponentBinding.hpp assetFromJson) take a path relative to the project
// working directory, the folder assets_list reports from. New projects use workingDirectory "assets", so that
// folder is no longer the .vfproj folder the asset database (and AssetRef::fromPath) resolves against. A path
// to a missing file is refused instead of being registered, and never leaves a .vfmeta behind.

#include <doctest.h>

#include "protocol/ArgReader.hpp"
#include "tools/ComponentBinding.hpp"

#include "asset/AssetDatabase.hpp"
#include "asset/AssetRef.hpp"
#include "config/ProjectConfig.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ProjectEvents.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace
{
    namespace fs = std::filesystem;

    // The project <temp>/P with one asset file, assets/models/Box.vfMesh, and the asset database rooted at P
    // (as AssetDatabaseServiceImpl::onProjectLoaded roots it at the .vfproj folder). The current project's
    // working directory is `workingDirectory`, which a test may change.
    struct ProjectFixture
    {
        fs::path temp;
        fs::path project;
        fs::path mesh;
        std::string workingDirectory;

        ProjectFixture()
        {
            clearAll();
            temp = fs::temp_directory_path() / "VertexForge_McpComponentAssetPaths";
            std::error_code ec;
            fs::remove_all(temp, ec);
            fs::create_directories(temp / "P" / "assets" / "models");
            temp = fs::weakly_canonical(temp);
            project = temp / "P";
            mesh = project / "assets" / "models" / "Box.vfMesh";
            std::ofstream(mesh, std::ios::binary) << "test";

            REQUIRE(asset::AssetDatabase::instance().rebuildFromMetaFiles(project.string()));
            workingDirectory = (project / "assets").string();
            events::EventDispatcher::instance().registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "P";
                    config.workingDirectory = workingDirectory;
                    return config;
                });
        }

        ~ProjectFixture()
        {
            clearAll();
            std::error_code ec;
            fs::remove_all(temp, ec);
        }

        ProjectFixture(const ProjectFixture&) = delete;
        ProjectFixture& operator=(const ProjectFixture&) = delete;

        static void clearAll()
        {
            events::EventDispatcher::instance().clear();
            asset::AssetDatabase::instance().clear();
        }

        static asset::AssetRef read(const std::string& text)
        {
            return mcp::tools::assetFromJson(nlohmann::json(text), "meshRef");
        }

        // True when `ref` names the fixture's Box.vfMesh.
        bool namesMesh(const asset::AssetRef& ref) const
        {
            std::error_code ec;
            return ref.isValid() && fs::equivalent(fs::path(ref.resolve()), mesh, ec);
        }
    };
}

TEST_SUITE("McpComponentAssetPaths")
{
    TEST_CASE("a relative path is relative to the working directory, not the .vfproj folder")
    {
        ProjectFixture fixture;

        const asset::AssetRef ref = ProjectFixture::read("models/Box.vfMesh");
        CHECK(fixture.namesMesh(ref));

        // The .vfproj-relative spelling names <P>/assets/assets/models/Box.vfMesh, which does not exist.
        CHECK_THROWS_AS(ProjectFixture::read("assets/models/Box.vfMesh"), mcp::ArgError);
        CHECK_FALSE(fs::exists(fixture.project / "assets" / "assets"));
    }

    TEST_CASE("absolute paths and guid: values are unchanged")
    {
        ProjectFixture fixture;

        const asset::AssetRef absolute = ProjectFixture::read(fixture.mesh.generic_string());
        CHECK(fixture.namesMesh(absolute));

        const asset::AssetRef byGuid = ProjectFixture::read("guid:" + absolute.toHexString());
        CHECK(byGuid == absolute);
        CHECK(fixture.namesMesh(byGuid));

        CHECK_FALSE(ProjectFixture::read("").isValid());
        CHECK_FALSE(mcp::tools::assetFromJson(nlohmann::json(nullptr), "meshRef").isValid());
    }

    TEST_CASE("a project whose working directory is its .vfproj folder keeps the old spelling")
    {
        ProjectFixture fixture;
        fixture.workingDirectory = fixture.project.string();

        CHECK(fixture.namesMesh(ProjectFixture::read("assets/models/Box.vfMesh")));
    }

    TEST_CASE("a missing file is refused and never registered")
    {
        ProjectFixture fixture;

        CHECK_THROWS_AS(ProjectFixture::read("models/Missing.vfMesh"), mcp::ArgError);
        CHECK_FALSE(fs::exists(fixture.project / "assets" / "models" / "Missing.vfMesh.vfmeta"));
        CHECK_FALSE(asset::AssetDatabase::instance()
                        .getGUID((fixture.project / "assets" / "models" / "Missing.vfMesh").string())
                        .has_value());
    }

    TEST_CASE("an asset the database holds still resolves after its file is deleted")
    {
        // An undo replays the get() snapshot it took; the file may be gone since.
        ProjectFixture fixture;
        const asset::AssetRef before = ProjectFixture::read("models/Box.vfMesh");
        REQUIRE(before.isValid());

        std::error_code ec;
        fs::remove(fixture.mesh, ec);
        REQUIRE_FALSE(fs::exists(fixture.mesh));

        CHECK(ProjectFixture::read("models/Box.vfMesh") == before);
        CHECK(ProjectFixture::read(fixture.mesh.generic_string()) == before);
    }
}
