#include "MeshImporter.hpp"
#include <algorithm>
#include <ranges>
#include <variant>

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

        bool isAnimationOnly(const pipeline::ImportContext& context)
        {
            const auto& options = context.file.config.customOptions;
            auto it = options.find("animationOnly");
            return it != options.end() && std::holds_alternative<bool>(it->second) &&
                   std::get<bool>(it->second);
        }

        bool isExtractEmbeddedTextures(const pipeline::ImportContext& context)
        {
            const auto& options = context.file.config.customOptions;
            auto it = options.find("extractEmbeddedTextures");
            return it != options.end() && std::holds_alternative<bool>(it->second) &&
                   std::get<bool>(it->second);
        }

        bool isCombineMeshes(const pipeline::ImportContext& context)
        {
            const auto& options = context.file.config.customOptions;
            auto it = options.find("combineMeshes");
            return it != options.end() && std::holds_alternative<bool>(it->second) &&
                   std::get<bool>(it->second);
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
        const bool animationOnly = isAnimationOnly(context);

        if (!animationOnly)
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

            // Split layout writes one .vfMesh per Assimp mesh. Combined static
            // layout writes one multi-submesh .vfMesh. Surface every produced
            // file so the controller creates the matching metadata and result.
            // Optionally (VK-55), embedded textures are extracted to .vfImage.
            const bool extractEmbedded = isExtractEmbeddedTextures(context);

            std::vector<std::string> writtenMeshes;
            std::vector<std::string> writtenTextures;
            const auto outputLayout = isCombineMeshes(context)
                                          ? types::MeshOutputLayout::CombinedStatic
                                          : types::MeshOutputLayout::Split;
            meshProcessor.loadFromFile(context.file, context.fileName, context.location, outputLayout, meshProgress,
                                       &writtenMeshes,
                                       extractEmbedded ? &writtenTextures : nullptr);

            for (auto& path : writtenMeshes)
                context.outputFiles.push_back({std::move(path), resource::AssetType::Mesh});

            for (auto& path : writtenTextures)
                context.outputFiles.push_back({std::move(path), resource::AssetType::Texture});
        }

        types::AnimationProgressCallback animProgress = nullptr;
        if (context.progressCallback)
        {
            const float base = animationOnly ? 0.0f : 0.7f;
            const float span = animationOnly ? 1.0f : 0.3f;
            animProgress = [&context, base, span](float progress)
            {
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, base + progress * span);
            };
        }

        animationProcessor.loadFromFile(context.file, context.fileName, context.location, animProgress);
    }

    std::string MeshImporter::deriveOutputFile(const pipeline::ImportContext& context) const
    {
        if (isAnimationOnly(context))
            return context.fileName + "." + FileExtension::animation;
        return {};
    }

    resource::AssetType MeshImporter::deriveAssetType(const pipeline::ImportContext& context) const
    {
        return isAnimationOnly(context) ? resource::AssetType::Animation : resource::AssetType::COUNT;
    }

    std::vector<ImportOptionDesc> MeshImporter::options() const
    {
        ImportOptionDesc animationOnly;
        animationOnly.key = "animationOnly";
        animationOnly.label = "Animation Only";
        animationOnly.tooltip = "Extract only animations (.vfAnim) and skip the mesh data entirely";
        animationOnly.type = ImportOptionDesc::Type::Bool;
        animationOnly.defaultValue = false;

        ImportOptionDesc extractEmbeddedTextures;
        extractEmbeddedTextures.key = "extractEmbeddedTextures";
        extractEmbeddedTextures.label = "Extract Embedded Textures";
        extractEmbeddedTextures.tooltip =
            "Extract textures embedded in the model file and save each as a separate .vfImage asset";
        extractEmbeddedTextures.type = ImportOptionDesc::Type::Bool;
        extractEmbeddedTextures.defaultValue = false;

        ImportOptionDesc combineMeshes;
        combineMeshes.key = "combineMeshes";
        combineMeshes.label = "Combine Meshes";
        combineMeshes.tooltip =
            "Write one multi-submesh .vfMesh for static models so one Mesh Component renders the complete model";
        combineMeshes.type = ImportOptionDesc::Type::Bool;
        combineMeshes.defaultValue = false;

        return {animationOnly, extractEmbeddedTextures, combineMeshes};
    }
}
