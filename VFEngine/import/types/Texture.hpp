#pragma once
#include <string>
#include <functional>
#include "config/Config.hpp"
#include "resource/Types.hpp"
#include "../ImportExport.hpp"

namespace types
{
    using TextureProgressCallback = std::function<void(float progress)>;

    // VK-1642: which engine asset (if any) a KTX/KTX2/DDS import produced. The
    // importer / material-import caller uses this to record the correct output
    // path + asset type (LDR -> .vfImage/Texture, HDR -> .vfHdr/HDR).
    enum class TextureImportResult
    {
        Failed,       // nothing written (unsupported payload, decode error, bad file)
        WroteVfImage, // an LDR .vfImage (BC7 or Uncompressed)
        WroteVfHdr    // an HDR .vfHdr (BC6H)
    };

    struct DecodedTexture; // defined in BlockTextureAssembly.hpp (KTX/DDS decode result)

    class VF_IMPORT_API Texture
    {
    public:
        void loadTextureFile(const importConfig::ImportFiles& file, std::string_view fileName,
                             std::string_view location, TextureProgressCallback progressCallback = nullptr);
        void loadHDRFile(const importConfig::ImportFiles& file, std::string_view fileName,
                         std::string_view location, TextureProgressCallback progressCallback = nullptr) const;

        // Decodes an embedded texture blob and writes it as a .vfImage using the
        // standard mip + compression pipeline (honoring the import config's
        // compression mode/quality). isCompressed (Assimp aiTexture::mHeight == 0):
        // `data` is a `byteLength`-byte encoded file (png/jpg/...). Otherwise
        // `data` is width*height BGRA8 texels. Returns true if a .vfImage was written.
        bool saveEmbeddedTexture(std::string_view fileName, std::string_view location,
                                 const unsigned char* data, size_t byteLength,
                                 bool isCompressed, uint32_t width, uint32_t height,
                                 const importConfig::ImportConfig& config) const;

        // VK-1642: decode a KTX2/KTX1 (loadKtxFile) or DDS (loadDdsFile) container
        // into the engine's image format(s). Basis-supercompressed (ETC1S/UASTC) and
        // raw BCn/uncompressed payloads become BC7 (LDR -> .vfImage) or BC6H
        // (HDR -> .vfHdr); the source mip chain is preserved (single-level sources
        // regenerate mips). Returns which asset was written (or Failed). Definitions
        // live in KtxTextureImport.cpp / DdsTextureImport.cpp to quarantine the heavy
        // basisu include out of this header.
        TextureImportResult loadKtxFile(const importConfig::ImportFiles& file, std::string_view fileName,
                                        std::string_view location,
                                        TextureProgressCallback progressCallback = nullptr) const;
        TextureImportResult loadDdsFile(const importConfig::ImportFiles& file, std::string_view fileName,
                                        std::string_view location,
                                        TextureProgressCallback progressCallback = nullptr) const;

    private:
        // Builds a .vfImage from RGBA8 pixel data via mips + compression. Shared by
        // loadTextureFile and saveEmbeddedTexture.
        void writeRGBAAsVfImage(std::string_view fileName, std::string_view location,
                                std::vector<unsigned char> rgba, uint32_t width, uint32_t height,
                                uint32_t numChannels, importConfig::TextureCompressionMode mode,
                                importConfig::TextureCompressionQuality quality) const;

        // --- VK-1642 shared write-assembly helpers (defined in BlockTextureAssembly.cpp) ---
        // Write an already-BCn block mip chain verbatim (no recompression): BC7 ->
        // .vfImage, BC6H -> .vfHdr. Each level already carries its width/height/
        // dataSize/blocks; mipLevels is set to levels.size().
        void writeBlockMips(std::string_view fileName, std::string_view location,
                            std::vector<resource::MipLevelData> levels, uint32_t width, uint32_t height,
                            resource::TextureCompressionFormat format) const;

        // Write an RGBA8 mip chain, (re)compressing per config -> .vfImage. A single
        // supplied base level regenerates the chain (engine policy); a multi-level
        // source is preserved and re-encoded in place.
        void writeRgba8Mips(std::string_view fileName, std::string_view location,
                            std::vector<resource::MipLevelData> rgbaLevels, uint32_t width, uint32_t height,
                            importConfig::TextureCompressionMode mode,
                            importConfig::TextureCompressionQuality quality) const;

        // Write a raw-float (RGBA32F, packed float bytes) mip chain as HDR -> .vfHdr,
        // compressing to BC6H per config. Single base level regenerates the chain.
        void writeFloatHdrMips(std::string_view fileName, std::string_view location,
                               std::vector<resource::MipLevelData> floatLevels, uint32_t width, uint32_t height,
                               importConfig::TextureCompressionMode mode,
                               importConfig::TextureCompressionQuality quality) const;

        // Dispatch a parsed KTX/DDS result to the right writer above, honoring the
        // import config's compression mode. Returns which asset was written.
        TextureImportResult writeDecodedTexture(DecodedTexture&& decoded, std::string_view fileName,
                                                std::string_view location,
                                                const importConfig::ImportConfig& config) const;

        void loadHDRFromStbi(const importConfig::ImportFiles& file, std::string_view fileName,
                             std::string_view location, TextureProgressCallback progressCallback) const;
        void loadHDRFromEXR(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location, TextureProgressCallback progressCallback) const;

        void saveToFileTextureWithMips(std::string_view fileName, std::string_view location,
                                       const resource::TextureData& textureData) const;
        void saveToFileHDRWithMips(std::string_view fileName, std::string_view location,
                                   const resource::HDRData& hdrData) const;

        void generateMipmaps(resource::TextureData& textureData) const;
        void generateHDRMipmaps(std::vector<float>& basePixels, uint32_t width, uint32_t height,
                                resource::HDRData& hdrData) const;

        resource::MipLevelData generateMipLevel(const resource::MipLevelData& source) const;

        void compressTextureMips(resource::TextureData& textureData,
                                 importConfig::TextureCompressionMode mode,
                                 importConfig::TextureCompressionQuality quality) const;
        void compressHDRMips(resource::HDRData& hdrData,
                             importConfig::TextureCompressionMode mode,
                             importConfig::TextureCompressionQuality quality) const;

        void convertTo4Channels(unsigned char* inputData, int width, int height, int inputChannels,
            std::vector<unsigned char>& outputData);

        std::vector<float> convertToRGBA32F(const float* data, int width, int height, int channels) const;
        void flipImageVertically(float* imageData, int width, int height) const;
    };

    class TGAWriter
    {
        public:
        static void writeTGA(std::ofstream& file, const std::vector<unsigned char>& pixelData);
    };
}
