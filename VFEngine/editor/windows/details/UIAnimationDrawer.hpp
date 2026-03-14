#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"

namespace windows::details {
    class UIAnimationDrawer {
    public:
        bool draw(services::EntityHandle handle);
    private:
        bool drawHeader(bool& outRemove);
        bool drawNode(services::UIAnimationNodeData& node, int depth, int& nodeId);
        bool drawClipEditor(services::UIAnimationClipData& clip, int nodeId);
    };
}
