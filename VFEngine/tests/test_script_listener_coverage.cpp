#include <doctest.h>

// VK-1458: listener-interface coverage. Six listener interfaces used to be
// dispatched by event bridges but were missing from the adapter's hardcoded
// probe list, so their callbacks silently never fired (IRagdollListener on the
// shipped RagdollController among them). The fix makes each bridge declare a
// kRequiredInterfaces array that ScriptingAdapter aggregates; this suite
// guards the inputs of that construction:
//
//   1. every "I*Listener"/"I*EventListener" literal in a bridge .cpp appears
//      in that bridge's declared kRequiredInterfaces (no undeclared gates);
//   2. every bridge that mentions listener interfaces declares
//      kRequiredInterfaces at all;
//   3. ScriptingAdapter.cpp aggregates every bridge's kRequiredInterfaces;
//   4. every declared interface has a matching interface definition file
//      assets/scripts/lib/engine/<Name>.mt (the IWeatherEventListener
//      reverse gap: cached + dispatched but impossible to implement).
//
// Zero allowlist. CPU-only, file-scanning — Tests.exe does not link Core.

#include "test_repo_scan_helpers.hpp"

#include <map>
#include <regex>
#include <set>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    // Every engine listener interface follows the I<Name>Listener /
    // I<Name>EventListener convention (see assets/scripts/lib/engine/I*.mt).
    std::set<std::string> extractInterfaceLiterals(const std::string& strippedText)
    {
        static const std::regex kIface(R"re("(I[A-Z][A-Za-z0-9]*Listener)")re");
        std::set<std::string> out;
        for (auto it = std::sregex_iterator(strippedText.begin(), strippedText.end(), kIface);
             it != std::sregex_iterator(); ++it)
        {
            out.insert(it->str(1));
        }
        return out;
    }
}

TEST_CASE("script listener coverage: bridge gates, aggregation, and .mt interface files")
{
    auto rootOpt = repo_scan::findRepoRoot();
    REQUIRE_MESSAGE(rootOpt.has_value(),
                    "repo root not found from Tests.exe location or CWD — "
                    "this suite must run from a checkout of the engine repo");
    const fs::path root = *rootOpt;
    const fs::path scriptingDir = root / "VFEngine" / "core" / "adapters" / "scripting";
    REQUIRE(fs::exists(scriptingDir));

    // Collect bridges: Script*EventBridge.hpp (declaring side) + .cpp (gates).
    std::map<std::string, fs::path> bridgeHeaders;   // bridge name -> hpp
    std::map<std::string, fs::path> bridgeSources;   // bridge name -> cpp
    static const std::regex kBridgeFile(R"(^(Script\w+EventBridge)\.(hpp|cpp)$)");
    for (auto& entry : fs::directory_iterator(scriptingDir))
    {
        if (!entry.is_regular_file()) continue;
        std::smatch m;
        std::string fileName = entry.path().filename().string();
        if (!std::regex_match(fileName, m, kBridgeFile)) continue;
        if (m[2] == "hpp") bridgeHeaders[m[1]] = entry.path();
        else bridgeSources[m[1]] = entry.path();
    }
    REQUIRE_MESSAGE(bridgeHeaders.size() >= 11,
                    "expected at least the 11 known event bridges, found "
                    << bridgeHeaders.size());

    const std::string adapterText = repo_scan::stripComments(
        repo_scan::readFile(scriptingDir / "ScriptingAdapter.cpp"));

    std::set<std::string> allDeclared;

    for (const auto& [bridge, hppPath] : bridgeHeaders)
    {
        const std::string hppText = repo_scan::stripComments(repo_scan::readFile(hppPath));

        const bool declaresArray = hppText.find("kRequiredInterfaces") != std::string::npos;
        const auto declared = extractInterfaceLiterals(hppText);

        std::set<std::string> gated;
        auto cppIt = bridgeSources.find(bridge);
        if (cppIt != bridgeSources.end())
        {
            gated = extractInterfaceLiterals(
                repo_scan::stripComments(repo_scan::readFile(cppIt->second)));
        }

        // (2) a bridge touching listener interfaces must declare the array
        if (!declared.empty() || !gated.empty())
        {
            CHECK_MESSAGE(declaresArray,
                          bridge << " mentions listener interfaces but declares no "
                                 << "kRequiredInterfaces array");
        }

        // (1) no gate literal outside the declared set. After the VK-1458
        // refactor the .cpp gates use the header constants, so any literal
        // that reappears in a .cpp is either a new undeclared gate (bug) or
        // must at least name a declared interface.
        for (const auto& iface : gated)
        {
            CHECK_MESSAGE(declared.count(iface) == 1,
                          bridge << ".cpp gates on \"" << iface
                                 << "\" which is missing from its kRequiredInterfaces");
        }

        // (3) adapter aggregates this bridge's declarations
        if (declaresArray)
        {
            const std::string aggregationRef = bridge + "::kRequiredInterfaces";
            CHECK_MESSAGE(adapterText.find(aggregationRef) != std::string::npos,
                          "ScriptingAdapter.cpp does not aggregate " << aggregationRef
                          << " — scripts implementing its interfaces will never be cached");
        }

        allDeclared.insert(declared.begin(), declared.end());
    }

    // Sanity: the six formerly-dead interfaces must all be declared now.
    for (const char* revived : {"IRagdollListener", "IDestructionListener",
                                "ISceneEventListener", "IWaterListener",
                                "IUIWindowListener", "IUIListViewListener"})
    {
        CHECK_MESSAGE(allDeclared.count(revived) == 1,
                      revived << " is not declared by any bridge — regression of the "
                              << "VK-1458 dead-listener fix");
    }

    // (4) every declared interface is implementable from script: a matching
    // interface definition file must exist and actually declare the interface.
    const fs::path engineLib = root / "assets" / "scripts" / "lib" / "engine";
    for (const auto& iface : allDeclared)
    {
        const fs::path mtFile = engineLib / (iface + ".mt");
        const bool exists = fs::exists(mtFile);
        CHECK_MESSAGE(exists,
                      "bridge-dispatched interface " << iface << " has no "
                      << "assets/scripts/lib/engine/" << iface
                      << ".mt — scripts cannot implement it");
        if (!exists) continue;

        const std::string mtText = repo_scan::stripComments(repo_scan::readFile(mtFile));
        CHECK_MESSAGE(mtText.find("interface " + iface) != std::string::npos,
                      mtFile.string() << " does not declare 'interface " << iface << "'");
    }
}
