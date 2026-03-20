#include "FileProcessingStage.hpp"
#include "../../types/BRDFLUTExporter.hpp"
#include "resource/PathResolver.hpp"
#include <stdexcept>
#include <filesystem>
#include <mutex>

namespace pipeline::stages
{
    template<typename CallbackType>
    CallbackType FileProcessingStage::wrapProgress(ImportContext& context) const
    {
        if (!context.progressCallback)
            return nullptr;

        return [&context](float progress)
        {
            context.progressCallback(context.fileName, context.fileIndex + 1,
                                     context.totalFiles, progress);
        };
    }

    std::optional<ImportContext> FileProcessingStage::process(ImportContext context)
    {
        try
        {
            if (context.fileType == "PNG" || context.fileType == "JPEG" || context.fileType == "BMP" || context.fileType == "TGA")
            {
                // Check if SVT tiling is requested for large textures
                if (context.file.config.svtEnabled)
                {
                    processTextureSVT(context);
                }
                else
                {
                    processTexture(context);
                }
            }
            else if (context.fileType == "HDR" || context.fileType == "EXR")
            {
                processHDR(context);
            }
            else if (context.fileType == "MP3" || context.fileType == "WAV" || context.fileType == "OGG")
            {
                processAudio(context);
            }
            else if (context.fileType == "OBJ" || context.fileType == "FBX" ||
                context.fileType == "DAE" || context.fileType == "GLTF" || context.fileType == "GLB")
            {
                processMesh(context);
            }
            else if (context.fileType == "TTF" || context.fileType == "OTF")
            {
                processFont(context);
            }
            else
            {
                context.isValid = false;
                context.errorMessage = "Unsupported file type for processing: " + context.fileType;
                return context;
            }
        }
        catch (const std::exception& e)
        {
            context.isValid = false;
            context.errorMessage = "Processing failed: " + std::string(e.what());
            return context;
        }

        return context;
    }

    void FileProcessingStage::processTexture(ImportContext& context)
    {
        textureProcessor.loadTextureFile(context.file, context.fileName, context.location,
                                         wrapProgress<types::TextureProgressCallback>(context));
    }

    void FileProcessingStage::processHDR(ImportContext& context)
    {
        textureProcessor.loadHDRFile(context.file, context.fileName, context.location,
                                     wrapProgress<types::TextureProgressCallback>(context));

        static std::once_flag brdfLutFlag;
        std::call_once(brdfLutFlag, []()
        {
            const std::string lutPath = resource::PathResolver::resolveEnginePath("../../resources/ibl/brdf_lut.vfImage");
            if (!std::filesystem::exists(lutPath))
            {
                types::BRDFLUTExporter::generateAndSave(lutPath);
            }
        });
    }

    void FileProcessingStage::processAudio(ImportContext& context)
    {
        audioProcessor.loadFromFileWithType(context.file, context.fileName, context.location,
                                            context.fileType, wrapProgress<types::AudioProgressCallback>(context));
    }

    void FileProcessingStage::processMesh(ImportContext& context)
    {
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

    void FileProcessingStage::processFont(ImportContext& context)
    {
        types::FontImportConfig config;
        if (!fontProcessor.loadFromFile(context.file, context.fileName, context.location, config,
                                        wrapProgress<types::FontProgressCallback>(context)))
        {
            throw std::runtime_error("Failed to import font: " + std::string(context.fileName));
        }
    }

    void FileProcessingStage::processTextureSVT(ImportContext& context)
    {
        // First generate standard .vfImage (still needed as fallback for small objects)
        processTexture(context);

        // Then generate .vfSVT alongside it for large textures
        types::SVTTextureProcessor::Config svtConfig;
        svtConfig.minSizeForSVT = context.file.config.svtMinSize;
        svtConfig.srgb = true; // Assume albedo; normal/ORM would be imported separately

        std::string outputPath = std::string(context.location) + "/" +
                                 std::string(context.fileName) + "." + FileExtension::svt;

        types::SVTProgressCallback svtProgress = nullptr;
        if (context.progressCallback)
        {
            svtProgress = [&context](float progress)
            {
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, 0.5f + progress * 0.5f);
            };
        }

        types::SVTTextureProcessor::convertToSVT(context.file.path, outputPath,
                                                   svtConfig, svtProgress);
    }
}
