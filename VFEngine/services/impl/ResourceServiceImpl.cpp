#include "ResourceServiceImpl.hpp"
#include <filesystem>
#include <algorithm>

namespace services {

    ResourceServiceImpl::ResourceServiceImpl() {
        // Import delegate will be set by Editor
    }

    void ResourceServiceImpl::setImportDelegate(const ImportDelegate& delegate) {
        importDelegate = delegate;
        if (importDelegate.initialize) {
            importDelegate.initialize();
        }
    }

    void ResourceServiceImpl::importFiles(const std::vector<ImportFileRequest>& files) {
        if (files.empty()) return;

        importing = true;
        progress = 0.0f;

        std::vector<std::string> filePaths;
        for (const auto& file : files) {
            filePaths.push_back(file.path);
        }

        // Publish started notification
        events::resource::ImportStartedNotification startNotification;
        startNotification.files = filePaths;
        events::EventDispatcher::instance().publish(startNotification);

        // Call the Import delegate if set
        if (importDelegate.importFiles) {
            importDelegate.importFiles(files);
        }

        // For now, mark as complete (actual async tracking would require Import modifications)
        importing = false;
        progress = 1.0f;

        // Publish completed notification
        events::resource::ImportCompletedNotification completeNotification;
        for (const auto& file : files) {
            ImportResult result;
            result.sourcePath = file.path;
            result.success = true;  // Assume success for now
            completeNotification.results.push_back(result);
        }
        events::EventDispatcher::instance().publish(completeNotification);
    }

    void ResourceServiceImpl::setImportLocation(const std::string& path) {
        importLocation = path;

        // Call the Import delegate if set
        if (importDelegate.setLocation) {
            importDelegate.setLocation(path);
        }

        // Publish notification
        events::resource::ImportLocationChangedNotification notification;
        notification.newLocation = path;
        events::EventDispatcher::instance().publish(notification);
    }

    std::string ResourceServiceImpl::getImportLocation() const {
        return importLocation;
    }

    void ResourceServiceImpl::cancelImport() {
        // Import controller doesn't support cancellation yet
        importing = false;
    }

    bool ResourceServiceImpl::isImporting() const {
        return importing;
    }

    float ResourceServiceImpl::getImportProgress() const {
        return progress;
    }

    std::string ResourceServiceImpl::getCurrentImportFile() const {
        return currentFile;
    }

    bool ResourceServiceImpl::isResourceLoaded(const std::string& path) const {
        // Would need to check ResourceManager cache
        return false;
    }

    ResourceType ResourceServiceImpl::getResourceType(const std::string& path) const {
        std::string ext = getExtension(path);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Check for engine formats
        if (ext == "vfimage") return ResourceType::Texture;
        if (ext == "vfhdr") return ResourceType::HDRTexture;
        if (ext == "vfmesh") return ResourceType::Mesh;
        if (ext == "vfaudio") return ResourceType::Audio;
        if (ext == "vfanim") return ResourceType::Animation;

        // Check for common formats
        if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp") {
            return ResourceType::Texture;
        }
        if (ext == "hdr" || ext == "exr") {
            return ResourceType::HDRTexture;
        }
        if (ext == "obj" || ext == "fbx" || ext == "gltf" || ext == "glb") {
            return ResourceType::Mesh;
        }
        if (ext == "wav" || ext == "mp3" || ext == "ogg") {
            return ResourceType::Audio;
        }

        return ResourceType::Unknown;
    }

    bool ResourceServiceImpl::pathExists(const std::string& path) const {
        return std::filesystem::exists(path);
    }

    bool ResourceServiceImpl::isDirectory(const std::string& path) const {
        return std::filesystem::is_directory(path);
    }

    std::vector<std::string> ResourceServiceImpl::getSupportedExtensions() const {
        return {
            // Images
            "png", "jpg", "jpeg", "tga", "bmp",
            // HDR
            "hdr", "exr",
            // Meshes
            "obj", "fbx", "gltf", "glb",
            // Audio
            "wav", "mp3", "ogg"
        };
    }

    bool ResourceServiceImpl::isExtensionSupported(const std::string& extension) const {
        auto supported = getSupportedExtensions();
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Remove leading dot if present
        if (!ext.empty() && ext[0] == '.') {
            ext = ext.substr(1);
        }

        return std::find(supported.begin(), supported.end(), ext) != supported.end();
    }

    std::string ResourceServiceImpl::getExtension(const std::string& path) const {
        std::filesystem::path p(path);
        std::string ext = p.extension().string();

        // Remove leading dot
        if (!ext.empty() && ext[0] == '.') {
            ext = ext.substr(1);
        }

        return ext;
    }

}
