#include "FileProcessingStage.hpp"
#include <stdexcept>

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
            if (context.fileType == "PNG" || context.fileType == "JPEG" || context.fileType == "BMP")
            {
                processTexture(context);
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
}
