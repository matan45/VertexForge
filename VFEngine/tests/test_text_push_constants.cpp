#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include <render/text/TextTypes.hpp>
#include <render/ui/UITextRenderTypes.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
// VK-1634: the text push-constant block is declared five times - once in
// render/text/TextTypes.hpp, once in render/ui/UITextRenderTypes.hpp, and once in each stage
// of resources/shaders/{text/text,ui/ui_text}.glsl. Nothing in the build reconciles them.
//
// A divergence here is silent: the pipeline layout is sized from sizeof(the C++ struct)
// (PipelineUtilities.cpp builds one range at offset 0), so a reordered or resized GLSL block
// still binds without a validation error and simply reads the wrong bytes - glyphs render
// with a nonsense mode or a nonsense band.
//
// The static_asserts in the two headers pin the C++ side at compile time. This suite pins the
// GLSL side and the correspondence between them.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    fs::path shaderRoot()
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        return *root / "resources" / "shaders";
    }

    std::string readFile(const fs::path& path)
    {
        std::ifstream in(path);
        REQUIRE_MESSAGE(in.is_open(), "missing " << path.string());
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }

    // "    uint glyphMode;   // a comment" -> "uint glyphMode"
    std::string normalizeMember(std::string line)
    {
        const size_t comment = line.find("//");
        if (comment != std::string::npos)
        {
            line.erase(comment);
        }

        std::istringstream words(line);
        std::string word;
        std::string normalized;
        while (words >> word)
        {
            if (!word.empty() && word.back() == ';')
            {
                word.pop_back();
            }
            if (word.empty())
            {
                continue;
            }
            if (!normalized.empty())
            {
                normalized += ' ';
            }
            normalized += word;
        }
        return normalized;
    }

    // Every push_constant block in the file, each as its ordered "<type> <name>" members.
    // One entry per declaration, so a stage that was edited in isolation shows up as a
    // mismatch rather than being averaged away.
    std::vector<std::vector<std::string>> pushConstantBlocks(const std::string& source)
    {
        const std::string opening = "layout(push_constant) uniform PushConstants {";
        std::vector<std::vector<std::string>> blocks;

        for (size_t at = source.find(opening); at != std::string::npos;
             at = source.find(opening, at + opening.size()))
        {
            const size_t bodyStart = at + opening.size();
            const size_t bodyEnd = source.find('}', bodyStart);
            REQUIRE(bodyEnd != std::string::npos);

            std::istringstream body(source.substr(bodyStart, bodyEnd - bodyStart));
            std::vector<std::string> members;
            std::string line;
            while (std::getline(body, line))
            {
                const std::string member = normalizeMember(line);
                if (!member.empty())
                {
                    members.push_back(member);
                }
            }
            blocks.push_back(std::move(members));
        }
        return blocks;
    }
}

TEST_SUITE("TextPushConstants")
{
    TEST_CASE("both text shaders declare the same push-constant block in every stage")
    {
        // The order is the contract: the C++ structs are memcpy'd into the range at offset 0,
        // so GLSL reads whatever sits at each offset regardless of what it calls it.
        const std::vector<std::string> expected{
            "vec2 viewportSize", "uint glyphMode", "float pxRange"};

        for (const fs::path relative : {fs::path("text") / "text.glsl",
                                        fs::path("ui") / "ui_text.glsl"})
        {
            const std::string label = relative.generic_string();
            CAPTURE(label);

            const auto blocks = pushConstantBlocks(readFile(shaderRoot() / relative));

            // Vertex and fragment are separate translation units (ShaderResource splits on
            // the stage marker before shaderc sees them), so each declares the block itself.
            REQUIRE(blocks.size() == 2);
            CHECK(blocks[0] == expected);
            CHECK(blocks[1] == expected);
        }
    }

    TEST_CASE("the C++ push-constant structs match the GLSL block byte for byte")
    {
        // vec2 at 0 (8 bytes), uint at 8 (4), float at 12 (4) - 16 total, which is what
        // TextPipelineSetup / UITextPipelineSetup register as the push-constant size.
        // These duplicate the static_asserts in the headers on purpose: the asserts fail the
        // Graphics build, this reports which field moved.
        CHECK(sizeof(render::text::TextPushConstants) == 16);
        CHECK(offsetof(render::text::TextPushConstants, viewportSize) == 0);
        CHECK(offsetof(render::text::TextPushConstants, glyphMode) == 8);
        CHECK(offsetof(render::text::TextPushConstants, pxRange) == 12);

        CHECK(sizeof(render::ui::UITextPushConstants) == 16);
        CHECK(offsetof(render::ui::UITextPushConstants, viewportSize) == 0);
        CHECK(offsetof(render::ui::UITextPushConstants, glyphMode) == 8);
        CHECK(offsetof(render::ui::UITextPushConstants, pxRange) == 12);

        // The two structs are pushed to the same shader pair, so they must agree with each
        // other as well as with the GLSL.
        CHECK(sizeof(render::text::TextPushConstants) ==
              sizeof(render::ui::UITextPushConstants));
    }
}
