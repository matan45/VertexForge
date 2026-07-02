#include <doctest.h>

// VK-1458: wrapper/native parity. Every native the .mt facade (and game
// scripts) call must be registered somewhere on the C++ side — a wrapper
// calling an unregistered native compiles fine and only explodes at runtime
// (the PostProcess TAA wrappers shipped broken for exactly this reason).
//
// Called set:     \b_native_\w+\b and \b_gas_\w+\b tokens in assets/scripts/**/*.mt
// Registered set: registerNativeFunction("...") / registerScriptFunction("...") /
//                 registerNativeMethod("...") string literals in
//                 VFEngine/core/adapters/**, VFEngine/plugin/**, plugins/**/*.cpp
//
// called ⊆ registered is a FAILURE; registered-but-unwrapped is only a
// MESSAGE (deliberately unexposed natives are legitimate). Comments are
// stripped on both sides; string literals are kept.

#include "test_repo_scan_helpers.hpp"

#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct NativeUse
    {
        std::string name;
        std::string file;   // repo-relative, for diagnostics
    };

    // Extract called-native tokens from one comment-stripped source.
    void collectCalledNatives(const std::string& text, const std::string& file,
                              std::vector<NativeUse>& out)
    {
        static const std::regex kToken(R"((_native_[A-Za-z0-9_]+|_gas_[A-Za-z0-9_]+))");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), kToken);
             it != std::sregex_iterator(); ++it)
        {
            out.push_back({it->str(1), file});
        }
    }

    // Extract registered-native names from one comment-stripped C++ source.
    void collectRegisteredNatives(const std::string& text, std::set<std::string>& out)
    {
        static const std::regex kRegister(
            R"((?:registerNativeFunction|registerScriptFunction|registerNativeMethod)\s*\(\s*"([^"]+)\")");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), kRegister);
             it != std::sregex_iterator(); ++it)
        {
            out.insert(it->str(1));
        }
    }

    std::string relPath(const fs::path& p, const fs::path& root)
    {
        std::error_code ec;
        auto rel = fs::relative(p, root, ec);
        return ec ? p.string() : rel.string();
    }
}

TEST_CASE("script native parity: extraction mechanics (self-test)")
{
    // The scanner itself must strip comments and keep strings, or the tree
    // scan below can silently go blind.
    const std::string mt =
        "// _native_in_comment()\n"
        "/* _native_in_block() */\n"
        "public static function f(): void { _native_real_call(1); }\n"
        "string s = \"_native_in_string\";\n";

    std::vector<NativeUse> uses;
    collectCalledNatives(repo_scan::stripComments(mt), "synthetic.mt", uses);

    std::set<std::string> names;
    for (const auto& u : uses) names.insert(u.name);

    CHECK(names.count("_native_real_call") == 1);
    CHECK(names.count("_native_in_comment") == 0);
    CHECK(names.count("_native_in_block") == 0);
    // Strings are kept by design (a native name in a string is suspicious
    // enough to surface).
    CHECK(names.count("_native_in_string") == 1);

    const std::string cpp =
        "// registerNativeFunction(\"_native_commented_out\", ...)\n"
        "interpreter->registerNativeFunction(\"_native_registered_a\",\n"
        "    {nullptr, ...});\n"
        "ctx->registerScriptFunction(\"_gas_registered_b\", ...);\n";

    std::set<std::string> registered;
    collectRegisteredNatives(repo_scan::stripComments(cpp), registered);

    CHECK(registered.count("_native_registered_a") == 1);
    CHECK(registered.count("_gas_registered_b") == 1);
    CHECK(registered.count("_native_commented_out") == 0);

    // The failure mode this suite exists for: called but never registered.
    std::vector<NativeUse> badUses;
    collectCalledNatives(repo_scan::stripComments(
        "PostProcess::x() { _native_postprocess_taa_isEnabled(); }"), "bad.mt", badUses);
    REQUIRE(badUses.size() == 1);
    CHECK(registered.count(badUses[0].name) == 0);
}

TEST_CASE("script native parity: every native called from .mt is registered in C++")
{
    auto rootOpt = repo_scan::findRepoRoot();
    REQUIRE_MESSAGE(rootOpt.has_value(),
                    "repo root not found from Tests.exe location or CWD — "
                    "this suite must run from a checkout of the engine repo");
    const fs::path root = *rootOpt;

    // --- called set ---
    std::vector<NativeUse> called;
    const fs::path scriptsRoot = root / "assets" / "scripts";
    for (auto it = fs::recursive_directory_iterator(scriptsRoot);
         it != fs::recursive_directory_iterator(); ++it)
    {
        if (!it->is_regular_file() || it->path().extension() != ".mt") continue;
        collectCalledNatives(repo_scan::stripComments(repo_scan::readFile(it->path())),
                             relPath(it->path(), root), called);
    }
    REQUIRE_MESSAGE(!called.empty(),
                    "no native call sites found under assets/scripts — scanner broken?");

    // --- registered set ---
    std::set<std::string> registered;
    const fs::path cppRoots[] = {
        root / "VFEngine" / "core" / "adapters",
        root / "VFEngine" / "plugin",
        root / "plugins",
    };
    for (const auto& cppRoot : cppRoots)
    {
        if (!fs::exists(cppRoot)) continue;
        for (auto it = fs::recursive_directory_iterator(cppRoot);
             it != fs::recursive_directory_iterator(); ++it)
        {
            if (!it->is_regular_file()) continue;
            auto ext = it->path().extension();
            if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
            collectRegisteredNatives(
                repo_scan::stripComments(repo_scan::readFile(it->path())), registered);
        }
    }
    REQUIRE_MESSAGE(registered.size() > 100,
                    "suspiciously few registered natives found (" << registered.size()
                    << ") — registration scanner broken?");

    // --- called ⊆ registered ---
    std::map<std::string, std::set<std::string>> missing;   // native -> files
    for (const auto& use : called)
    {
        if (registered.count(use.name) == 0)
            missing[use.name].insert(use.file);
    }

    if (!missing.empty())
    {
        std::string report = "facade methods call natives that are never registered:\n";
        for (const auto& [name, files] : missing)
        {
            report += "  " + name + "  (called from:";
            for (const auto& f : files) report += " " + f;
            report += ")\n";
        }
        FAIL_CHECK(report);
    }

    // --- OOP layer purity: wrappers under engine/oop forward through the
    // static facades, never through _native_* directly (keeps this suite's
    // coverage transitive and the drop-to-static escape hatch meaningful) ---
    const fs::path oopRoot = scriptsRoot / "lib" / "engine" / "oop";
    if (fs::exists(oopRoot))
    {
        std::vector<NativeUse> oopUses;
        for (auto it = fs::recursive_directory_iterator(oopRoot);
             it != fs::recursive_directory_iterator(); ++it)
        {
            if (!it->is_regular_file() || it->path().extension() != ".mt") continue;
            collectCalledNatives(repo_scan::stripComments(repo_scan::readFile(it->path())),
                                 relPath(it->path(), root), oopUses);
        }
        for (const auto& use : oopUses)
        {
            FAIL_CHECK("engine/oop wrapper calls a native directly: "
                       << use.name << " in " << use.file);
        }
    }

    // --- reverse direction: registered but not called anywhere in .mt.
    // Informational only — intentionally unexposed natives are fine.
    std::set<std::string> calledNames;
    for (const auto& use : called) calledNames.insert(use.name);

    std::vector<std::string> unwrapped;
    for (const auto& name : registered)
    {
        // Only engine natives follow the _native_/_gas_ convention; other
        // registered names (if any) are out of scope for the facade.
        if (name.rfind("_native_", 0) != 0 && name.rfind("_gas_", 0) != 0) continue;
        if (calledNames.count(name) == 0) unwrapped.push_back(name);
    }
    if (!unwrapped.empty())
    {
        std::string note = "registered natives with no .mt wrapper (informational): ";
        for (const auto& n : unwrapped) note += n + " ";
        MESSAGE(note);
    }
}
