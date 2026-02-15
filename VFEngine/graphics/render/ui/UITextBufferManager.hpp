#pragma once

#include "UITextRenderTypes.hpp"
#include "UIRenderTypes.hpp"
#include "../common/QuadBufferManager.hpp"
#include <vector>

namespace render::ui
{
    class UITextBufferManager : public common::QuadBufferManager<UIVertex, UITextCharInstance>
    {
    public:
        explicit UITextBufferManager(core::Device& device);
        ~UITextBufferManager() = default;

        void init();
        void cleanUp();

        void updateInstanceBuffer(const std::vector<UITextCharInstance>& instances);
    };
}
