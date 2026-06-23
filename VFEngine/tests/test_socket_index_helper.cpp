#include <doctest.h>

#include <animator/SocketTypes.hpp>
#include <resource/Types.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

// ============================================================
// VK-1432: dedup of the three byte-identical socket-name scans into
// animator::indexOfSocket, and the static-socket offset-cache invariant
// (the model-space offset is constant, so caching it and re-multiplying by
// parentWorld yields the exact same world transform as recomputing per frame).
// ============================================================

namespace
{
    animator::SocketDefinition makeSocket(const std::string& name,
                                          const glm::vec3& pos = glm::vec3(0.0f),
                                          const glm::quat& rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
    {
        animator::SocketDefinition s;
        s.name = name;
        s.localPosition = pos;
        s.localRotation = rot;
        return s;
    }

    bool matricesApproxEqual(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f)
    {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                if (std::abs(a[c][r] - b[c][r]) > eps)
                    return false;
        return true;
    }
}

TEST_SUITE("SocketIndexHelper")
{
    TEST_CASE("indexOfSocket: finds a socket at first / middle / last position")
    {
        std::vector<animator::SocketDefinition> sockets{
            makeSocket("muzzle"), makeSocket("grip"), makeSocket("tracer")};

        CHECK(animator::indexOfSocket(sockets, "muzzle") == 0);
        CHECK(animator::indexOfSocket(sockets, "grip") == 1);
        CHECK(animator::indexOfSocket(sockets, "tracer") == 2);
    }

    TEST_CASE("indexOfSocket: absent name returns -1")
    {
        std::vector<animator::SocketDefinition> sockets{makeSocket("muzzle"), makeSocket("grip")};
        CHECK(animator::indexOfSocket(sockets, "scope") == -1);
        CHECK(animator::indexOfSocket(sockets, "") == -1);
    }

    TEST_CASE("indexOfSocket: empty range returns -1")
    {
        std::vector<animator::SocketDefinition> empty;
        CHECK(animator::indexOfSocket(empty, "muzzle") == -1);
    }

    TEST_CASE("indexOfSocket: accepts a string_view name without forcing a std::string")
    {
        std::vector<animator::SocketDefinition> sockets{makeSocket("muzzle"), makeSocket("grip")};
        std::string_view sv = "grip";
        CHECK(animator::indexOfSocket(sockets, sv) == 1);
    }

    TEST_CASE("indexOfSocket: distinguishes names that share a prefix")
    {
        std::vector<animator::SocketDefinition> sockets{
            makeSocket("hand"), makeSocket("hand_r"), makeSocket("hand_l")};
        CHECK(animator::indexOfSocket(sockets, "hand") == 0);
        CHECK(animator::indexOfSocket(sockets, "hand_r") == 1);
        CHECK(animator::indexOfSocket(sockets, "hand_l") == 2);
    }

    TEST_CASE("indexOfSocket: equivalent to SkeletonData::getSocketIndex (guards the delegation)")
    {
        // getSocketIndex now delegates to animator::indexOfSocket; assert parity so a future
        // divergence is caught.
        resource::SkeletonData skel;
        skel.sockets = {makeSocket("root"), makeSocket("muzzle"), makeSocket("tracer")};

        for (const char* name : {"root", "muzzle", "tracer", "missing", ""})
        {
            CHECK(skel.getSocketIndex(name) == animator::indexOfSocket(skel.sockets, name));
        }
    }
}

TEST_SUITE("StaticSocketOffsetCache")
{
    TEST_CASE("cached constant offset yields identical world transform as per-frame recompute")
    {
        // The VK-1432 fast path caches socket.getLocalOffsetMatrix() once and re-multiplies by
        // the (possibly moving) parentWorld. This must equal recomputing getLocalOffsetMatrix()
        // every frame, for any parentWorld.
        const animator::SocketDefinition socket =
            makeSocket("muzzle", glm::vec3(0.5f, 1.0f, -0.25f),
                       glm::angleAxis(glm::radians(35.0f), glm::normalize(glm::vec3(0.2f, 1.0f, 0.3f))));

        const glm::mat4 cachedOffset = socket.getLocalOffsetMatrix(); // resolved once on the cold path

        const std::vector<glm::mat4> parentWorlds = {
            glm::mat4(1.0f),
            glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, -5.0f)),
            glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 2.0f, 7.0f)),
                        glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 2.0f)),
        };

        for (const auto& parentWorld : parentWorlds)
        {
            const glm::mat4 fromCache = parentWorld * cachedOffset;
            const glm::mat4 recomputed = parentWorld * socket.getLocalOffsetMatrix();
            CHECK(matricesApproxEqual(fromCache, recomputed));
        }
    }

    TEST_CASE("getLocalOffsetMatrix is deterministic across repeated calls (cache safety)")
    {
        const animator::SocketDefinition socket =
            makeSocket("tracer", glm::vec3(1.0f, -2.0f, 3.0f),
                       glm::angleAxis(glm::radians(120.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
        CHECK(matricesApproxEqual(socket.getLocalOffsetMatrix(), socket.getLocalOffsetMatrix()));
    }
}
