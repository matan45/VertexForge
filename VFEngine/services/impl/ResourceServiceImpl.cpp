#include "ResourceServiceImpl.hpp"
#include <filesystem>
#include <algorithm>
#include <future>

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

        // Don't start a new import if one is already in progress
        if (importing.load()) {
            return;
        }

        importing.store(true);
        progress.store(0.0f);

        // Publish started notification on main thread
        std::vector<std::string> filePaths;
        for (const auto& file : files) {
            filePaths.push_back(file.path);
        }
        events::resource::ImportStartedNotification startNotification;
        startNotification.files = filePaths;
        events::EventDispatcher::instance().publish(startNotification);

        // Run import asynchronously so UI can update
        importFuture = std::async(std::launch::async, [this, files]() {
            // Create progress callback that publishes events
            auto progressCallback = [this](std::string_view currentFileName, uint32_t fileIndex,
                                           uint32_t totalFiles, float fileProgress) {
                // Calculate overall progress
                float overallProgress = (static_cast<float>(fileIndex - 1) + fileProgress) / totalFiles;
                progress.store(overallProgress);

                // Thread-safe update of current file
                {
                    std::lock_guard<std::mutex> lock(currentFileMutex);
                    currentFile = std::string(currentFileName);
                }

                // Publish progress notification
                events::resource::ImportProgressNotification progressNotif;
                progressNotif.currentFile = std::string(currentFileName);
                progressNotif.progress = overallProgress;
                events::EventDispatcher::instance().publish(progressNotif);
            };

            // Call the Import delegate if set
            if (importDelegate.importFiles) {
                importDelegate.importFiles(files, progressCallback);
            }

            // Mark as complete
            importing.store(false);
            progress.store(1.0f);

            // Publish completed notification
            events::resource::ImportCompletedNotification completeNotification;
            for (const auto& file : files) {
                ImportResult result;
                result.sourcePath = file.path;
                result.success = true;  // Assume success for now
                completeNotification.results.push_back(result);
            }
            events::EventDispatcher::instance().publish(completeNotification);
        });
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
        std::lock_guard<std::mutex> lock(currentFileMutex);
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

    void ResourceServiceImpl::registerEventHandlers() {
        auto& dispatcher = events::EventDispatcher::instance();

        // Command handlers
        dispatcher.registerCommandHandler<events::resource::ImportFilesCommand>(
            [this](const events::resource::ImportFilesCommand& cmd) {
                importFiles(cmd.files);
            });

        dispatcher.registerCommandHandler<events::resource::SetImportLocationCommand>(
            [this](const events::resource::SetImportLocationCommand& cmd) {
                setImportLocation(cmd.path);
            });

        dispatcher.registerCommandHandler<events::resource::CancelImportCommand>(
            [this](const events::resource::CancelImportCommand&) {
                cancelImport();
            });

        // Query handlers
        dispatcher.registerQueryHandler<events::resource::GetImportLocationQuery>(
            [this](const events::resource::GetImportLocationQuery&) {
                return getImportLocation();
            });

        dispatcher.registerQueryHandler<events::resource::IsImportingQuery>(
            [this](const events::resource::IsImportingQuery&) {
                return isImporting();
            });

        dispatcher.registerQueryHandler<events::resource::GetImportProgressQuery>(
            [this](const events::resource::GetImportProgressQuery&) {
                return getImportProgress();
            });
    }

}
