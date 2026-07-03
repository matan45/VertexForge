#pragma once

// Shared helpers for the repo-scanning script tests (test_script_native_parity,
// test_script_listener_coverage). These tests read engine SOURCE files at test
// runtime, so they locate the repo root from the running Tests.exe path
// (bin/Tests/<Config>/x64/Tests.exe -> 4 parents up), with a current_path
// walk-up fallback for non-standard launch dirs. CPU-only, no engine linkage.

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace repo_scan
{
    namespace fs = std::filesystem;

    inline bool looksLikeRepoRoot(const fs::path& p)
    {
        return fs::exists(p / "assets" / "scripts" / "lib" / "engine") &&
               fs::exists(p / "VFEngine" / "core" / "adapters" / "api");
    }

    // Repo root or nullopt. Tries the executable's directory first, then the
    // process working directory, walking up to 8 parents from each.
    inline std::optional<fs::path> findRepoRoot()
    {
        auto walkUp = [](fs::path p) -> std::optional<fs::path> {
            for (int i = 0; i < 8 && !p.empty(); ++i)
            {
                if (looksLikeRepoRoot(p)) return p;
                auto parent = p.parent_path();
                if (parent == p) break;
                p = parent;
            }
            return std::nullopt;
        };

        char buf[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, buf, MAX_PATH) > 0)
        {
            if (auto root = walkUp(fs::path(buf).parent_path())) return root;
        }

        std::error_code ec;
        auto cwd = fs::current_path(ec);
        if (!ec)
        {
            if (auto root = walkUp(cwd)) return root;
        }

        return std::nullopt;
    }

    inline std::string readFile(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    // Strip // line comments and /* */ block comments while preserving string
    // and char literals (a name inside a string still counts; a name inside a
    // comment must not). The same lexical rules cover both C++ and mType.
    inline std::string stripComments(const std::string& src)
    {
        std::string out;
        out.reserve(src.size());

        enum class State { Code, LineComment, BlockComment, String, Char };
        State state = State::Code;

        for (size_t i = 0; i < src.size(); ++i)
        {
            char c = src[i];
            char next = (i + 1 < src.size()) ? src[i + 1] : '\0';

            switch (state)
            {
            case State::Code:
                if (c == '/' && next == '/') { state = State::LineComment; ++i; }
                else if (c == '/' && next == '*') { state = State::BlockComment; ++i; }
                else
                {
                    if (c == '"') state = State::String;
                    else if (c == '\'') state = State::Char;
                    out += c;
                }
                break;

            case State::LineComment:
                if (c == '\n') { state = State::Code; out += c; }
                break;

            case State::BlockComment:
                if (c == '*' && next == '/') { state = State::Code; ++i; }
                else if (c == '\n') out += c;  // keep line structure for diagnostics
                break;

            case State::String:
                out += c;
                if (c == '\\' && next != '\0') { out += next; ++i; }
                else if (c == '"') state = State::Code;
                break;

            case State::Char:
                out += c;
                if (c == '\\' && next != '\0') { out += next; ++i; }
                else if (c == '\'') state = State::Code;
                break;
            }
        }

        return out;
    }
}
