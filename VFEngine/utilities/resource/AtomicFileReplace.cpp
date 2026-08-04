#include "AtomicFileReplace.hpp"

#include "../print/Log.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace resource
{
    namespace fs = std::filesystem;

    bool flushFileToDisk(const fs::path& path)
    {
        // Shares write as well as read: callers legitimately flush a file they still hold open
        // through an fstream, and MSVC's iostreams open with _SH_DENYNO (read+write shared), so a
        // narrower share mode here would collide with their own handle.
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            vfLogError("flushFileToDisk: Cannot open {} (error {})", path.string(), GetLastError());
            return false;
        }

        const bool flushed = FlushFileBuffers(handle) != FALSE;
        const DWORD error = flushed ? 0 : GetLastError();
        CloseHandle(handle);

        if (!flushed)
            vfLogError("flushFileToDisk: Failed to flush {} (error {})", path.string(), error);
        return flushed;
    }

    bool replaceFileAtomically(const fs::path& tempPath, const fs::path& finalPath)
    {
        std::error_code ec;
        if (!fs::exists(tempPath, ec) || ec)
        {
            vfLogError("replaceFileAtomically: Replacement file {} does not exist", tempPath.string());
            return false;
        }

        // Order matters: the rename below only makes the *name* point at the new file. If the new
        // file's bytes are still in the page cache when power is lost, the name would resolve to a
        // complete-looking file with a truncated tail. std::ofstream exposes no handle, so the only
        // way to reach FlushFileBuffers is to reopen it.
        if (!flushFileToDisk(tempPath))
            return false;

        // MoveFileExW with MOVEFILE_REPLACE_EXISTING is a single NTFS directory-entry transaction:
        // the destination name resolves to the complete old file until it resolves to the complete
        // new one, with no instant in between where it resolves to nothing. ReplaceFileW would also
        // work but is compound (backup-rename, rename, then ACL/attribute/stream merge) and so has
        // strictly more intermediate states, in exchange for preserving destination ACLs and
        // alternate data streams that a project-folder asset does not have.
        //
        // MOVEFILE_COPY_ALLOWED is deliberately absent: it degrades a rename into copy-then-delete,
        // which would reintroduce exactly the long non-atomic window this function exists to remove.
        // The caller's temp is always a sibling of the destination, so a same-volume rename is
        // always available.
        if (MoveFileExW(tempPath.c_str(), finalPath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
            return true;

        // Leaves BOTH files intact. A reader holding the destination open blocks the rename with a
        // sharing violation (MSVC's ifstream opens without FILE_SHARE_DELETE), and the right answer
        // there is to keep the previous complete file and let the caller retry — not to destroy it,
        // which is what the fs::remove-then-rename this replaced would already have done by now.
        vfLogError("replaceFileAtomically: Failed to replace {} with {} (error {})",
                   finalPath.string(), tempPath.string(), GetLastError());
        return false;
    }
}
