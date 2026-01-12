#include "FileProcessingStage.hpp"

namespace pipeline::stages
{
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
        // Create a texture progress callback that wraps the import progress callback
        types::TextureProgressCallback textureProgress = nullptr;
        if (context.progressCallback)
        {
            textureProgress = [&context](float progress)
            {
                // Report texture progress through the import progress callback
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, progress);
            };
        }

        textureProcessor.loadTextureFile(context.file, context.fileName, context.location, textureProgress);
    }

    void FileProcessingStage::processHDR(ImportContext& context)
    {
        // Create a texture progress callback that wraps the import progress callback
        types::TextureProgressCallback textureProgress = nullptr;
        if (context.progressCallback)
        {
            textureProgress = [&context](float progress)
            {
                // Report HDR progress through the import progress callback
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, progress);
            };
        }

        textureProcessor.loadHDRFile(context.file, context.fileName, context.location, textureProgress);
    }

    void FileProcessingStage::processAudio(ImportContext& context)
    {
        // Create an audio progress callback that wraps the import progress callback
        types::AudioProgressCallback audioProgress = nullptr;
        if (context.progressCallback)
        {
            audioProgress = [&context](float progress)
            {
                // Report audio progress through the import progress callback
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, progress);
            };
        }

        audioProcessor.loadFromFileWithType(context.file, context.fileName, context.location,
                                            context.fileType, audioProgress);
    }

    void FileProcessingStage::processMesh(ImportContext& context)
    {
        // Create a mesh progress callback that wraps the import progress callback
        types::MeshProgressCallback meshProgress = nullptr;
        if (context.progressCallback)
        {
            meshProgress = [&context](float progress)
            {
                // Report mesh progress through the import progress callback
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, progress);
            };
        }

        meshProcessor.loadFromFile(context.file, context.fileName, context.location, meshProgress);
    }

    void FileProcessingStage::processFont(ImportContext& context)
    {
        types::FontProgressCallback fontProgress = nullptr;
        if (context.progressCallback)
        {
            fontProgress = [&context](float progress)
            {
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, progress);
            };
        }

        types::FontImportConfig config;
        fontProcessor.loadFromFile(context.file, context.fileName, context.location, config, fontProgress);
    }
}
