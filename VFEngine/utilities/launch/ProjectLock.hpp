#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// VK-1682: one editor per project. While a project is open, the editor holds an exclusive
// LockFileEx byte-range lock in <folder of the .vfproj>/.vertexforge/editor.lock.
//
// Protocol (the launcher's VK-1683 probe relies on it):
// - The lock covers 1 byte at editorLockOffset, far beyond the file's content, so the content
//   ({"pid":N}, the holder's process id) stays readable and copyable while the lock is held.
// - The file is opened with FILE_SHARE_READ | FILE_SHARE_WRITE (never DELETE) and the handle is not
//   inheritable. The OS drops the lock when the holder exits or is killed, possibly after a short
//   delay, so a reader that just saw the holder die should retry for about a second.
// - Java probes with FileChannel.tryLock(editorLockOffset, 1, true) on a READ channel: null means
//   another editor holds it. Such a probe briefly holds a shared lock, so the editor retries its own
//   lock for ~250 ms before it reports the project as held.
// - The file is never deleted.
//
// A project on read-only media (or a file system without locks) opens unprotected: acquiring then
// reports Unavailable and the caller warns instead of refusing.
//
// This header carries no Win32 types: it is exported into the plugin SDK.
namespace launch
{
    inline constexpr std::uint64_t editorLockOffset = 1ull << 32;

    struct ProjectLockAttempt;

    class ProjectLock
    {
    public:
        ProjectLock() noexcept = default;
        ~ProjectLock();

        ProjectLock(ProjectLock&& other) noexcept;
        ProjectLock& operator=(ProjectLock&& other) noexcept;
        ProjectLock(const ProjectLock&) = delete;
        ProjectLock& operator=(const ProjectLock&) = delete;

        bool isHeld() const noexcept { return handle != nullptr; }
        // The project folder whose lock this is (absolute); empty when not held.
        const std::filesystem::path& projectDirectory() const noexcept { return directory; }

        // True when the held file's identity (volume serial + file id) could be read. Without it
        // holdsFile() always answers false.
        bool hasFileIdentity() const noexcept { return identityKnown; }

        // True when `lockFile` names the very file this lock holds (same volume and file id),
        // however the path is spelled. False when not held, when the identity is unknown, or when
        // `lockFile` does not exist.
        bool holdsFile(const std::filesystem::path& lockFile) const;

        void release() noexcept;

    private:
        friend ProjectLockAttempt tryAcquireProjectLock(const std::filesystem::path& projectDirectory);

        void* handle = nullptr;  // HANDLE
        // FILE_ID_INFO (128-bit, ReFS-safe), or the 64-bit file index where that is unsupported.
        bool identityKnown = false;
        bool identityIs128 = false;
        std::uint64_t volumeSerial = 0;
        std::array<std::uint8_t, 16> fileId{};
        std::filesystem::path directory;
    };

    struct ProjectLockAttempt
    {
        enum class Status
        {
            Acquired,     // `lock` holds the project
            HeldByOther,  // another editor holds it; `ownerPid` when its pid could be read
            Unavailable   // no lock could be created (read-only media, no lock support, ...); open anyway
        };

        Status status = Status::Unavailable;
        ProjectLock lock;
        std::optional<unsigned long> ownerPid;
        std::string message;  // UTF-8, names the folder; empty when Acquired
    };

    // `projectDirectory` is the folder that holds the .vfproj. Creates `.vertexforge` (one level)
    // when missing. Never blocks for long: it retries for at most a few hundred milliseconds.
    ProjectLockAttempt tryAcquireProjectLock(const std::filesystem::path& projectDirectory);

    // Holds the lock of the project the editor has open and moves it on a project switch.
    class ProjectLockKeeper
    {
    public:
        struct Result
        {
            enum class Outcome
            {
                Kept,         // the project's folder is the one already held
                Acquired,     // the new folder is held now; the previous lock was released
                Unavailable,  // the new folder could not be locked; the previous lock was released
                HeldByOther   // another editor holds the new folder; the previous lock is still held
            };

            Outcome outcome = Outcome::Unavailable;
            std::string message;  // UTF-8; set for Unavailable and HeldByOther
            std::optional<unsigned long> ownerPid;
        };

        // `projectFile` is the .vfproj about to become the open project. A folder that was
        // Unavailable is remembered: switching to it again is Kept, without a second attempt.
        Result switchTo(const std::filesystem::path& projectFile);
        void release() noexcept;
        const ProjectLock& current() const noexcept { return lock; }

    private:
        bool isCurrentFolder(const std::filesystem::path& folder) const;

        ProjectLock lock;
        std::filesystem::path currentFolder;  // the open project's folder, locked or not; empty = none
    };
}
