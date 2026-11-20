// VK-1689: the project templates the launcher creates projects from (assets/templates/<id>/). Each holds a
// template.json (read by the launcher's TemplateManifest) and a project/ payload copied into the new project's
// folder, with {{PROJECT_NAME}} filled in: escaped as an XML attribute in a .mtproj, as an mType string in a .mt.
// The launcher then copies the engine's script library to the template's scriptLibrary folder and, per option,
// the examples or the VFX library. This test checks the templates against the engine content and materializes a
// project the way the launcher does (twins of its escapes), then checks it the way mType reads it. CPU-only.

#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include "test_repo_scan_helpers.hpp"

#include "config/Config.hpp"
#include "launch/ProjectPathRules.hpp"
#include "tools/MTypeApiDoc.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;
    namespace doc = mcp::tools::mtypedoc;

    constexpr std::string_view placeholder = "{{PROJECT_NAME}}";
    // Every character a project name may hold that an escape or a parser could trip over (the launcher's
    // ProjectNameRules forbid < > : " / \ | ? * and control characters).
    const std::string hostileName = "Tom & Jerry's {Game} $1 \xC3\xBC";

    // The option ids the launcher knows (TemplateOptionKind) and the engine.json folder each copies from.
    const std::set<std::string> knownOptions = {"examples", "vfxLibrary"};

    size_t countOf(std::string_view text, std::string_view needle)
    {
        size_t count = 0;
        for (size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + needle.size()))
        {
            ++count;
        }
        return count;
    }

    std::string replaceAll(std::string text, std::string_view needle, const std::string& value)
    {
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + value.size()))
        {
            text.replace(at, needle.size(), value);
        }
        return text;
    }

    fs::path utf8Path(const std::string& text)
    {
        return fs::path(std::u8string(text.begin(), text.end()));
    }

    // Twin of the launcher's XML attribute escape (Placeholders.xmlAttribute): & < > " only. mType's XmlParser
    // decodes no entities, so every escape it does not need would reach it as literal text.
    std::string xmlAttribute(const std::string& value)
    {
        std::string out;
        for (char c : value)
        {
            switch (c)
            {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
            }
        }
        return out;
    }

    // Twin of the launcher's mType string escape (Placeholders.mtypeString): a backslash before \ " { }, so the
    // value reads back the same in a plain "..." and in an interpolated $"..." literal.
    std::string mtypeString(const std::string& value)
    {
        std::string out;
        for (char c : value)
        {
            if (c == '\\' || c == '"' || c == '{' || c == '}')
            {
                out += '\\';
            }
            out += c;
        }
        return out;
    }

    // Twin of the mType lexer's escape decoding (Lexer::processEscapeChar): \n \t \r \\ \" \{ \} decode, any
    // other escape keeps its backslash.
    std::string decodeMTypeString(std::string_view raw)
    {
        std::string out;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] != '\\' || i + 1 >= raw.size())
            {
                out += raw[i];
                continue;
            }
            const char escaped = raw[++i];
            switch (escaped)
            {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '\\': out += '\\'; break;
            case '"': out += '"'; break;
            case '{': out += '{'; break;
            case '}': out += '}'; break;
            default:
                out += '\\';
                out += escaped;
                break;
            }
        }
        return out;
    }

    // The raw text of every "..." literal in comment-free mType code (escapes not decoded).
    std::vector<std::string> stringLiterals(const std::string& code)
    {
        std::vector<std::string> literals;
        for (size_t i = 0; i < code.size(); ++i)
        {
            if (code[i] != '"')
            {
                continue;
            }
            std::string raw;
            size_t j = i + 1;
            for (; j < code.size() && code[j] != '"'; ++j)
            {
                if (code[j] == '\\' && j + 1 < code.size())
                {
                    raw += code[j];
                    ++j;
                }
                raw += code[j];
            }
            literals.push_back(raw);
            i = j;
        }
        return literals;
    }

    // Attribute name -> value as mType's XmlParser reads them: the text between the quotes, verbatim.
    std::map<std::string, std::string> xmlAttributes(const std::string& tagText)
    {
        static const std::regex attribute(R"re(([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)")re");
        std::map<std::string, std::string> attributes;
        for (std::sregex_iterator it(tagText.begin(), tagText.end(), attribute), end; it != end; ++it)
        {
            attributes[(*it)[1].str()] = (*it)[2].str();
        }
        return attributes;
    }

    // The parts of a .mtproj the editor's script build depends on (ProjectConfigParser).
    struct MtprojShape
    {
        size_t projectTags = 0;
        size_t projectEnds = 0;
        std::map<std::string, std::string> projectAttributes;
        std::vector<std::string> includes;
        std::vector<std::string> outputDirectories;
        std::vector<std::string> searchPaths;
    };

    std::vector<std::string> elementTexts(const std::string& xml, const char* tag)
    {
        const std::regex element(std::string("<") + tag + R"(>\s*([^<]*?)\s*</)" + tag + ">");
        std::vector<std::string> texts;
        for (std::sregex_iterator it(xml.begin(), xml.end(), element), end; it != end; ++it)
        {
            texts.push_back((*it)[1].str());
        }
        return texts;
    }

    MtprojShape readMtproj(const std::string& xml)
    {
        MtprojShape shape;
        static const std::regex projectTag(R"(<Project\b([^>]*)>)");
        static const std::regex outputTag(R"(<Output\b([^>]*)/?>)");
        for (std::sregex_iterator it(xml.begin(), xml.end(), projectTag), end; it != end; ++it)
        {
            ++shape.projectTags;
            shape.projectAttributes = xmlAttributes((*it)[1].str());
        }
        shape.projectEnds = countOf(xml, "</Project>");
        shape.includes = elementTexts(xml, "Include");
        shape.searchPaths = elementTexts(xml, "SearchPath");
        for (std::sregex_iterator it(xml.begin(), xml.end(), outputTag), end; it != end; ++it)
        {
            const auto attributes = xmlAttributes((*it)[1].str());
            const auto directory = attributes.find("Directory");
            shape.outputDirectories.push_back(directory == attributes.end() ? std::string() : directory->second);
        }
        return shape;
    }

    // A project-relative path the launcher accepts: '/'-separated, normalised, no '.', '..' or root, and not in
    // the editor's state folder.
    bool isCleanRelative(const std::string& text)
    {
        if (text.empty() || text.find('\\') != std::string::npos || text.front() == '/' || text.back() == '/')
        {
            return false;
        }
        const fs::path path = utf8Path(text);
        if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        {
            return false;
        }
        for (const fs::path& part : path)
        {
            const std::string name = part.generic_string();
            if (name.empty() || name == "." || name == ".." || launch::componentEquals(part, fs::path(".vertexforge")))
            {
                return false;
            }
        }
        return true;
    }

    std::optional<std::array<uint32_t, 3>> parseVersion(const std::string& text)
    {
        static const std::regex version(R"((0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8}))");
        std::smatch match;
        if (!std::regex_match(text, match, version))
        {
            return std::nullopt;
        }
        return std::array<uint32_t, 3>{static_cast<uint32_t>(std::stoul(match[1].str())),
                                       static_cast<uint32_t>(std::stoul(match[2].str())),
                                       static_cast<uint32_t>(std::stoul(match[3].str()))};
    }

    bool isNonEmptyString(const json& object, const char* key)
    {
        return object.contains(key) && object[key].is_string() && !object[key].get<std::string>().empty();
    }

    std::string stringOf(const json& object, const char* key)
    {
        return object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : std::string();
    }

    void writeBytes(const fs::path& file, const std::string& bytes)
    {
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE_MESSAGE(out.good(), "cannot write " << file.string());
    }

    // Copies every regular file under `from` to the same relative path under `to`, byte for byte (read and
    // written, so no file attribute is carried over). `fill` may rewrite a file's bytes.
    template <typename Fill>
    size_t copyTree(const fs::path& from, const fs::path& to, Fill fill)
    {
        size_t files = 0;
        for (const auto& entry : fs::recursive_directory_iterator(from))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            const fs::path relative = fs::relative(entry.path(), from);
            writeBytes(to / relative, fill(entry.path(), repo_scan::readFile(entry.path())));
            ++files;
        }
        return files;
    }

    size_t copyTree(const fs::path& from, const fs::path& to)
    {
        return copyTree(from, to, [](const fs::path&, std::string bytes) { return bytes; });
    }

    // A unique folder under the temp directory, removed (with everything in it) when the test case ends.
    struct TempFolder
    {
        fs::path path;

        TempFolder()
        {
            static std::atomic<uint32_t> counter{0};
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("vf_project_templates_" + std::to_string(GetCurrentProcessId()) + "_" +
                                                std::to_string(ticks) + "_" + std::to_string(counter++));
            fs::create_directories(path);
        }

        ~TempFolder()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }

        TempFolder(const TempFolder&) = delete;
        TempFolder& operator=(const TempFolder&) = delete;
    };

    struct Template
    {
        fs::path folder;
        json manifest;
    };

    std::vector<Template> loadTemplates(const fs::path& templatesRoot)
    {
        std::vector<Template> templates;
        for (const auto& entry : fs::directory_iterator(templatesRoot))
        {
            if (!entry.is_directory())
            {
                continue;
            }
            const fs::path manifestPath = entry.path() / "template.json";
            REQUIRE_MESSAGE(fs::is_regular_file(manifestPath), manifestPath.string() << " is missing");
            json manifest = json::parse(repo_scan::readFile(manifestPath), nullptr, false);
            REQUIRE_MESSAGE(!manifest.is_discarded(), manifestPath.string() << " is not valid JSON");
            REQUIRE_MESSAGE(manifest.is_object(), manifestPath.string() << " is not a JSON object");
            templates.push_back({entry.path(), std::move(manifest)});
        }
        std::sort(templates.begin(), templates.end(),
                  [](const Template& a, const Template& b) { return a.folder < b.folder; });
        return templates;
    }

    std::optional<std::string> optionTarget(const json& manifest, const std::string& id)
    {
        if (!manifest.contains("options") || !manifest["options"].is_array())
        {
            return std::nullopt;
        }
        for (const json& option : manifest["options"])
        {
            if (option.is_object() && stringOf(option, "id") == id)
            {
                return stringOf(option, "target");
            }
        }
        return std::nullopt;
    }

    struct EngineContent
    {
        fs::path scriptLib;
        fs::path examples;
        fs::path vfxLibrary;
    };

    // The payload of `tmpl` filled in for `name`, as the launcher writes it into `project`.
    void writePayload(const Template& tmpl, const fs::path& project, const std::string& name)
    {
        copyTree(tmpl.folder / "project", project, [&](const fs::path& source, std::string bytes) {
            const fs::path extension = source.extension();
            if (launch::componentEquals(extension, fs::path(".mtproj")))
            {
                return replaceAll(std::move(bytes), placeholder, xmlAttribute(name));
            }
            if (launch::componentEquals(extension, fs::path(".mt")))
            {
                return replaceAll(std::move(bytes), placeholder, mtypeString(name));
            }
            return bytes;
        });
    }

    std::set<std::string> classNames(const fs::path& folder)
    {
        std::set<std::string> names;
        for (const auto& entry : fs::recursive_directory_iterator(folder))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".mt")
            {
                for (const doc::ApiType& type : doc::parseApi(repo_scan::readFile(entry.path())))
                {
                    names.insert(type.name);
                }
            }
        }
        return names;
    }

    void checkImports(const fs::path& scriptsFolder, const std::vector<std::string>& searchPaths)
    {
        for (const char* folder : {"lib", "game"})
        {
            CAPTURE(folder);
            const std::vector<std::string> failures =
                repo_scan::unresolvedImports(scriptsFolder, scriptsFolder / folder, searchPaths);
            for (const std::string& failure : failures)
            {
                MESSAGE("unresolved import: " << failure);
            }
            CHECK(failures.empty());
        }
    }
}

TEST_SUITE("ProjectTemplates")
{
    TEST_CASE("every template has a valid template.json and a project payload")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "repo root not found from the Tests.exe path or the working directory");
        const fs::path templatesRoot = *root / "assets" / "templates";
        REQUIRE_MESSAGE(fs::is_directory(templatesRoot), templatesRoot.string() << " is missing");

        const std::vector<Template> templates = loadTemplates(templatesRoot);
        const bool hasEmpty = std::any_of(templates.begin(), templates.end(),
                                          [](const Template& t) { return t.folder.filename() == "empty"; });
        CHECK_MESSAGE(hasEmpty, "the 'empty' template is the launcher's default and must exist");

        for (const Template& tmpl : templates)
        {
            const json& m = tmpl.manifest;
            INFO("template " << tmpl.folder.filename().string());

            REQUIRE(m.contains("formatVersion"));
            CHECK(m["formatVersion"].is_number_unsigned());
            CHECK(m["formatVersion"] == 1);
            CHECK(stringOf(m, "id") == tmpl.folder.filename().generic_string());
            CHECK(isNonEmptyString(m, "name"));
            CHECK(isNonEmptyString(m, "description"));

            const auto minimum = parseVersion(stringOf(m, "minimumEngineVersion"));
            REQUIRE_MESSAGE(minimum.has_value(), "minimumEngineVersion must be MAJOR.MINOR.PATCH");
            const std::array<uint32_t, 3> engine{Version::major, Version::minor, Version::patch};
            CHECK_MESSAGE(*minimum <= engine, "the template needs a newer engine than this one (" << Version::text << ")");

            const std::string workingDirectory = stringOf(m, "workingDirectory");
            const std::string startupScene = stringOf(m, "startupScene");
            const std::string scriptLibrary = stringOf(m, "scriptLibrary");
            CHECK(isCleanRelative(workingDirectory));
            CHECK(isCleanRelative(startupScene));
            CHECK(isCleanRelative(scriptLibrary));
            CHECK(launch::componentEquals(utf8Path(startupScene).extension(), fs::path(".vfScene")));
            // The editor reads <workingDirectory>/scripts/scripts.mtproj, whose SearchPath is "lib".
            CHECK(scriptLibrary == workingDirectory + "/scripts/lib");

            REQUIRE(m.contains("options"));
            REQUIRE(m["options"].is_array());
            std::set<std::string> ids;
            for (const json& option : m["options"])
            {
                REQUIRE(option.is_object());
                const std::string id = stringOf(option, "id");
                CAPTURE(id);
                CHECK_MESSAGE(knownOptions.count(id) == 1, "an option id the launcher does not know is ignored");
                CHECK_MESSAGE(ids.insert(id).second, "duplicate option id");
                CHECK(isNonEmptyString(option, "name"));
                CHECK(option.contains("description"));
                CHECK(option["description"].is_string());
                CHECK(option.contains("default"));
                CHECK(option["default"].is_boolean());
                CHECK(isCleanRelative(stringOf(option, "target")));
            }
            // The examples import "../../lib/...", so they must sit two folders below the library's parent; the
            // examples and the VFX library combos name effects as .vfproj-relative assets/vfx/library/... paths.
            if (const auto examples = optionTarget(m, "examples"))
            {
                CHECK(*examples == workingDirectory + "/scripts/game/examples");
            }
            if (const auto vfx = optionTarget(m, "vfxLibrary"))
            {
                CHECK(*vfx == "assets/vfx/library");
            }

            const fs::path payload = tmpl.folder / "project";
            REQUIRE_MESSAGE(fs::is_directory(payload), payload.string() << " is missing");
            CHECK(fs::is_regular_file(payload / utf8Path(workingDirectory) / "scripts" / "scripts.mtproj"));
            CHECK(fs::is_regular_file(payload / utf8Path(workingDirectory) / "scripts" / "game" / "Main.mt"));
            for (const auto& entry : fs::recursive_directory_iterator(payload))
            {
                CAPTURE(entry.path().string());
                CHECK_FALSE(launch::componentEquals(entry.path().extension(), fs::path(".vfproj")));
                if (entry.is_regular_file() && launch::componentEquals(entry.path().filename(), fs::path(".vflib.json")))
                {
                    FAIL_CHECK("the launcher writes .vflib.json itself");
                }
            }
        }
    }

    TEST_CASE("the project-name placeholder sits only where the launcher escapes it")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE(root.has_value());
        for (const Template& tmpl : loadTemplates(*root / "assets" / "templates"))
        {
            INFO("template " << tmpl.folder.filename().string());
            CHECK(countOf(repo_scan::readFile(tmpl.folder / "template.json"), placeholder) == 0);

            for (const auto& entry : fs::recursive_directory_iterator(tmpl.folder / "project"))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }
                CAPTURE(entry.path().string());
                const std::string text = repo_scan::readFile(entry.path());
                const size_t total = countOf(text, placeholder);
                const fs::path extension = entry.path().extension();

                if (launch::componentEquals(extension, fs::path(".mtproj")))
                {
                    // Only inside <Project Name="...">.
                    const MtprojShape shape = readMtproj(text);
                    const auto name = shape.projectAttributes.find("Name");
                    REQUIRE(name != shape.projectAttributes.end());
                    CHECK(countOf(name->second, placeholder) == total);
                }
                else if (launch::componentEquals(extension, fs::path(".mt")))
                {
                    // Only inside string literals (never in a comment or in code).
                    const std::string code = repo_scan::stripComments(text);
                    CHECK(countOf(code, placeholder) == total);
                    size_t inLiterals = 0;
                    for (const std::string& literal : stringLiterals(code))
                    {
                        inLiterals += countOf(literal, placeholder);
                    }
                    CHECK(inLiterals == total);
                }
                else
                {
                    CHECK_MESSAGE(total == 0, "the launcher fills the placeholder only in .mtproj and .mt files");
                }
            }
        }
    }

    TEST_CASE("a project made from each template builds against the engine's script library")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE(root.has_value());
        const EngineContent engine{*root / "assets" / "scripts" / "lib", *root / "assets" / "scripts" / "game" / "examples",
                                   *root / "assets" / "vfx" / "library"};
        const MtprojShape engineProject = readMtproj(repo_scan::readFile(*root / "assets" / "scripts" / "scripts.mtproj"));
        const std::set<std::string> libraryClasses = classNames(engine.scriptLib);

        for (const Template& tmpl : loadTemplates(*root / "assets" / "templates"))
        {
            INFO("template " << tmpl.folder.filename().string());
            const json& m = tmpl.manifest;
            const fs::path workingDirectory = utf8Path(stringOf(m, "workingDirectory"));

            TempFolder temp;
            const fs::path project = temp.path / utf8Path(hostileName);
            writePayload(tmpl, project, hostileName);
            REQUIRE(copyTree(engine.scriptLib, project / utf8Path(stringOf(m, "scriptLibrary"))) > 0);

            // The startup scene the editor builds on first open (VK-1687) is allowed there.
            CHECK(launch::starterSceneAllowed(project, project / workingDirectory / utf8Path(stringOf(m, "startupScene"))));

            // scripts.mtproj: well formed enough for ProjectConfigParser, the same build shape as the engine's own
            // project, and the name exactly as XmlParser reads it (entities are not decoded).
            const fs::path scripts = project / workingDirectory / "scripts";
            const MtprojShape shape = readMtproj(repo_scan::readFile(scripts / "scripts.mtproj"));
            CHECK(shape.projectTags == 1);
            CHECK(shape.projectEnds == 1);
            REQUIRE(shape.projectAttributes.count("Name") == 1);
            CHECK(shape.projectAttributes.at("Name") == "Tom &amp; Jerry's {Game} $1 \xC3\xBC");
            CHECK(shape.includes == engineProject.includes);
            CHECK(shape.outputDirectories == engineProject.outputDirectories);
            CHECK(shape.searchPaths == engineProject.searchPaths);
            REQUIRE_FALSE(shape.searchPaths.empty());

            // Main.mt: every string that held the placeholder reads back as the name itself.
            const fs::path templateMain = tmpl.folder / "project" / workingDirectory / "scripts" / "game" / "Main.mt";
            const std::vector<std::string> before = stringLiterals(repo_scan::stripComments(repo_scan::readFile(templateMain)));
            const std::string filledMain = repo_scan::readFile(scripts / "game" / "Main.mt");
            const std::vector<std::string> after = stringLiterals(repo_scan::stripComments(filledMain));
            REQUIRE(before.size() == after.size());
            size_t filled = 0;
            for (size_t i = 0; i < before.size(); ++i)
            {
                CAPTURE(before[i]);
                if (countOf(before[i], placeholder) > 0)
                {
                    ++filled;
                    CHECK(decodeMTypeString(after[i]) == replaceAll(decodeMTypeString(before[i]), placeholder, hostileName));
                }
                else
                {
                    CHECK(after[i] == before[i]);
                }
            }
            CHECK(filled > 0);
            CHECK(countOf(filledMain, placeholder) == 0);

            // Main is a Behaviour whose name no library class takes (one namespace per build, primer section 3).
            const std::vector<doc::ApiType> mainTypes = doc::parseApi(filledMain);
            REQUIRE(mainTypes.size() == 1);
            CHECK(mainTypes[0].name == "Main");
            CHECK(mainTypes[0].header.find("class Main extends Behaviour") != std::string::npos);
            CHECK(libraryClasses.count("Main") == 0);

            // Examples off: the library and Main resolve every import.
            CHECK(fs::is_regular_file(scripts / "lib" / "engine" / "oop" / "Behaviour.mt"));
            {
                INFO("examples off");
                checkImports(scripts, shape.searchPaths);
            }

            // Examples and VFX library on.
            if (const auto examples = optionTarget(m, "examples"))
            {
                REQUIRE(copyTree(engine.examples, project / utf8Path(*examples)) > 0);
                CHECK(fs::is_regular_file(project / utf8Path(*examples) / "PlayerMovement.mt"));
                INFO("examples on");
                checkImports(scripts, shape.searchPaths);

                std::set<std::string> gameClasses;
                for (const auto& entry : fs::recursive_directory_iterator(scripts / "game"))
                {
                    if (!entry.is_regular_file() || entry.path().extension() != ".mt")
                    {
                        continue;
                    }
                    for (const doc::ApiType& type : doc::parseApi(repo_scan::readFile(entry.path())))
                    {
                        CAPTURE(type.name);
                        CHECK_MESSAGE(libraryClasses.count(type.name) == 0, "a game class takes a library class name");
                        CHECK_MESSAGE(gameClasses.insert(type.name).second, "two game classes share a name");
                    }
                }
            }
            if (const auto vfx = optionTarget(m, "vfxLibrary"))
            {
                REQUIRE(copyTree(engine.vfxLibrary, project / utf8Path(*vfx)) > 0);
                // Effects are named relative to the project folder (AssetDatabase::resolveAssetPath).
                static const std::regex effectLiteral(R"re("(assets/vfx/library/[^"]+)")re");
                for (const auto& entry : fs::recursive_directory_iterator(project))
                {
                    if (!entry.is_regular_file())
                    {
                        continue;
                    }
                    const fs::path extension = entry.path().extension();
                    if (extension == ".mt")
                    {
                        const std::string code = repo_scan::stripComments(repo_scan::readFile(entry.path()));
                        for (std::sregex_iterator it(code.begin(), code.end(), effectLiteral), end; it != end; ++it)
                        {
                            CAPTURE(entry.path().filename().string());
                            CAPTURE((*it)[1].str());
                            CHECK(fs::is_regular_file(project / utf8Path((*it)[1].str())));
                        }
                    }
                    else if (extension == ".vfVFXSequence")
                    {
                        const json sequence = json::parse(repo_scan::readFile(entry.path()), nullptr, false);
                        REQUIRE_MESSAGE(!sequence.is_discarded(), entry.path().string() << " is not valid JSON");
                        REQUIRE(sequence.contains("steps"));
                        for (const json& step : sequence["steps"])
                        {
                            const std::string ref = stringOf(step, "vfxRef");
                            CAPTURE(entry.path().filename().string());
                            CAPTURE(ref);
                            CHECK(fs::is_regular_file(project / utf8Path(ref)));
                        }
                    }
                }
            }
        }
    }

    TEST_CASE("the examples the MCP primer quotes ship with the engine")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE(root.has_value());
        const fs::path examples = *root / "assets" / "scripts" / "game" / "examples";

        static const std::regex named(R"(game/examples/([A-Za-z0-9_]+)\.mt)");
        const std::string& primer = doc::primer();
        std::set<std::string> names;
        for (std::sregex_iterator it(primer.begin(), primer.end(), named), end; it != end; ++it)
        {
            names.insert((*it)[1].str());
        }
        REQUIRE_FALSE(names.empty());
        for (const std::string& name : names)
        {
            CAPTURE(name);
            CHECK(fs::is_regular_file(examples / (name + ".mt")));
        }
    }

    TEST_CASE("the engine's script library carries no launcher manifest")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE(root.has_value());
        // The launcher writes scripts/lib/.vflib.json into each new project; one in the engine library would be
        // copied as a library file and then overwritten.
        for (const auto& entry : fs::recursive_directory_iterator(*root / "assets" / "scripts" / "lib"))
        {
            CAPTURE(entry.path().string());
            CHECK_FALSE(launch::componentEquals(entry.path().filename(), fs::path(".vflib.json")));
        }
    }
}
