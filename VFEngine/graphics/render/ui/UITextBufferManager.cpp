#include "UITextBufferManager.hpp"

namespace render::ui
{
    UITextBufferManager::UITextBufferManager(core::Device& device)
        : QuadBufferManager{device}
    {
    }

    void UITextBufferManager::init()
    {
        createQuadBuffers(QUAD_VERTICES, QUAD_INDICES);
        createInstanceBuffer();
    }

    void UITextBufferManager::cleanUp()
    {
        cleanUpQuadAndInstanceBuffers();
    }

    void UITextBufferManager::updateInstanceBuffer(const std::vector<UITextCharInstance>& instances)
    {
        uploadInstances(instances.data(), static_cast<uint32_t>(instances.size()));
    }
}
