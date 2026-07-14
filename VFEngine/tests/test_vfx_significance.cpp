// CPU-only coverage for VK-1503 (VFX significance cap).
//
//   1. .vfVFX codec: the additive per-asset `significance` weight round-trips and the
//      saved file stamps version "1.2"; a legacy file with no `significance` key loads
//      as the neutral default 1.0.
//   2. Pure scorer + selection (VFXSignificance.hpp): the score is monotone and finite,
//      the ordering is a deterministic total order (tie-break by id, immune to input
//      order), top-N selection keeps exactly the N most significant, structurally-
//      protected candidates are never evicted, and the rank-based hysteresis cannot flap
//      inside the [N-k, N) dead-band.
//
// No graphics layer, no Vulkan device — the scorer/selection are a header-only pure core.

#include <doctest.h>

#include <vfx/VFXAsset.hpp>
#include <vfx/VFXSignificance.hpp>
#include <vfx/VFXTypes.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path sigTestRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_significance_tests";
    }

    void resetSigTestRoot()
    {
        std::error_code ec;
        fs::remove_all(sigTestRoot(), ec);
        fs::create_directories(sigTestRoot(), ec);
    }

    std::string readVersionField(const fs::path& path)
    {
        std::ifstream in(path);
        REQUIRE(in.is_open());
        json j = json::parse(in);
        return j.value("version", std::string{});
    }

    vfx::VFXData makeMinimalVFX()
    {
        vfx::VFXData data;
        data.version = vfx::VFX_FORMAT_VERSION;
        data.uuid = "sig-uuid-123";
        data.name = "SigFX";

        vfx::VFXNode emitter;
        emitter.id = 1;
        emitter.type = vfx::VFXNodeType::Emitter;
        emitter.name = "Emitter";
        data.graph.nodes.push_back(emitter);
        data.graph.nextNodeId = 2;
        return data;
    }

    // A candidate with explicit fields (avoids designated-initializer ordering pitfalls).
    vfx::VFXSignificanceCandidate cand(uint32_t id, float score, bool evictable,
                                       bool currentlyEvicted = false)
    {
        vfx::VFXSignificanceCandidate c;
        c.id = id;
        c.score = score;
        c.evictable = evictable;
        c.currentlyEvicted = currentlyEvicted;
        return c;
    }

    // Run the selection and return the set of evicted ids (order-independent view).
    std::set<uint32_t> evictedIds(std::vector<vfx::VFXSignificanceCandidate> cands, int budget,
                                  int k = vfx::kSignificanceHysteresis)
    {
        std::vector<char> out;
        vfx::selectSignificanceEvictions(cands, budget, k, out);
        std::set<uint32_t> s;
        for (size_t i = 0; i < cands.size(); ++i)
            if (out[i])
                s.insert(cands[i].id);
        return s;
    }
}

TEST_SUITE("VFXSignificanceSerialization")
{
    TEST_CASE(".vfVFX round-trips significance; stamps 1.2")
    {
        resetSigTestRoot();

        vfx::VFXData original = makeMinimalVFX();
        original.significance = 2.5f;

        const fs::path path = sigTestRoot() / "SigFX.vfVFX";
        REQUIRE(vfx::VFXAsset::save(path.string(), original));
        CHECK(readVersionField(path) == "1.2");

        auto loadedOpt = vfx::VFXAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        CHECK(loadedOpt->significance == doctest::Approx(2.5f));
    }

    TEST_CASE("a .vfVFX without a significance key loads as neutral 1.0")
    {
        resetSigTestRoot();

        // Legacy file: version/uuid/name/graph only, no significance key.
        json j;
        j["version"] = "1.1";
        j["uuid"] = "legacy-sig";
        j["name"] = "LegacySigFX";

        json emitter;
        emitter["id"] = 1;
        emitter["type"] = "Emitter";
        emitter["name"] = "Emitter";
        emitter["position"] = json::array({0.0f, 0.0f});
        emitter["properties"] = json::object();

        json graph;
        graph["nodes"] = json::array({emitter});
        graph["links"] = json::array();
        j["graph"] = graph;

        const fs::path path = sigTestRoot() / "LegacySigFX.vfVFX";
        {
            std::ofstream file(path);
            REQUIRE(file.is_open());
            file << j.dump(4);
        }

        auto loadedOpt = vfx::VFXAsset::load(path.string());
        REQUIRE(loadedOpt.has_value());
        CHECK(loadedOpt->significance == doctest::Approx(1.0f));
    }
}

TEST_SUITE("VFXSignificanceScorer")
{
    TEST_CASE("score is monotone in distance and significance, and finite at zero distance")
    {
        // Closer scores higher.
        CHECK(vfx::significanceScore(1.0f, 100.0f) < vfx::significanceScore(1.0f, 25.0f));
        // Higher significance scores higher at the same distance.
        CHECK(vfx::significanceScore(2.0f, 50.0f) > vfx::significanceScore(1.0f, 50.0f));
        // Emitter on the camera: eps floor keeps it finite (no divide-by-zero).
        CHECK(vfx::significanceScore(1.0f, 0.0f) ==
              doctest::Approx(1.0f / vfx::kSignificanceEpsilon));
    }

    TEST_CASE("non-finite inputs sanitize to 0 so the sort ordering stays total (review #9)")
    {
        // A NaN/Inf transform or camera position must not produce a NaN score: that would make
        // moreSignificant() violate std::sort's strict-weak-ordering (UB). Sanitized to 0 = least
        // significant (evicted first), and the comparator stays asymmetric against finite scores.
        const float nanScore = vfx::significanceScore(std::numeric_limits<float>::quiet_NaN(), 100.0f);
        const float infDist = vfx::significanceScore(1.0f, std::numeric_limits<float>::infinity());
        CHECK(nanScore == 0.0f);
        CHECK(infDist == 0.0f);
        CHECK(std::isfinite(nanScore));

        vfx::VFXSignificanceCandidate bad = cand(1, nanScore, true);
        vfx::VFXSignificanceCandidate good = cand(2, vfx::significanceScore(1.0f, 4.0f), true);
        CHECK(vfx::moreSignificant(good, bad));
        CHECK_FALSE(vfx::moreSignificant(bad, good));
    }

    TEST_CASE("protected candidates always outrank evictable ones regardless of score")
    {
        // Protected: tiny significance, huge distance => tiny score.
        vfx::VFXSignificanceCandidate prot = cand(1, vfx::significanceScore(0.001f, 1.0e6f), false);
        // Evictable: huge significance, on-camera => enormous score.
        vfx::VFXSignificanceCandidate loud = cand(2, vfx::significanceScore(1000.0f, 0.0f), true);

        CHECK(vfx::moreSignificant(prot, loud));
        CHECK_FALSE(vfx::moreSignificant(loud, prot));
    }
}

TEST_SUITE("VFXSignificanceSelection")
{
    TEST_CASE("keeps exactly the top-N by score; budget<=0 evicts nothing")
    {
        std::vector<vfx::VFXSignificanceCandidate> cands;
        for (uint32_t id = 1; id <= 100; ++id)
            cands.push_back(cand(id, static_cast<float>(id), /*evictable*/ true));

        // budget 32 => the 32 highest scores (ids 69..100) survive, ids 1..68 evicted.
        const std::set<uint32_t> evicted = evictedIds(cands, 32);
        CHECK(evicted.size() == 68);
        for (uint32_t id = 1; id <= 68; ++id)
            CHECK(evicted.count(id) == 1);
        for (uint32_t id = 69; id <= 100; ++id)
            CHECK(evicted.count(id) == 0);

        // Disabled: nothing evicted.
        CHECK(evictedIds(cands, 0).empty());
        CHECK(evictedIds(cands, -5).empty());
    }

    TEST_CASE("selection is deterministic under input reordering; ties break by ascending id")
    {
        // Four evictable candidates with IDENTICAL scores; budget 2 keeps the two
        // lowest ids (the id tie-break), evicts the two highest.
        std::vector<vfx::VFXSignificanceCandidate> cands = {
            cand(10, 1.0f, true), cand(20, 1.0f, true),
            cand(30, 1.0f, true), cand(40, 1.0f, true)};

        const std::set<uint32_t> expected = {30, 40};
        CHECK(evictedIds(cands, 2) == expected);

        // Same result for every permutation of the input order.
        std::mt19937 gen(12345);
        for (int trial = 0; trial < 8; ++trial)
        {
            std::shuffle(cands.begin(), cands.end(), gen);
            CHECK(evictedIds(cands, 2) == expected);
        }
    }

    TEST_CASE("protected candidates are never evicted and consume budget headroom")
    {
        // 2 protected + 5 evictable, budget 3 => evictableBudget = 3 - 2 = 1, so only the
        // single highest-score evictable survives; the other 4 are evicted. Protected are
        // never evicted even though they sit inside the budget.
        std::vector<vfx::VFXSignificanceCandidate> cands = {
            cand(1, 999.0f, false), cand(2, 0.001f, false), // protected (any score)
            cand(10, 5.0f, true), cand(20, 4.0f, true), cand(30, 3.0f, true),
            cand(40, 2.0f, true), cand(50, 1.0f, true)};

        const std::set<uint32_t> evicted = evictedIds(cands, 3);
        CHECK(evicted.count(1) == 0); // protected
        CHECK(evicted.count(2) == 0); // protected
        CHECK(evicted.count(10) == 0); // highest-score evictable survives the 1 headroom
        CHECK(evicted == std::set<uint32_t>{20, 30, 40, 50});
    }
}

TEST_SUITE("VFXSignificanceHysteresis")
{
    TEST_CASE("transition truth-table (N=32, k=4)")
    {
        const int N = 32, k = 4; // dead-band [28, 32)

        // Beyond budget -> suppress regardless of prior state.
        CHECK(vfx::nextSuppressed(false, 32, N, k) == true);
        CHECK(vfx::nextSuppressed(false, 40, N, k) == true);
        // Safely inside (rank < N-k) -> admit regardless of prior state.
        CHECK(vfx::nextSuppressed(true, 27, N, k) == false);
        CHECK(vfx::nextSuppressed(false, 0, N, k) == false);
        // Inside the dead-band [28,32) -> hold the previous state.
        CHECK(vfx::nextSuppressed(true, 28, N, k) == true);
        CHECK(vfx::nextSuppressed(false, 28, N, k) == false);
        CHECK(vfx::nextSuppressed(true, 31, N, k) == true);
        CHECK(vfx::nextSuppressed(false, 31, N, k) == false);
    }

    TEST_CASE("no oscillation: one evict and one re-admit across a full boundary sweep")
    {
        const int N = 32, k = 4;

        bool prev = false;
        int transitions = 0;
        for (int rank : {27, 28, 29, 30, 31, 32, 31, 30, 29, 28, 27, 26})
        {
            const bool next = vfx::nextSuppressed(prev, rank, N, k);
            if (next != prev)
                ++transitions;
            prev = next;
        }
        CHECK(transitions == 2); // exactly: admit->suppress at 32, suppress->admit at 27
    }

    TEST_CASE("no flap: rank jitter entirely inside the dead-band never toggles state")
    {
        const int N = 32, k = 4;

        bool prev = false;
        int transitions = 0;
        for (int rank : {28, 31, 29, 30, 28, 31, 30})
        {
            const bool next = vfx::nextSuppressed(prev, rank, N, k);
            if (next != prev)
                ++transitions;
            prev = next;
        }
        CHECK(transitions == 0); // held at its entry state (false) the whole time
    }
}
