#pragma once
#include <glm/glm.hpp>
#include <string_view>

namespace render
{
    class RenderPassHandler;
}

namespace controllers::offscreen
{
    class IBLController
    {
    public:
        explicit IBLController(render::RenderPassHandler& renderHandler);
        ~IBLController();

        void set(std::string_view iblPath);
        void setCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        void remove();

    private:
        render::RenderPassHandler& renderHandler;
    };
}
