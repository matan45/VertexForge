#include "MeshImporter.hpp"
#include <algorithm>
#include <ranges>

namespace import::builtin
{
    namespace
    {
        constexpr unsigned char glbSig[] = {0x67, 0x6C, 0x54, 0x46};

        bool matchesSignature(std::span<const unsigned char> header, std::span<const unsigned char> signature)
        {
            return header.size() >= signature.size() &&
                   std::equal(signature.begin(), signature.end(), header.begin());
        }

        bool isFBX(std::span<const unsigned char> header)
        {
            const std::string fbxSignature = "Kaydara FBX Binary";
            if (header.size() < fbxSignature.size()) return false;

            auto result = std::ranges::search(header, fbxSignature);
            return !result.empty();
        }

        bool isDAE(std::span<const unsigned char> header)
        {
            const std::string xmlStart = "<?xml";
            const std::string colladaRoot = "<COLLADA";

            if (header.size() < xmlStart.size()) return false;

            auto xmlResult = std::ranges::search(header, xmlStart);
            if (xmlResult.empty()) return false;

            auto colladaResult = std::ranges::search(header, colladaRoot);
            return !colladaResult.empty();
        }

        bool isGLTF(std::span<const unsigned char> header)
        {
            if (header.empty() || header[0] != '{') return false;

            const std::string gltfAsset = "\"asset\"";
            auto result = std::ranges::search(header, gltfAsset);
            return !result.empty();
        }

        bool isOBJ(std::span<const unsigned char> header)
        {
            const std::vector<std::string> objKeywords = {"# ", "v ", "vn ", "vt ", "f ", "o ", "g "};

            std::string headerStr(header.begin(), header.end());

            for (const auto& keyword : objKeywords)
            {
                if (headerStr.find(keyword) != std::string::npos)
                {
                    return true;
                }
            }

            return false;
        }
    }

    std::vector<FormatInfo> MeshImporter::formats() const
    {
        return {
            {"GLB", "Model Files", {"glb"}, FileExtension::mesh, resource::AssetType::Mesh, 100},
            {"FBX", "Model Files", {"fbx"}, FileExtension::mesh, resource::AssetType::Mesh, 50},
            {"DAE", "Model Files", {"dae"}, FileExtension::mesh, resource::AssetType::Mesh, 40},
            {"GLTF", "Model Files", {"gltf"}, FileExtension::mesh, resource::AssetType::Mesh, 30},
            {"OBJ", "Model Files", {"obj"}, FileExtension::mesh, resource::AssetType::Mesh, 20},
        };
    }

    bool MeshImporter::matches(const std::string& fileType, const DetectionInput& input) const
    {
        if (fileType == "GLB") return matchesSignature(input.header, glbSig);
        if (fileType == "FBX") return isFBX(input.header);
        if (fileType == "DAE") return isDAE(input.header);
        if (fileType == "GLTF") return isGLTF(input.header);
        if (fileType == "OBJ") return isOBJ(input.header);
        return false;
    }

    void MeshImporter::process(pipeline::ImportContext& context)
    {
        // Mesh extraction reports 0-70% of the file, animation extraction 70-100%.
        types::MeshProgressCallback meshProgress = nullptr;
        if (context.progressCallback)
        {
            meshProgress = [&context](float progress)
            {
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, progress * 0.7f);
            };
        }

        meshProcessor.loadFromFile(context.file, context.fileName, context.location, meshProgress);

        types::AnimationProgressCallback animProgress = nullptr;
        if (context.progressCallback)
        {
            animProgress = [&context](float progress)
            {
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, 0.7f + progress * 0.3f);
            };
        }

        animationProcessor.loadFromFile(context.file, context.fileName, context.location, animProgress);
    }
}
