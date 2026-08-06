#pragma once

#include <filesystem>

namespace resource
{
    // Forces the file's bytes past the OS write cache. std::ofstream::flush()/close() only drain
    // the CRT and page cache, and iostreams expose no handle, so this reopens the path purely to
    // call FlushFileBuffers on it.
    bool flushFileToDisk(const std::filesystem::path& path);

    // Durably replaces finalPath with the contents of tempPath.
    //
    // The destination name never resolves to "absent": the swap is a single directory-level commit
    // (ReplaceFileW, or MoveFileExW when there is nothing to replace), so an interrupted call leaves
    // either the previous complete file or the new complete file in place — never a hole.
    //
    // tempPath's bytes are pushed past the OS cache before the swap, so the file that survives a
    // power loss is complete rather than a plausible-looking prefix. std::ostream::flush() only
    // reaches the cache, which is why callers must not rely on it alone.
    //
    // On success tempPath no longer exists. On failure finalPath is untouched and tempPath is
    // removed, so a caller can retry from scratch.
    bool replaceFileAtomically(const std::filesystem::path& tempPath,
                               const std::filesystem::path& finalPath);
}
