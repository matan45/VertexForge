// VK-1657: every import in the shipped mType scripts must resolve. IVFXEventListener.mt and
// ComboCuePayload.mt imported "Vec3f.mt", which the resolver cannot find from lib/engine/, so any
// script implementing the VFX listeners failed to compile. This test reads the SOURCE scripts at
// runtime and applies the resolver's order (dependencies/mType/mType/services/
// ImportManager_Resolution.cpp): the importing file's folder, then the scripts root, then each
// search path of assets/scripts/scripts.mtproj (just "lib"). CPU-only, no engine linkage.

#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<
#include "test_repo_scan_helpers.hpp"

#include <regex>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    bool importResolves(const fs::path& scriptsRoot, const fs::path& importingFile, const std::string& target)
    {
        const fs::path relative(target);
        if (relative.is_absolute())
            return fs::exists(relative);

        return fs::exists(importingFile.parent_path() / relative) ||
               fs::exists(scriptsRoot / relative) ||
               fs::exists(scriptsRoot / "lib" / relative);
    }

    std::vector<std::string> unresolvedImports(const fs::path& scriptsRoot, const fs::path& folder)
    {
        static const std::regex importPattern(R"(import\s+[^;"]*?from\s+"([^"]+)\")");

        std::vector<std::string> failures;
        for (const auto& entry : fs::recursive_directory_iterator(folder))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".mt")
                continue;

            const std::string code = repo_scan::stripComments(repo_scan::readFile(entry.path()));
            for (std::sregex_iterator it(code.begin(), code.end(), importPattern), end; it != end; ++it)
            {
                const std::string target = (*it)[1].str();
                if (!importResolves(scriptsRoot, entry.path(), target))
                {
                    failures.push_back(fs::relative(entry.path(), scriptsRoot).generic_string() + " -> " + target);
                }
            }
        }
        return failures;
    }
}

TEST_SUITE("ScriptLibImports")
{
    TEST_CASE("every import in assets/scripts/lib and assets/scripts/game resolves")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "repo root not found from the Tests.exe path or the working directory");
        const fs::path scriptsRoot = *root / "assets" / "scripts";

        // The resolver's only search path; if this changes, update importResolves().
        const std::string project = repo_scan::readFile(scriptsRoot / "scripts.mtproj");
        REQUIRE(project.find("<SearchPath>lib</SearchPath>") != std::string::npos);

        for (const char* folder : {"lib", "game"})
        {
            CAPTURE(folder);
            const std::vector<std::string> failures = unresolvedImports(scriptsRoot, scriptsRoot / folder);
            for (const std::string& failure : failures)
            {
                MESSAGE("unresolved import: " << failure);
            }
            CHECK(failures.empty());
        }
    }
}
