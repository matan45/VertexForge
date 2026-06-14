#pragma once
#include "../../services/events/render/ThumbnailEvents.hpp"
#include "../../services/data/DTOs.hpp"
#include "../../services/data/AsyncLoadingTypes.hpp"
#include <memory>
#include <string>
#include <deque>
#include <unordered_map>

namespace controllers
{
    class MeshPreviewController;
    class MaterialPreviewController;
}

namespace core
{
    // VK-1379: renders Content Browser thumbnails for meshes/materials/material
    // instances using a small fixed pool (one mesh + one material preview
    // controller, reused), strictly throttled to one in-flight job per kind.
    // Each completed render is GPU-blitted into an owned 128px texture. Registers
    // its own CQRS handlers (events/render/ThumbnailEvents.hpp) and is ticked once
    // per frame via process().
    class ThumbnailRenderAdapter
    {
    public:
        explicit ThumbnailRenderAdapter() = default;
        ~ThumbnailRenderAdapter();

        ThumbnailRenderAdapter(const ThumbnailRenderAdapter&) = delete;
        ThumbnailRenderAdapter& operator=(const ThumbnailRenderAdapter&) = delete;

        void init();      // register CQRS handlers
        void process();   // per-frame tick (advances the mesh + material slots)
        void cleanUp();

    private:
        enum class Stage : uint8_t { Pending, MeshLoading, Rendering, Done, Failed };

        struct Job
        {
            void* id = nullptr;
            events::render::RenderThumbnailKind kind = events::render::RenderThumbnailKind::Mesh;
            std::string meshPath;
            services::MaterialPreviewParams materialParams;
            Stage stage = Stage::Pending;
            int settleFrames = 0;
            bool started = false;
            services::EditorTextureHandle handle;
        };

        std::unordered_map<void*, Job> jobs;
        std::deque<void*> meshQueue;
        std::deque<void*> materialQueue;
        void* activeMesh = nullptr;
        void* activeMaterial = nullptr;
        std::unordered_map<void*, events::render::RenderThumbnailKind> handleOwner; // descriptor -> kind

        std::unique_ptr<controllers::MeshPreviewController> meshController;
        std::unique_ptr<controllers::MaterialPreviewController> materialController;
        bool handlersRegistered = false;

        static constexpr uint32_t THUMB_SIZE = 128;
        static constexpr int MESH_SETTLE_FRAMES = 2;
        static constexpr int MATERIAL_SETTLE_FRAMES = 4;

        // CQRS-backed operations
        void queue(const events::render::LoadRenderThumbnailAsyncCommand& cmd);
        void cancel(void* id);
        services::TextureLoadingProgress getProgress(void* id) const;
        services::EditorTextureHandle takeHandle(void* id);
        void release(void* handle);

        void processMeshSlot();
        void processMaterialSlot();
        void ensureControllers();
        void removeFromQueue(std::deque<void*>& queue, void* id);
    };
}
