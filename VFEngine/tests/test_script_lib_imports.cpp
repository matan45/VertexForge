// VK-1657: every import in the shipped mType scripts must resolve. IVFXEventListener.mt and
// ComboCuePayload.mt imported "Vec3f.mt", which the resolver cannot find from lib/engine/, so any
// script implementing the VFX listeners failed to compile. This test reads the SOURCE scripts at
// runtime and applies the resolver's order (dependencies/mType/mType/services/
// ImportManager_Resolution.cpp): the importing file's folder, then the scripts root, then each
// search path of assets/scripts/scripts.mtproj (just "lib"). The resolver emulation lives in
// test_repo_scan_helpers.hpp (repo_scan::unresolvedImports), shared with test_project_templates (VK-1689).
// CPU-only, no engine linkage.

#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<
#include "test_repo_scan_helpers.hpp"

#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
}

TEST_SUITE("ScriptLibImports")
{
    TEST_CASE("every import in assets/scripts/lib and assets/scripts/game resolves")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "repo root not found from the Tests.exe path or the working directory");
        const fs::path scriptsRoot = *root / "assets" / "scripts";

        // The resolver's only search path; if this changes, update the search paths passed below.
        const std::string project = repo_scan::readFile(scriptsRoot / "scripts.mtproj");
        REQUIRE(project.find("<SearchPath>lib</SearchPath>") != std::string::npos);

        for (const char* folder : {"lib", "game"})
        {
            CAPTURE(folder);
            const std::vector<std::string> failures =
                repo_scan::unresolvedImports(scriptsRoot, scriptsRoot / folder, {"lib"});
            for (const std::string& failure : failures)
            {
                MESSAGE("unresolved import: " << failure);
            }
            CHECK(failures.empty());
        }
    }
}
