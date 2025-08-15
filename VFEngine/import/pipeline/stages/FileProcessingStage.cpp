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
        textureProcessor.loadTextureFileWithType(context.file, context.fileName, context.location, context.fileType);
    }

    void FileProcessingStage::processHDR(ImportContext& context)
    {
        textureProcessor.loadHDRFileWithType(context.file, context.fileName, context.location, context.fileType);
    }

    void FileProcessingStage::processAudio(ImportContext& context)
    {
        audioProcessor.loadFromFileWithType(context.file, context.fileName, context.location, context.fileType);
    }

    void FileProcessingStage::processMesh(ImportContext& context)
    {
        meshProcessor.loadFromFile(context.file, context.fileName, context.location);
    }
}