#include "CoreTools.hpp"
#include "../protocol/ArgReader.hpp"
#include "../util/ImageEncode.hpp"

#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        using ReadbackState = services::ViewportReadbackResult::State;

        constexpr int64_t defaultMaxWidth = 1280;
        constexpr int64_t minMaxWidth = 64;
        constexpr int64_t maxMaxWidth = 2048;
        constexpr int64_t defaultSettleFrames = 2;
        constexpr int64_t maxSettleFrames = 10;

        constexpr std::chrono::milliseconds pollInterval{16};
        constexpr std::chrono::milliseconds readbackDeadline{3000};
        constexpr std::chrono::milliseconds mainThreadTimeout{5000};

        void registerViewportScreenshot(ToolRegistry& registry, const ToolContext& context)
        {
            ToolDef tool;
            tool.name = "viewport_screenshot";
            tool.title = "Viewport screenshot";
            tool.description =
                "Capture the editor viewport as a PNG image (the final rendered scene, after tonemapping "
                "and upscaling). ImGui overlays are NOT captured: transform gizmos, entity icons/billboard "
                "icons and other editor widgets drawn over the viewport do not appear. The capture waits "
                "'settleFrames' more rendered frames so edits made just before the call are visible. "
                "Fails if the viewport is not rendering (panel hidden, window minimized or a scene is "
                "loading). Returns the image plus {width, height, sourceWidth, sourceHeight}.";
            tool.inputSchema = schema::object({
                {"maxWidth", {
                    {"type", "integer"}, {"minimum", minMaxWidth}, {"maximum", maxMaxWidth},
                    {"description", "Downscale so the image is at most this wide (aspect preserved). Default 1280."}
                }},
                {"settleFrames", {
                    {"type", "integer"}, {"minimum", 0}, {"maximum", maxSettleFrames},
                    {"description", "Frames to render before capturing. Default 2."}
                }}
            });
            tool.affinity = ThreadAffinity::Worker;
            tool.readOnly = true;
            tool.timeout = std::chrono::milliseconds(15000);
            tool.handler = [context](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const int64_t maxWidth = reader.optInt("maxWidth", defaultMaxWidth);
                if (maxWidth < minMaxWidth || maxWidth > maxMaxWidth)
                {
                    throw ArgError("argument 'maxWidth' must be between " + std::to_string(minMaxWidth) +
                                   " and " + std::to_string(maxMaxWidth));
                }
                const int64_t settleFrames = reader.optInt("settleFrames", defaultSettleFrames);
                if (settleFrames < 0 || settleFrames > maxSettleFrames)
                {
                    throw ArgError("argument 'settleFrames' must be between 0 and " +
                                   std::to_string(maxSettleFrames));
                }

                const uint32_t settle = static_cast<uint32_t>(settleFrames);
                nlohmann::json ticketJson = context.runOnMain([settle]() -> nlohmann::json
                {
                    events::render::RequestViewportReadbackCommand command;
                    command.settleFrames = settle;
                    return events::EventDispatcher::instance().execute(command);
                }, mainThreadTimeout);
                const uint64_t ticket = ticketJson.get<uint64_t>();
                if (ticket == 0)
                {
                    return ToolResult::error("viewport readback is not available (no viewport renderer)");
                }

                // Pixels are not JSON: the main-thread task moves the result into a
                // slot it shares by value. A fresh slot per poll, so a task that runs
                // after its caller timed out never writes into a later poll's slot.
                std::shared_ptr<services::ViewportReadbackResult> result;
                const auto deadline = std::chrono::steady_clock::now() + readbackDeadline;
                while (true)
                {
                    auto slot = std::make_shared<services::ViewportReadbackResult>();
                    context.runOnMain([slot, ticket]() -> nlohmann::json
                    {
                        events::render::TakeViewportReadbackQuery query;
                        query.ticket = ticket;
                        *slot = events::EventDispatcher::instance().query(query);
                        return static_cast<int>(slot->state);
                    }, mainThreadTimeout);

                    if (slot->state != ReadbackState::Pending)
                    {
                        result = std::move(slot);
                        break;
                    }
                    if (std::chrono::steady_clock::now() >= deadline)
                    {
                        return ToolResult::error(
                            "viewport not rendering (panel hidden, minimized or a scene is loading)");
                    }
                    std::this_thread::sleep_for(pollInterval);
                }

                if (result->state == ReadbackState::Failed)
                {
                    return ToolResult::error("viewport readback failed (the request was replaced or the "
                                             "viewport was recreated); try again");
                }

                const uint32_t sourceWidth = result->width;
                const uint32_t sourceHeight = result->height;
                std::vector<uint8_t> rgba8 = util::halfRgbaToRgba8(result->rgba16f, sourceWidth, sourceHeight);
                result.reset();  // release the RGBA16F copy before encoding
                if (rgba8.empty())
                {
                    return ToolResult::error("viewport readback returned no pixels");
                }

                uint32_t width = 0;
                uint32_t height = 0;
                rgba8 = util::downscaleRgba8(rgba8, sourceWidth, sourceHeight,
                                             static_cast<uint32_t>(maxWidth), width, height);
                const std::vector<uint8_t> png = util::encodePng(rgba8, width, height);
                if (png.empty())
                {
                    return ToolResult::error("PNG encoding failed");
                }

                nlohmann::json structured{
                    {"width", width},
                    {"height", height},
                    {"sourceWidth", sourceWidth},
                    {"sourceHeight", sourceHeight}
                };
                std::string text = "Viewport screenshot " + std::to_string(width) + "x" + std::to_string(height) +
                                   " (source " + std::to_string(sourceWidth) + "x" +
                                   std::to_string(sourceHeight) + ")";
                return ToolResult::image(util::base64Encode(png.data(), png.size()), "image/png",
                                         std::move(structured), std::move(text));
            };
            registry.add(std::move(tool));
        }
    }

    void registerViewTools(ToolRegistry& registry, const ToolContext& context)
    {
        registerViewportScreenshot(registry, context);
    }
}
