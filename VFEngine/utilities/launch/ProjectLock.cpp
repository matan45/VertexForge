#include "ProjectLock.hpp"

#include "EditorStateDirectory.hpp"
#include "Utf8Path.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <system_error>
#include <utility>

namespace launch
{
    namespace
    {
        // An antivirus scanner or an indexer can hold the file briefly without FILE_SHARE_WRITE.
        constexpr int kOpenRetries = 4;
        constexpr DWORD kOpenRetryDelayMs = 50;

        // The launcher's probe (a shared lock on the same byte) can overlap an editor that is
        // starting; ride it out before reporting the project as held.
        constexpr int kLockRetries = 5;
        constexpr DWORD kLockRetryDelayMs = 50;

        OVERLAPPED lockRegion() noexcept
        {
            OVERLAPPED region{};
            region.Offset = static_cast<DWORD>(editorLockOffset & 0xFFFFFFFFull);
            region.OffsetHigh = static_cast<DWORD>(editorLockOffset >> 32);
            return region;
        }

        std::string windowsErrorText(DWORD error)
        {
            std::string text = "Windows error " + std::to_string(error);
            switch (error)
            {
                case ERROR_ACCESS_DENIED:    text += ": access denied"; break;
                case ERROR_WRITE_PROTECT:    text += ": the media is write-protected"; break;
                case ERROR_PATH_NOT_FOUND:   text += ": path not found"; break;
                case ERROR_NOT_SUPPORTED:
                case ERROR_INVALID_FUNCTION: text += ": the file system does not support locks"; break;
                default: break;
            }
            return text;
        }

        std::optional<unsigned long> readOwnerPid(HANDLE file) noexcept
        {
            LARGE_INTEGER start{};
            if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN))
            {
                return std::nullopt;
            }

            char content[64] = {};
            DWORD bytesRead = 0;
            if (!ReadFile(file, content, sizeof(content) - 1, &bytesRead, nullptr))
            {
                return std::nullopt;
            }

            const std::string_view text(content, bytesRead);
            constexpr std::string_view key = "\"pid\":";
            const std::size_t at = text.find(key);
            if (at == std::string_view::npos)
            {
                return std::nullopt;
            }

            const char* first = text.data() + at + key.size();
            const char* last = text.data() + text.size();
            unsigned long pid = 0;
            auto [ptr, ec] = std::from_chars(first, last, pid);
            if (ec != std::errc{} || ptr == first)
            {
                return std::nullopt;
            }
            return pid;
        }

        // Diagnostics only: a failure here leaves the lock held and correct.
        void writeOwnerPid(HANDLE file) noexcept
        {
            LARGE_INTEGER start{};
            if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN) || !SetEndOfFile(file))
            {
                return;
            }

            char content[48];
            const int length = std::snprintf(content, sizeof(content), "{\"pid\":%lu}\n", GetCurrentProcessId());
            if (length <= 0 || length >= static_cast<int>(sizeof(content)))
            {
                return;
            }
            DWORD written = 0;
            WriteFile(file, content, static_cast<DWORD>(length), &written, nullptr);
        }

        struct FileIdentity
        {
            bool known = false;
            bool is128 = false;
            std::uint64_t volume = 0;
            std::array<std::uint8_t, 16> id{};
        };

        // FILE_ID_INFO first: the 64-bit file index of BY_HANDLE_FILE_INFORMATION is truncated on
        // ReFS. An all-zero id means the file system has none to give (some network redirectors).
        FileIdentity identityOf(HANDLE file) noexcept
        {
            FileIdentity identity;
            FILE_ID_INFO idInfo{};
            if (GetFileInformationByHandleEx(file, FileIdInfo, &idInfo, sizeof(idInfo)))
            {
                static_assert(sizeof(idInfo.FileId.Identifier) == sizeof(identity.id));
                std::memcpy(identity.id.data(), idInfo.FileId.Identifier, identity.id.size());
                identity.volume = idInfo.VolumeSerialNumber;
                identity.is128 = true;
            }
            else
            {
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(file, &info))
                {
                    return identity;
                }
                const std::uint64_t index = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
                std::memcpy(identity.id.data(), &index, sizeof(index));
                identity.volume = info.dwVolumeSerialNumber;
            }
            identity.known = std::any_of(identity.id.begin(), identity.id.end(),
                                         [](std::uint8_t byte) { return byte != 0; });
            return identity;
        }

        bool sameFolderText(const std::filesystem::path& left, const std::filesystem::path& right) noexcept
        {
            const std::wstring& a = left.native();
            const std::wstring& b = right.native();
            if (a.empty() || b.empty())
            {
                return false;
            }
            return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()),
                                        TRUE) == CSTR_EQUAL;
        }
    }

    ProjectLock::~ProjectLock()
    {
        release();
    }

    ProjectLock::ProjectLock(ProjectLock&& other) noexcept
        : handle(std::exchange(other.handle, nullptr))
        , identityKnown(std::exchange(other.identityKnown, false))
        , identityIs128(std::exchange(other.identityIs128, false))
        , volumeSerial(std::exchange(other.volumeSerial, 0))
        , fileId(std::exchange(other.fileId, {}))
        , directory(std::move(other.directory))
    {
        other.directory.clear();
    }

    ProjectLock& ProjectLock::operator=(ProjectLock&& other) noexcept
    {
        if (this != &other)
        {
            release();
            handle = std::exchange(other.handle, nullptr);
            identityKnown = std::exchange(other.identityKnown, false);
            identityIs128 = std::exchange(other.identityIs128, false);
            volumeSerial = std::exchange(other.volumeSerial, 0);
            fileId = std::exchange(other.fileId, {});
            directory = std::move(other.directory);
            other.directory.clear();
        }
        return *this;
    }

    bool ProjectLock::holdsFile(const std::filesystem::path& lockFile) const
    {
        if (!handle || !identityKnown)
        {
            return false;
        }

        // Attribute access only: it never conflicts with the share mode of the held handle.
        HANDLE file = CreateFileW(lockFile.c_str(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        const FileIdentity candidate = identityOf(file);
        CloseHandle(file);
        return candidate.known && candidate.is128 == identityIs128 && candidate.volume == volumeSerial &&
               candidate.id == fileId;
    }

    void ProjectLock::release() noexcept
    {
        if (!handle)
        {
            return;
        }

        HANDLE file = static_cast<HANDLE>(handle);
        OVERLAPPED region = lockRegion();
        UnlockFileEx(file, 0, 1, 0, &region);
        CloseHandle(file);

        handle = nullptr;
        identityKnown = false;
        identityIs128 = false;
        volumeSerial = 0;
        fileId = {};
        directory.clear();
    }

    ProjectLockAttempt tryAcquireProjectLock(const std::filesystem::path& projectDirectory)
    {
        ProjectLockAttempt attempt;

        std::error_code ec;
        std::filesystem::path folder = std::filesystem::absolute(projectDirectory, ec);
        if (ec)
        {
            attempt.message = "cannot resolve the project folder " + pathToUtf8(projectDirectory);
            return attempt;
        }
        folder = folder.lexically_normal();

        // One level only: the project folder itself must already exist.
        const std::filesystem::path stateFolder = folder / editorStateDirName;
        if (!CreateDirectoryW(stateFolder.c_str(), nullptr))
        {
            const DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS)
            {
                attempt.message = "cannot create " + pathToUtf8(stateFolder) + " (" + windowsErrorText(error) + ")";
                return attempt;
            }
        }

        // nullptr security attributes: the handle is not inherited, so a child process can never
        // keep the lock alive after the editor exits. No FILE_SHARE_DELETE: while an editor holds
        // the file it cannot be deleted, which would let a second editor lock a fresh one.
        const std::filesystem::path lockPath = stateFolder / editorLockFileName;
        HANDLE file = INVALID_HANDLE_VALUE;
        for (int attemptIndex = 0;; ++attemptIndex)
        {
            file = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                break;
            }
            const DWORD error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION || attemptIndex >= kOpenRetries)
            {
                attempt.message = "cannot open " + pathToUtf8(lockPath) + " (" + windowsErrorText(error) + ")";
                return attempt;
            }
            Sleep(kOpenRetryDelayMs);
        }

        for (int attemptIndex = 0;; ++attemptIndex)
        {
            OVERLAPPED region = lockRegion();
            if (LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &region))
            {
                break;
            }

            const DWORD error = GetLastError();
            if (error == ERROR_LOCK_VIOLATION && attemptIndex < kLockRetries)
            {
                Sleep(kLockRetryDelayMs);
                continue;
            }

            if (error == ERROR_LOCK_VIOLATION)
            {
                attempt.status = ProjectLockAttempt::Status::HeldByOther;
                attempt.ownerPid = readOwnerPid(file);
                attempt.message = pathToUtf8(folder) + " is already open in another editor";
                if (attempt.ownerPid)
                {
                    attempt.message += " (pid " + std::to_string(*attempt.ownerPid) + ")";
                }
            }
            else
            {
                attempt.message = "cannot lock " + pathToUtf8(lockPath) + " (" + windowsErrorText(error) + ")";
            }
            CloseHandle(file);
            return attempt;
        }

        writeOwnerPid(file);

        const FileIdentity identity = identityOf(file);
        attempt.lock.identityKnown = identity.known;
        attempt.lock.identityIs128 = identity.is128;
        attempt.lock.volumeSerial = identity.volume;
        attempt.lock.fileId = identity.id;
        attempt.lock.handle = file;
        attempt.lock.directory = folder;
        attempt.status = ProjectLockAttempt::Status::Acquired;
        return attempt;
    }

    ProjectLockKeeper::Result ProjectLockKeeper::switchTo(const std::filesystem::path& projectFile)
    {
        Result result;

        std::error_code ec;
        const std::filesystem::path file = std::filesystem::absolute(projectFile, ec);
        if (ec)
        {
            release();
            result.outcome = Result::Outcome::Unavailable;
            result.message = "cannot resolve the project path " + pathToUtf8(projectFile);
            return result;
        }
        const std::filesystem::path folder = file.lexically_normal().parent_path();

        // Re-acquiring the folder already held would fail against our own handle (locks belong to
        // the handle), and retrying an unlockable folder would only warn twice.
        if (isCurrentFolder(folder))
        {
            result.outcome = Result::Outcome::Kept;
            return result;
        }

        ProjectLockAttempt attempt = tryAcquireProjectLock(folder);
        switch (attempt.status)
        {
            case ProjectLockAttempt::Status::Acquired:
                lock = std::move(attempt.lock);  // releases the previous project's lock
                currentFolder = folder;
                result.outcome = Result::Outcome::Acquired;
                break;
            case ProjectLockAttempt::Status::HeldByOther:
                // The holder is this very lock, seen through a path the identity check could not
                // open: never refuse the project that is already open.
                if (lock.isHeld() && attempt.ownerPid == GetCurrentProcessId() &&
                    sameFolderText(currentFolder, folder))
                {
                    result.outcome = Result::Outcome::Kept;
                    break;
                }
                result.outcome = Result::Outcome::HeldByOther;
                result.message = std::move(attempt.message);
                result.ownerPid = attempt.ownerPid;
                break;
            case ProjectLockAttempt::Status::Unavailable:
                lock.release();
                currentFolder = folder;
                result.outcome = Result::Outcome::Unavailable;
                result.message = std::move(attempt.message);
                break;
        }
        return result;
    }

    bool ProjectLockKeeper::isCurrentFolder(const std::filesystem::path& folder) const
    {
        if (currentFolder.empty())
        {
            return false;
        }
        // The lock file's identity decides when there is one: a case-insensitive name compare would
        // call two folders of a case-sensitive directory (fsutil setCaseSensitiveInfo) the same.
        if (lock.isHeld() && lock.hasFileIdentity())
        {
            return lock.holdsFile(folder / editorStateDirName / editorLockFileName);
        }
        return sameFolderText(currentFolder, folder);
    }

    void ProjectLockKeeper::release() noexcept
    {
        lock.release();
        currentFolder.clear();
    }
}
