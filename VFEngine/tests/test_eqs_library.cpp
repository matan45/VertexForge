#include <doctest.h>
#include <eqs/EQSQueryLibrary.hpp>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

// ============================================================
// VK-1660: eqs::EQSQueryLibrary - templates by name through an injected loader, cached until invalidated
// (valid and invalid alike), misses never cached, invalid names refused without reading.
// ============================================================

namespace
{
    constexpr const char* kValidTemplate = R"({
        "generator": {"type": "Ring", "params": {"radius": 6}},
        "tests": [{"type": "OnNavMesh", "filter": {}}]
    })";

    constexpr const char* kInvalidTemplate = R"({
        "generator": {"type": "Cone"}
    })";

    struct FakeFiles
    {
        std::unordered_map<std::string, std::string> files;
        int reads = 0;

        eqs::EQSQueryLibrary::Loader loader()
        {
            return [this](const std::string& name) -> std::optional<std::string>
            {
                ++reads;
                auto it = files.find(name);
                if (it == files.end())
                    return std::nullopt;
                return it->second;
            };
        }
    };

    bool hasErrorAt(const eqs::EQSQueryLibrary::Lookup& lookup, const std::string& path, const std::string& text)
    {
        return std::any_of(lookup.errors.begin(), lookup.errors.end(), [&](const eqs::EQSSpecError& error)
        {
            return error.path == path && error.message.find(text) != std::string::npos;
        });
    }
}

TEST_SUITE("EQSQueryLibrary")
{
    TEST_CASE("a valid template is parsed, built and cached")
    {
        FakeFiles files;
        files.files["Cover"] = kValidTemplate;
        eqs::EQSQueryLibrary library(files.loader());

        const auto first = library.lookup("Cover");
        CHECK(first.exists);
        CHECK(first.errors.empty());
        REQUIRE(first.def.get() != nullptr);
        REQUIRE(first.spec.get() != nullptr);
        CHECK(first.def->name == "Cover");
        CHECK(first.def->generator->getName() == "Ring");
        CHECK(first.spec->generator.type == "Ring");

        const auto second = library.lookup("Cover");
        CHECK(files.reads == 1);
        CHECK(second.def.get() == first.def.get()); // the same built query
    }

    TEST_CASE("an invalid template reports its errors and stays cached until invalidated")
    {
        FakeFiles files;
        files.files["Broken"] = kInvalidTemplate;
        eqs::EQSQueryLibrary library(files.loader());

        const auto broken = library.lookup("Broken");
        CHECK(broken.exists);
        CHECK(broken.def.get() == nullptr);
        CHECK(hasErrorAt(broken, "generator.type", "unknown generator type"));

        files.files["Broken"] = kValidTemplate; // fixed on disk, not yet announced
        CHECK(library.lookup("Broken").def.get() == nullptr);
        CHECK(files.reads == 1);

        library.invalidate("Broken");
        const auto fixed = library.lookup("Broken");
        CHECK(fixed.def.get() != nullptr);
        CHECK(files.reads == 2);
    }

    TEST_CASE("a missing template is not cached, so a file added later is found")
    {
        FakeFiles files;
        eqs::EQSQueryLibrary library(files.loader());

        const auto missing = library.lookup("Later");
        CHECK_FALSE(missing.exists);
        CHECK(missing.def.get() == nullptr);
        CHECK(hasErrorAt(missing, "", "no EQS query template named 'Later'"));

        files.files["Later"] = kValidTemplate;
        const auto found = library.lookup("Later");
        CHECK(found.exists);
        CHECK(found.def.get() != nullptr);
        CHECK(files.reads == 2);
    }

    TEST_CASE("invalid names are refused without reading anything")
    {
        FakeFiles files;
        eqs::EQSQueryLibrary library(files.loader());
        CHECK(hasErrorAt(library.lookup("../secret"), "", "not a valid EQS query name"));
        CHECK(hasErrorAt(library.lookup(""), "", "not a valid EQS query name"));
        CHECK(files.reads == 0);
    }

    TEST_CASE("invalidate(\"\") forgets every cached template")
    {
        FakeFiles files;
        files.files["A"] = kValidTemplate;
        files.files["B"] = kValidTemplate;
        eqs::EQSQueryLibrary library(files.loader());
        library.lookup("A");
        library.lookup("B");
        REQUIRE(files.reads == 2);

        library.invalidate("");
        library.lookup("A");
        library.lookup("B");
        CHECK(files.reads == 4);
    }

    TEST_CASE("a loader that throws is reported, never thrown")
    {
        eqs::EQSQueryLibrary library([](const std::string&) -> std::optional<std::string>
        {
            throw std::runtime_error("disk on fire");
        });
        eqs::EQSQueryLibrary::Lookup lookup;
        CHECK_NOTHROW(lookup = library.lookup("Cover"));
        CHECK(lookup.def.get() == nullptr);
        CHECK(hasErrorAt(lookup, "", "disk on fire"));
    }
}
