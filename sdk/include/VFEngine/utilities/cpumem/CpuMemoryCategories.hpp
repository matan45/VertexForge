#pragma once
#include <string_view>

// Canonical names for the well-known CPU RAM categories. Category *ids* are
// assigned at runtime by CpuMemoryManager::registerCategory; recorders resolve
// a name to an id once (cached in a function-local static) and then use the id
// on the hot path. Keeping the names here avoids string typos drifting between
// the recorder sites and the diagnostics window.

namespace memory::categories
{
    // Transient import/decode peaks. Import is an Editor-only DLL, so these
    // populate in the Editor; asset decode in the Runtime reuses the same names.
    inline constexpr std::string_view ImportTextureDecode   = "Import/TextureDecode";
    inline constexpr std::string_view ImportHdrDecode       = "Import/HdrDecode";
    inline constexpr std::string_view ImportTextureCompress = "Import/TextureCompress";
    inline constexpr std::string_view ImportMeshDecode      = "Import/MeshDecode";
    inline constexpr std::string_view ImportAudioDecode     = "Import/AudioDecode";

    // CPU-visible upload staging — runs in both the Editor and the Runtime.
    inline constexpr std::string_view UploadStagingRing     = "UploadStaging/Ring";
    inline constexpr std::string_view UploadStagingOverflow = "UploadStaging/Overflow";
}
