#include <doctest.h>

#include "launch/EditorConsole.hpp"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "test_repo_scan_helpers.hpp" // pulls <windows.h>: keep it last

// ============================================================
// VK-1683: `Editor.exe --console` mirrors the launcher's per-launch log file into a console window
// of the editor's own. The window itself needs a real console; these tests cover the CPU side: the
// classification of print/Log.hpp lines (their colour), the heap-free line assembler the mirror
// thread cuts the byte stream with, and Main.cpp's ordering.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using launch::LogLineAssembler;
    using launch::LogLineLevel;

    constexpr std::size_t capacity = LogLineAssembler::capacity;

    struct Fragment
    {
        std::string text;
        bool endsLine = false;
    };

    struct Collector
    {
        std::vector<Fragment> fragments;

        void operator()(std::string_view text, bool endsLine) { fragments.push_back({std::string(text), endsLine}); }

        // The complete lines (fragments joined up to each endsLine).
        std::vector<std::string> lines() const
        {
            std::vector<std::string> result;
            std::string current;
            for (const Fragment& fragment : fragments)
            {
                current += fragment.text;
                if (fragment.endsLine)
                {
                    result.push_back(current);
                    current.clear();
                }
            }
            return result;
        }
    };

    std::vector<std::string> linesOf(std::initializer_list<std::string_view> chunks, bool flush = false)
    {
        LogLineAssembler assembler;
        Collector collector;
        for (std::string_view chunk : chunks)
        {
            assembler.feed(chunk, collector);
        }
        if (flush)
        {
            assembler.flush(collector);
        }
        return collector.lines();
    }

    // Structural UTF-8 check: every lead byte is followed by exactly its continuation bytes.
    bool isWholeUtf8(std::string_view text)
    {
        for (std::size_t i = 0; i < text.size();)
        {
            const auto lead = static_cast<unsigned char>(text[i]);
            std::size_t length = 1;
            if (lead >= 0xF0)
            {
                length = 4;
            }
            else if (lead >= 0xE0)
            {
                length = 3;
            }
            else if (lead >= 0xC0)
            {
                length = 2;
            }
            else if (lead >= 0x80)
            {
                return false;  // a continuation byte without its lead
            }
            if (i + length > text.size())
            {
                return false;
            }
            for (std::size_t k = 1; k < length; ++k)
            {
                if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
                {
                    return false;
                }
            }
            i += length;
        }
        return true;
    }

    // Feeds `line` + '\n' and checks the fragments: they rebuild the line, only the last ends it,
    // none is longer than the capacity and none splits a UTF-8 sequence.
    std::vector<Fragment> checkedFragments(const std::string& line, std::size_t chunkSize)
    {
        LogLineAssembler assembler;
        Collector collector;
        const std::string input = line + "\n";
        for (std::size_t at = 0; at < input.size(); at += chunkSize)
        {
            assembler.feed(std::string_view(input).substr(at, chunkSize), collector);
        }
        CHECK_FALSE(assembler.hasPending());

        std::string joined;
        for (std::size_t i = 0; i < collector.fragments.size(); ++i)
        {
            const Fragment& fragment = collector.fragments[i];
            CAPTURE(i);
            CHECK(fragment.endsLine == (i + 1 == collector.fragments.size()));
            CHECK(fragment.text.size() <= capacity);
            joined += fragment.text;
        }
        CHECK(joined == line);
        return collector.fragments;
    }

    std::string compactCode(const fs::path& relative)
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        const fs::path path = *root / relative;
        REQUIRE_MESSAGE(fs::exists(path), "missing " << path.string());
        std::string code = repo_scan::stripComments(repo_scan::readFile(path));
        std::erase_if(code, [](unsigned char c) { return std::isspace(c) != 0; });
        return code;
    }
}

TEST_SUITE("EditorConsole")
{
    // ------------------------------------------------------- line classification

    TEST_CASE("every print/Log.hpp tag is recognised")
    {
        CHECK(launch::classifyLogLine("[12:34:56] TRACE: x") == LogLineLevel::Trace);
        CHECK(launch::classifyLogLine("[12:34:56] DEBUG: x") == LogLineLevel::Debug);
        CHECK(launch::classifyLogLine("[12:34:56] INFO: x") == LogLineLevel::Info);
        CHECK(launch::classifyLogLine("[12:34:56] WARNING: x") == LogLineLevel::Warning);
        CHECK(launch::classifyLogLine("[23:59:59] ERROR: Loading project C:\\Demo.vfproj threw") == LogLineLevel::Error);
        // An empty message still carries the tag.
        CHECK(launch::classifyLogLine("[00:00:00] INFO: ") == LogLineLevel::Info);
    }

    TEST_CASE("untagged lines are plain")
    {
        for (const char* line : {
                 "",
                 "hello",
                 "Editor.exe: bad arguments: unknown argument --bogus",
                 "usage: Editor.exe [project.vfproj]",
                 "2026-11-15 12:00:00.123 [info] spdlog line",  // a DLL's spdlog output
                 "[12:00:00] hello",                            // time, no tag
                 "[12:00:00] ",                                 // the time prefix only
                 "[12:00:00]",                                  // shorter than the prefix
                 "[12:00:0",
                 "[12:00:00] WARNINGS: x",                      // not exactly a tag
                 "[12:00:00] INFO:x",                           // no space after the colon
                 "[12:00:00] INFO",
                 "[12:00:00] info: x",                          // tags are upper case
                 "[12:00:00]  INFO: x",                         // two spaces
                 "[12:00:00]INFO: x",
                 " [12:00:00] INFO: x",                         // leading space
             })
        {
            CAPTURE(line);
            CHECK(launch::classifyLogLine(line) == LogLineLevel::Plain);
        }
    }

    TEST_CASE("the time prefix must be digits and separators in place")
    {
        for (const char* line : {
                 "[1a:00:00] INFO: x",
                 "[12:0b:00] INFO: x",
                 "[12:00:0c] INFO: x",
                 "[12-00-00] INFO: x",
                 "[12:00-00] INFO: x",
                 "(12:00:00] INFO: x",
                 "[12:00:00} INFO: x",
                 "[12:00:00]_INFO: x",
                 "[ 2:00:00] INFO: x",
             })
        {
            CAPTURE(line);
            CHECK(launch::classifyLogLine(line) == LogLineLevel::Plain);
        }
    }

    TEST_CASE("console colours are print/Log.hpp's")
    {
        CHECK(launch::consoleAttributeFor(LogLineLevel::Trace) == 0x08);
        CHECK(launch::consoleAttributeFor(LogLineLevel::Debug) == 0x03);
        CHECK(launch::consoleAttributeFor(LogLineLevel::Info) == 0x02);
        CHECK(launch::consoleAttributeFor(LogLineLevel::Warning) == 0x06);
        CHECK(launch::consoleAttributeFor(LogLineLevel::Error) == 0x04);
        CHECK(launch::consoleAttributeFor(LogLineLevel::Plain) == 0);  // leave the colour alone
        CHECK(launch::consoleResetAttribute == 0x0F);
    }

    // ------------------------------------------------------------ line assembly

    TEST_CASE("a line split across chunks comes out whole")
    {
        LogLineAssembler assembler;
        Collector collector;
        assembler.feed("[12:00:00] IN", collector);
        assembler.feed("FO: hel", collector);
        CHECK(collector.fragments.empty());
        CHECK(assembler.hasPending());
        assembler.feed("lo\n", collector);
        REQUIRE(collector.fragments.size() == 1);
        CHECK(collector.fragments[0].text == "[12:00:00] INFO: hello");
        CHECK(collector.fragments[0].endsLine);
        CHECK_FALSE(assembler.hasPending());
    }

    TEST_CASE("several lines in one chunk")
    {
        CHECK(linesOf({"a\nbb\nccc\n"}) == std::vector<std::string>{"a", "bb", "ccc"});
    }

    TEST_CASE("CRLF and bare LF both end a line; only the CR before the LF is dropped")
    {
        CHECK(linesOf({"one\r\ntwo\nthree\r\n"}) == std::vector<std::string>{"one", "two", "three"});
        CHECK(linesOf({"one\r", "\ntwo\n"}) == std::vector<std::string>{"one", "two"});
        CHECK(linesOf({"a\rb\n"}) == std::vector<std::string>{"a\rb"});
        CHECK(linesOf({"a\r\r\n"}) == std::vector<std::string>{"a\r"});
    }

    TEST_CASE("empty lines come out empty")
    {
        CHECK(linesOf({"\n\r\n\n"}) == std::vector<std::string>{"", "", ""});
        CHECK(linesOf({"a\n\nb\n"}) == std::vector<std::string>{"a", "", "b"});
    }

    TEST_CASE("an empty chunk changes nothing")
    {
        LogLineAssembler assembler;
        Collector collector;
        assembler.feed("", collector);
        CHECK_FALSE(assembler.hasPending());
        assembler.feed("x", collector);
        assembler.feed("", collector);
        CHECK(collector.fragments.empty());
        CHECK(assembler.hasPending());
    }

    TEST_CASE("a partial line waits for its newline or for flush")
    {
        LogLineAssembler assembler;
        Collector collector;
        assembler.flush(collector);  // nothing pending: nothing emitted
        CHECK(collector.fragments.empty());

        assembler.feed("done\npartial", collector);
        REQUIRE(collector.fragments.size() == 1);
        CHECK(assembler.hasPending());

        assembler.flush(collector);
        REQUIRE(collector.fragments.size() == 2);
        CHECK(collector.fragments[1].text == "partial");
        CHECK(collector.fragments[1].endsLine);
        CHECK_FALSE(assembler.hasPending());

        assembler.flush(collector);
        CHECK(collector.fragments.size() == 2);

        // A CR left at the end of the stream is dropped like the one before a LF.
        CHECK(linesOf({"x\r"}, true) == std::vector<std::string>{"x"});
    }

    TEST_CASE("byte by byte gives the same lines as one chunk")
    {
        const std::string text = "[12:00:00] INFO: a\r\nplain\n\n[12:00:01] ERROR: b\n";
        LogLineAssembler assembler;
        Collector collector;
        for (const char c : text)
        {
            assembler.feed(std::string_view(&c, 1), collector);
        }
        CHECK(collector.lines() == linesOf({text}));
        CHECK(collector.fragments.size() == 4);
    }

    TEST_CASE("a line of exactly the capacity is one fragment")
    {
        const auto fragments = checkedFragments(std::string(capacity, 'a'), 1000);
        REQUIRE(fragments.size() == 1);
        CHECK(fragments[0].text.size() == capacity);
    }

    TEST_CASE("a longer line comes out as fragments that rebuild it")
    {
        std::string line;
        for (std::size_t i = 0; i < 2 * capacity + 100; ++i)
        {
            line += static_cast<char>('a' + i % 26);
        }
        for (const std::size_t chunk : {std::size_t{1}, std::size_t{1000}, capacity, 3 * capacity})
        {
            CAPTURE(chunk);
            const auto fragments = checkedFragments(line, chunk);
            REQUIRE(fragments.size() == 3);
            CHECK(fragments[0].text.size() == capacity);
            CHECK(fragments[1].text.size() == capacity);
            CHECK(fragments[2].text.size() == 100);
        }

        const auto oneOver = checkedFragments(std::string(capacity + 1, 'z'), 4096);
        REQUIRE(oneOver.size() == 2);
        CHECK(oneOver[1].text == "z");
    }

    TEST_CASE("a UTF-8 character at the capacity boundary is not split")
    {
        // A 2-byte character whose lead byte is the buffer's last byte.
        {
            const std::string line = std::string(capacity - 1, 'a') + "\xC3\xA9" "b";
            const auto fragments = checkedFragments(line, 512);
            REQUIRE(fragments.size() == 2);
            CHECK(fragments[0].text == std::string(capacity - 1, 'a'));
            CHECK(fragments[1].text == "\xC3\xA9" "b");
        }
        // A 3-byte character cut after its second byte.
        {
            const std::string line = std::string(capacity - 2, 'a') + "\xE2\x82\xAC";
            const auto fragments = checkedFragments(line, 512);
            REQUIRE(fragments.size() == 2);
            CHECK(fragments[0].text.size() == capacity - 2);
            CHECK(fragments[1].text == "\xE2\x82\xAC");
        }
        // A 4-byte character cut after its third byte.
        {
            const std::string line = std::string(capacity - 3, 'a') + "\xF0\x9F\x98\x80" "!";
            const auto fragments = checkedFragments(line, 512);
            REQUIRE(fragments.size() == 2);
            CHECK(fragments[0].text.size() == capacity - 3);
            CHECK(fragments[1].text == "\xF0\x9F\x98\x80" "!");
        }
        // A character that ends exactly at the boundary is complete: nothing is held back.
        {
            const std::string line = std::string(capacity - 2, 'a') + "\xC3\xA9" "b";
            const auto fragments = checkedFragments(line, 512);
            REQUIRE(fragments.size() == 2);
            CHECK(fragments[0].text.size() == capacity);
            CHECK(fragments[1].text == "b");
        }
    }

    TEST_CASE("no fragment splits a character, wherever the boundary falls")
    {
        for (const std::string_view character : {std::string_view("\xC3\xA9"), std::string_view("\xE2\x82\xAC"),
                                                  std::string_view("\xF0\x9F\x98\x80")})
        {
            for (std::size_t offset = 0; offset < 4; ++offset)
            {
                CAPTURE(character.size());
                CAPTURE(offset);
                std::string line(offset, 'a');
                while (line.size() < 3 * capacity)
                {
                    line += character;
                }
                for (const Fragment& fragment : checkedFragments(line, 777))
                {
                    CHECK(isWholeUtf8(fragment.text));
                }
            }
        }
    }

    TEST_CASE("bytes that are not UTF-8 are never held back for long")
    {
        // 0xE9 is 'e acute' in Windows-1252, and looks like a 3-byte lead byte to a UTF-8 reader.
        const std::string line(2 * capacity + 10, '\xE9');
        const auto fragments = checkedFragments(line, 4096);
        REQUIRE(fragments.size() >= 2);
        for (std::size_t i = 0; i + 1 < fragments.size(); ++i)
        {
            CAPTURE(i);
            CHECK(fragments[i].text.size() >= capacity - 3);
        }
    }

    // ------------------------------------------------------------------ console

    TEST_CASE("an EditorConsole that was never opened does nothing")
    {
        launch::EditorConsole console;  // no console call: open() is what touches the process
        CHECK(true);
    }

    TEST_CASE("Main unbuffers the streams first and keeps the console to the very end")
    {
        const std::string source = compactCode(fs::path("VFEngine") / "editor" / "run" / "Main.cpp");
        // The first statement of main(), before the crash handler and the log file.
        CHECK(source.find("intmain(intargc,char*argv[]){launch::prepareStandardStreams();") != std::string::npos);

        // Declared before the project lock and the editor, so it is destroyed after both.
        const std::size_t console = source.find("launch::EditorConsoleeditorConsole;");
        const std::size_t lock = source.find("launch::ProjectLockKeeperprojectLock;");
        const std::size_t editor = source.find("handlers::EditorHandlereditorHandler;");
        REQUIRE(console != std::string::npos);
        REQUIRE(lock != std::string::npos);
        REQUIRE(editor != std::string::npos);
        CHECK(console < lock);
        CHECK(console < editor);
        CHECK(source.find("if(args.console)") != std::string::npos);
    }
}
