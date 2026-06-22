#include <doctest.h>

#include <animator/SocketTypes.hpp>
#include <resource/Types.hpp>
#include <resource/EndianUtils.hpp>
#include <resource/MeshStreamHandle.hpp>
#include <types/MeshSocketWriter.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

// ============================================================
// VK-1427: StaticMeshSocket — named sockets on non-skeletal meshes.
//
// Covers the format/storage path (a SOK2 socket block appended after the
// hasSkinning=0 marker of a *static* .vfMesh, with no skeleton) and the
// static-socket transform math (parentWorld * getLocalOffsetMatrix(), and
// the multi-level chain that must converge in one parent-first resolve).
// Skeletal-mesh sockets are unaffected — the block is identical, only the
// bytes preceding it differ.
// ============================================================

namespace
{
    constexpr uint32_t kSocketBlockMagic = 0x534F4B32; // 'SOK2'
    constexpr uint32_t kSocketBlockVersion = 2;        // version 2 stores localRotation

    // Writes a minimal, valid static .vfMesh: header (FileType::MESH, current
    // version, 0 submeshes) + hasSkinning=0 + a SOK2 block. A 0-submesh mesh
    // parses straight through to the skeleton header (parseHeader skips the
    // per-submesh meshlet/convex sections), so this exercises the real static
    // parse path without any geometry. If withSocketBlock is false the file
    // ends right after the hasSkinning byte (an "old" static mesh = EOF).
    void writeStaticMesh(const std::string& path,
                         const std::vector<animator::SocketDefinition>& sockets,
                         bool withSocketBlock = true)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        REQUIRE(f.is_open());

        // --- Mesh header (mirrors MeshStreamHandle::parseHeader) ---
        resource::endian::writeLE<uint8_t>(f, static_cast<uint8_t>(resource::FileType::MESH));
        resource::endian::writeLE<uint32_t>(f, Version::major);
        resource::endian::writeLE<uint32_t>(f, Version::minor);
        resource::endian::writeLE<uint32_t>(f, Version::patch);
        resource::endian::writeLE<uint32_t>(f, 0u); // numSubmeshes
        resource::endian::writeLE<uint32_t>(f, 0u); // compressionFlags

        // --- Skeleton header: static mesh => hasSkinning = 0 ---
        resource::endian::writeLE<uint8_t>(f, 0u);

        if (!withSocketBlock)
        {
            return; // EOF right after the marker — legacy static mesh, no sockets.
        }

        // --- SOK2 socket block (mirrors MeshSerializer::writeSocketData v2) ---
        resource::endian::writeLE<uint32_t>(f, kSocketBlockMagic);
        resource::endian::writeLE<uint32_t>(f, kSocketBlockVersion);
        resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(sockets.size()));
        for (const auto& s : sockets)
        {
            resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(s.name.size()));
            if (!s.name.empty()) f.write(s.name.data(), static_cast<std::streamsize>(s.name.size()));

            resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(s.targetBoneName.size()));
            if (!s.targetBoneName.empty())
                f.write(s.targetBoneName.data(), static_cast<std::streamsize>(s.targetBoneName.size()));

            resource::endian::writeLE<float>(f, s.localPosition.x);
            resource::endian::writeLE<float>(f, s.localPosition.y);
            resource::endian::writeLE<float>(f, s.localPosition.z);

            resource::endian::writeLE<float>(f, s.localRotation.w);
            resource::endian::writeLE<float>(f, s.localRotation.x);
            resource::endian::writeLE<float>(f, s.localRotation.y);
            resource::endian::writeLE<float>(f, s.localRotation.z);
        }
    }

    // Writes a minimal static .vfMesh whose socket block uses the *legacy v1*
    // layout: no SOK2 magic, no version, no per-socket rotation — the block begins
    // directly with the socket count (which is always < 256, so the reader can tell
    // it apart from the magic sentinel). Exercises readSocketDefinitions' legacy path
    // on a static mesh: rotation must come back identity.
    void writeStaticMeshLegacyV1(const std::string& path,
                                 const std::vector<animator::SocketDefinition>& sockets)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        REQUIRE(f.is_open());

        resource::endian::writeLE<uint8_t>(f, static_cast<uint8_t>(resource::FileType::MESH));
        resource::endian::writeLE<uint32_t>(f, Version::major);
        resource::endian::writeLE<uint32_t>(f, Version::minor);
        resource::endian::writeLE<uint32_t>(f, Version::patch);
        resource::endian::writeLE<uint32_t>(f, 0u); // numSubmeshes
        resource::endian::writeLE<uint32_t>(f, 0u); // compressionFlags
        resource::endian::writeLE<uint8_t>(f, 0u);  // hasSkinning = 0 (static)

        // Legacy v1 block: count first (no magic, no version, no rotation).
        resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(sockets.size()));
        for (const auto& s : sockets)
        {
            resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(s.name.size()));
            if (!s.name.empty()) f.write(s.name.data(), static_cast<std::streamsize>(s.name.size()));

            resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(s.targetBoneName.size()));
            if (!s.targetBoneName.empty())
                f.write(s.targetBoneName.data(), static_cast<std::streamsize>(s.targetBoneName.size()));

            resource::endian::writeLE<float>(f, s.localPosition.x);
            resource::endian::writeLE<float>(f, s.localPosition.y);
            resource::endian::writeLE<float>(f, s.localPosition.z);
            // No rotation floats in v1.
        }
    }

    // Writes a minimal *skeletal* .vfMesh: header + hasSkinning=1 + a single bone +
    // its inverse-bind-pose + the global inverse transform + a SOK2 v2 socket block.
    // Mirrors MeshStreamHandleRead's readSkeleton layout (bone hierarchy then bind
    // poses then sockets). Sockets here may name the bone so boneIndex resolves >= 0.
    void writeSkinnedMesh(const std::string& path, const std::string& boneName,
                          const std::vector<animator::SocketDefinition>& sockets)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        REQUIRE(f.is_open());

        resource::endian::writeLE<uint8_t>(f, static_cast<uint8_t>(resource::FileType::MESH));
        resource::endian::writeLE<uint32_t>(f, Version::major);
        resource::endian::writeLE<uint32_t>(f, Version::minor);
        resource::endian::writeLE<uint32_t>(f, Version::patch);
        resource::endian::writeLE<uint32_t>(f, 0u); // numSubmeshes
        resource::endian::writeLE<uint32_t>(f, 0u); // compressionFlags

        // --- Skeleton header: skeletal mesh => hasSkinning = 1, one bone ---
        resource::endian::writeLE<uint8_t>(f, 1u);
        resource::endian::writeLE<uint32_t>(f, 1u); // boneCount

        // Bone 0: name, parentIndex(-1), offsetMatrix(identity), preTransform(identity).
        resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(boneName.size()));
        f.write(boneName.data(), static_cast<std::streamsize>(boneName.size()));
        resource::endian::writeLE<int32_t>(f, -1);
        const glm::mat4 identity(1.0f);
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                resource::endian::writeLE<float>(f, identity[col][row]); // offsetMatrix
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                resource::endian::writeLE<float>(f, identity[col][row]); // preTransform

        // Inverse bind pose (1 bone) + global inverse transform (both identity).
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                resource::endian::writeLE<float>(f, identity[col][row]); // inverseBindPose[0]
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                resource::endian::writeLE<float>(f, identity[col][row]); // globalInverseTransform

        // --- SOK2 socket block (v2, identical bytes to the static case) ---
        resource::endian::writeLE<uint32_t>(f, kSocketBlockMagic);
        resource::endian::writeLE<uint32_t>(f, kSocketBlockVersion);
        resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(sockets.size()));
        for (const auto& s : sockets)
        {
            resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(s.name.size()));
            if (!s.name.empty()) f.write(s.name.data(), static_cast<std::streamsize>(s.name.size()));

            resource::endian::writeLE<uint32_t>(f, static_cast<uint32_t>(s.targetBoneName.size()));
            if (!s.targetBoneName.empty())
                f.write(s.targetBoneName.data(), static_cast<std::streamsize>(s.targetBoneName.size()));

            resource::endian::writeLE<float>(f, s.localPosition.x);
            resource::endian::writeLE<float>(f, s.localPosition.y);
            resource::endian::writeLE<float>(f, s.localPosition.z);

            resource::endian::writeLE<float>(f, s.localRotation.w);
            resource::endian::writeLE<float>(f, s.localRotation.x);
            resource::endian::writeLE<float>(f, s.localRotation.y);
            resource::endian::writeLE<float>(f, s.localRotation.z);
        }
    }

    std::string tempMeshPath(const char* tag)
    {
        auto dir = std::filesystem::temp_directory_path();
        return (dir / (std::string("vk1427_") + tag + ".vfMesh")).string();
    }

    animator::SocketDefinition makeStaticSocket(const std::string& name,
                                                glm::vec3 pos, glm::quat rot)
    {
        animator::SocketDefinition s;
        s.name = name;
        s.targetBoneName.clear(); // static socket: no bone
        s.boneIndex = -1;
        s.localPosition = pos;
        s.localRotation = rot;
        return s;
    }
}

TEST_SUITE("StaticMeshSocket")
{
    TEST_CASE("SOK2 round-trip on a skeleton-less mesh: sockets survive with boneIndex == -1")
    {
        const std::string path = tempMeshPath("roundtrip");

        std::vector<animator::SocketDefinition> sockets;
        sockets.push_back(makeStaticSocket("Muzzle", glm::vec3(0.0f, 0.1f, 2.5f),
                                           glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0))));
        sockets.push_back(makeStaticSocket("Scope", glm::vec3(0.0f, 0.4f, -0.2f), glm::quat(1, 0, 0, 0)));

        writeStaticMesh(path, sockets);

        resource::MeshesData data = resource::MeshStreamResource::loadAll(path);

        // No skeleton, but the SOK2 block is surfaced into skeleton.sockets.
        CHECK(data.hasSkinning == false);
        REQUIRE(data.skeleton.sockets.size() == 2);

        const auto& muzzle = data.skeleton.sockets[0];
        CHECK(muzzle.name == "Muzzle");
        CHECK(muzzle.boneIndex == -1);        // static: no bone resolved
        CHECK(muzzle.targetBoneName.empty());
        CHECK(muzzle.localPosition.x == doctest::Approx(0.0f));
        CHECK(muzzle.localPosition.y == doctest::Approx(0.1f));
        CHECK(muzzle.localPosition.z == doctest::Approx(2.5f));
        // Rotation survives the version-2 quaternion round-trip.
        glm::vec4 rotated = muzzle.getLocalOffsetMatrix() * glm::vec4(0, 0, 1, 1);
        CHECK(rotated.x == doctest::Approx(1.0f).epsilon(0.001));

        const auto& scope = data.skeleton.sockets[1];
        CHECK(scope.name == "Scope");
        CHECK(scope.boneIndex == -1);

        std::filesystem::remove(path);
    }

    TEST_CASE("Static mesh with no socket block still loads (backward compatible, no error)")
    {
        const std::string path = tempMeshPath("nosockets");
        writeStaticMesh(path, {}, /*withSocketBlock=*/false);

        resource::MeshesData data = resource::MeshStreamResource::loadAll(path);

        CHECK(data.hasSkinning == false);
        CHECK(data.skeleton.sockets.empty());

        std::filesystem::remove(path);
    }

    TEST_CASE("MeshSocketWriter::saveSocketsToMesh works on a static mesh (no skeleton required)")
    {
        const std::string path = tempMeshPath("editorsave");

        // Start with one socket, then overwrite via the editor save path.
        writeStaticMesh(path, { makeStaticSocket("Old", glm::vec3(1, 1, 1), glm::quat(1, 0, 0, 0)) });

        std::vector<animator::SocketDefinition> updated;
        updated.push_back(makeStaticSocket("Muzzle", glm::vec3(0.0f, 0.0f, 3.0f), glm::quat(1, 0, 0, 0)));
        updated.push_back(makeStaticSocket("Eject", glm::vec3(0.1f, 0.0f, 0.0f), glm::quat(1, 0, 0, 0)));

        // This is the path the mesh-preview Save button calls; it must not require a skeleton.
        REQUIRE(types::MeshSocketWriter::saveSocketsToMesh(path, updated) == true);

        resource::MeshesData reloaded = resource::MeshStreamResource::loadAll(path);
        REQUIRE(reloaded.skeleton.sockets.size() == 2);
        CHECK(reloaded.skeleton.sockets[0].name == "Muzzle");
        CHECK(reloaded.skeleton.sockets[0].localPosition.z == doctest::Approx(3.0f));
        CHECK(reloaded.skeleton.sockets[1].name == "Eject");
        CHECK(reloaded.skeleton.sockets[0].boneIndex == -1);

        std::filesystem::remove(path);
    }

    TEST_CASE("Static socket world transform == parentWorld * getLocalOffsetMatrix()")
    {
        // The runtime rule for a static socket (SocketAdapter::getSocketWorldTransform
        // / SocketAttachmentUpdater static branch): no bone, no animator cache.
        glm::mat4 parentWorld = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f))
                              * glm::mat4_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));

        animator::SocketDefinition muzzle =
            makeStaticSocket("Muzzle", glm::vec3(0.0f, 0.0f, 2.0f), glm::quat(1, 0, 0, 0));

        glm::mat4 socketWorld = parentWorld * muzzle.getLocalOffsetMatrix();
        glm::vec3 worldPos = glm::vec3(socketWorld[3]);

        // Parent at x=10 yawed 90deg about Y: its local +Z (the 2.0 offset) maps to world +X.
        CHECK(worldPos.x == doctest::Approx(12.0f).epsilon(0.001));
        CHECK(worldPos.y == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(worldPos.z == doctest::Approx(0.0f).epsilon(0.001));
    }

    TEST_CASE("Two-level chain converges via parent-first composition")
    {
        // soldier(skeleton hand world) -> AK47 static 'Muzzle' socket -> flash/scope.
        // Resolving parent-first means the child world is the exact composition of the
        // two offsets through the root world, with no one-frame lag.
        glm::mat4 handWorld = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f));

        // AK47 is attached to the hand socket; its world = handWorld * ak47Offset.
        animator::SocketDefinition ak47OnHand =
            makeStaticSocket("Weapon_R", glm::vec3(0.5f, 0.0f, 0.0f), glm::quat(1, 0, 0, 0));
        glm::mat4 ak47World = handWorld * ak47OnHand.getLocalOffsetMatrix();

        // The flash is attached to the AK47's static 'Muzzle' socket.
        animator::SocketDefinition muzzleOnAk47 =
            makeStaticSocket("Muzzle", glm::vec3(0.0f, 0.0f, 2.0f), glm::quat(1, 0, 0, 0));
        glm::mat4 flashWorld = ak47World * muzzleOnAk47.getLocalOffsetMatrix();
        glm::vec3 flashPos = glm::vec3(flashWorld[3]);

        // Hand(0,5,0) + ak47(+0.5 x) + muzzle(+2 z) with identity rotations.
        CHECK(flashPos.x == doctest::Approx(0.5f).epsilon(0.001));
        CHECK(flashPos.y == doctest::Approx(5.0f).epsilon(0.001));
        CHECK(flashPos.z == doctest::Approx(2.0f).epsilon(0.001));
    }

    TEST_CASE("Legacy v1 socket block (no magic, no rotation) round-trips on a static mesh")
    {
        // A pre-VK-1402 mesh that picked up a socket block before the SOK2 magic
        // existed: the block starts directly with the count, has no version word, and
        // stores no per-socket rotation. The reader must take the legacy branch
        // (firstWord != magic => version 1) and leave localRotation at identity.
        const std::string path = tempMeshPath("legacyv1");

        std::vector<animator::SocketDefinition> sockets;
        // Non-trivial position; rotation field is intentionally non-identity in memory
        // but is NOT written by the v1 writer, so it must come back as identity.
        auto s = makeStaticSocket("Muzzle", glm::vec3(0.0f, 0.0f, 2.5f),
                                  glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));
        sockets.push_back(s);

        writeStaticMeshLegacyV1(path, sockets);

        resource::MeshesData data = resource::MeshStreamResource::loadAll(path);

        CHECK(data.hasSkinning == false);
        REQUIRE(data.skeleton.sockets.size() == 1);

        const auto& muzzle = data.skeleton.sockets[0];
        CHECK(muzzle.name == "Muzzle");
        CHECK(muzzle.boneIndex == -1);
        CHECK(muzzle.localPosition.z == doctest::Approx(2.5f));

        // Legacy block carries no rotation => identity quaternion (w=1, x=y=z=0),
        // regardless of what the in-memory socket held before serialization.
        CHECK(muzzle.localRotation.w == doctest::Approx(1.0f));
        CHECK(muzzle.localRotation.x == doctest::Approx(0.0f));
        CHECK(muzzle.localRotation.y == doctest::Approx(0.0f));
        CHECK(muzzle.localRotation.z == doctest::Approx(0.0f));

        // Identity rotation => offset matrix leaves +Z on +Z (pure translation).
        glm::vec4 p = muzzle.getLocalOffsetMatrix() * glm::vec4(0, 0, 1, 1);
        CHECK(p.z == doctest::Approx(3.5f).epsilon(0.001)); // 1 + 2.5
        CHECK(std::fabs(p.x) < 1e-3f);

        std::filesystem::remove(path);
    }

    TEST_CASE("Empty SOK2 block (count == 0) loads as zero sockets on a static mesh")
    {
        // The new import path always appends a SOK2 block to a static mesh, even when
        // there are no sockets (count = 0). It must load cleanly to an empty socket
        // list — distinct from the EOF/no-block case but observably identical.
        const std::string path = tempMeshPath("emptyblock");
        writeStaticMesh(path, {}, /*withSocketBlock=*/true);

        resource::MeshesData data = resource::MeshStreamResource::loadAll(path);

        CHECK(data.hasSkinning == false);
        CHECK(data.skeleton.sockets.empty());

        // A count=0 block still records a valid socket-data offset, so the editor save
        // path can append fresh sockets onto a mesh that started with none.
        std::vector<animator::SocketDefinition> added;
        added.push_back(makeStaticSocket("Muzzle", glm::vec3(0, 0, 1), glm::quat(1, 0, 0, 0)));
        REQUIRE(types::MeshSocketWriter::saveSocketsToMesh(path, added) == true);

        resource::MeshesData reloaded = resource::MeshStreamResource::loadAll(path);
        REQUIRE(reloaded.skeleton.sockets.size() == 1);
        CHECK(reloaded.skeleton.sockets[0].name == "Muzzle");
        CHECK(reloaded.skeleton.sockets[0].boneIndex == -1);

        std::filesystem::remove(path);
    }

    TEST_CASE("Many static sockets (8) all survive with correct names, positions, boneIndex == -1")
    {
        const std::string path = tempMeshPath("many");

        std::vector<animator::SocketDefinition> sockets;
        for (int i = 0; i < 8; ++i)
        {
            // Distinct name + position per socket so ordering / data corruption shows.
            sockets.push_back(makeStaticSocket("Socket_" + std::to_string(i),
                                               glm::vec3(static_cast<float>(i),
                                                         static_cast<float>(i) * 0.5f,
                                                         static_cast<float>(i) * -2.0f),
                                               glm::quat(1, 0, 0, 0)));
        }

        writeStaticMesh(path, sockets);

        resource::MeshesData data = resource::MeshStreamResource::loadAll(path);

        CHECK(data.hasSkinning == false);
        REQUIRE(data.skeleton.sockets.size() == 8);
        for (int i = 0; i < 8; ++i)
        {
            const auto& s = data.skeleton.sockets[static_cast<size_t>(i)];
            INFO("socket index " << i);
            CHECK(s.name == "Socket_" + std::to_string(i));
            CHECK(s.boneIndex == -1);
            CHECK(s.targetBoneName.empty());
            CHECK(s.localPosition.x == doctest::Approx(static_cast<float>(i)));
            CHECK(s.localPosition.y == doctest::Approx(static_cast<float>(i) * 0.5f));
            CHECK(s.localPosition.z == doctest::Approx(static_cast<float>(i) * -2.0f));
        }

        std::filesystem::remove(path);
    }

    TEST_CASE("MeshSocketWriter on a SKELETAL mesh is unchanged (bone sockets re-resolve)")
    {
        // Guards that dropping the hasSkeletonData() gate in MeshSocketWriter did not
        // regress the skeletal save path: a skinned mesh with a real bone must still
        // round-trip through saveSocketsToMesh, preserving the skeleton and resolving
        // bone-attached sockets to their bone index by name.
        const std::string path = tempMeshPath("skeletalsave");

        // Initial skinned mesh with one bone "Hand_R" and a bone-attached socket.
        std::vector<animator::SocketDefinition> initial;
        {
            animator::SocketDefinition s;
            s.name = "OldGrip";
            s.targetBoneName = "Hand_R";
            s.localPosition = glm::vec3(0, 0, 0);
            initial.push_back(s);
        }
        writeSkinnedMesh(path, "Hand_R", initial);

        // Sanity: the initial skeletal mesh loads with one bone and resolves the socket.
        // NOTE: loadAll() does not populate MeshesData::hasSkinning (it stays at its
        // false default — see report); the real "is skeletal" signal is the loaded
        // bone list, so we assert on that.
        resource::MeshesData before = resource::MeshStreamResource::loadAll(path);
        REQUIRE(before.skeleton.bones.size() == 1);
        REQUIRE(before.skeleton.sockets.size() == 1);
        CHECK(before.skeleton.sockets[0].boneIndex == 0); // "Hand_R" resolves to bone 0

        // Overwrite via the editor save path (the same call used for static meshes).
        std::vector<animator::SocketDefinition> updated;
        {
            animator::SocketDefinition grip;
            grip.name = "Grip";
            grip.targetBoneName = "Hand_R";
            grip.localPosition = glm::vec3(0.0f, 0.1f, 0.0f);
            grip.localRotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));
            updated.push_back(grip);

            // A second, *static* socket (no bone) on the same skeletal mesh.
            updated.push_back(makeStaticSocket("Detached", glm::vec3(1, 0, 0), glm::quat(1, 0, 0, 0)));
        }
        REQUIRE(types::MeshSocketWriter::saveSocketsToMesh(path, updated) == true);

        // Reload: the skeleton (bones) must be intact and sockets re-resolved.
        resource::MeshesData after = resource::MeshStreamResource::loadAll(path);
        REQUIRE(after.skeleton.bones.size() == 1);          // skeleton preserved by prefix copy
        CHECK(after.skeleton.bones[0].name == "Hand_R");
        REQUIRE(after.skeleton.sockets.size() == 2);

        CHECK(after.skeleton.sockets[0].name == "Grip");
        CHECK(after.skeleton.sockets[0].targetBoneName == "Hand_R");
        CHECK(after.skeleton.sockets[0].boneIndex == 0);    // re-resolved from name
        CHECK(after.skeleton.sockets[0].localPosition.y == doctest::Approx(0.1f));

        CHECK(after.skeleton.sockets[1].name == "Detached");
        CHECK(after.skeleton.sockets[1].boneIndex == -1);   // static socket on a skeletal mesh

        std::filesystem::remove(path);
    }

    TEST_CASE("getLocalOffsetMatrix composition: parentWorld * offset for several parents")
    {
        // A static socket with a non-identity rotation+translation, composed against
        // a couple of different parent worlds, must equal parentWorld * offset exactly
        // (this is the rule SocketAdapter applies for a static, bone-less socket).
        animator::SocketDefinition socket =
            makeStaticSocket("Aim", glm::vec3(0.0f, 0.0f, 2.0f),
                             glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));
        const glm::mat4 offset = socket.getLocalOffsetMatrix();

        // Parent A: pure translation. socket world translation = parent + offset translation.
        {
            glm::mat4 parentA = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 4.0f, 5.0f));
            glm::mat4 world = parentA * offset;
            glm::vec3 pos(world[3]);
            CHECK(pos.x == doctest::Approx(3.0f).epsilon(0.001));
            CHECK(pos.y == doctest::Approx(4.0f).epsilon(0.001));
            CHECK(pos.z == doctest::Approx(7.0f).epsilon(0.001)); // 5 + 2

            // The socket's forward (local +Z) is yawed 90deg, so a point at local +Z of
            // the socket maps to +X in the socket frame, then offset by the parent.
            glm::vec4 fwd = world * glm::vec4(0, 0, 1, 1);
            CHECK(fwd.x == doctest::Approx(4.0f).epsilon(0.001)); // 3 + 1
            CHECK(fwd.z == doctest::Approx(7.0f).epsilon(0.001));
        }

        // Parent B: yawed 90deg about Y at x=10. The socket's +Z offset (2.0) is rotated
        // into world +X by the parent => world position x = 10 + 2.
        {
            glm::mat4 parentB = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f))
                              * glm::mat4_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));
            glm::mat4 world = parentB * offset;
            glm::vec3 pos(world[3]);
            CHECK(pos.x == doctest::Approx(12.0f).epsilon(0.001));
            CHECK(std::fabs(pos.y) < 1e-3f);
            CHECK(std::fabs(pos.z) < 1e-3f);

            // Two stacked 90deg yaws compose to 180deg: local +Z maps back to world -Z
            // (relative to the parent origin), i.e. world z = 0 - 1 = -1 plus parent.
            glm::vec4 fwd = world * glm::vec4(0, 0, 1, 1);
            CHECK(fwd.x == doctest::Approx(12.0f).epsilon(0.001)); // x unchanged by 180 about Y
            CHECK(fwd.z == doctest::Approx(-1.0f).epsilon(0.001));
        }
    }
}
