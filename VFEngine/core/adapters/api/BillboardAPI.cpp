// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "BillboardAPI.hpp"
#include "NativeHelpers.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "asset/AssetRef.hpp"
#include "data/EntityConversion.hpp"
#include "time/Timer.hpp"
#include "render/FlipbookMath.hpp"

namespace core::api
{
    namespace
    {
        // Resolve a script entity id to a valid entt::entity that already carries a
        // BillboardComponent. Returns nullptr otherwise (so setters become no-ops on
        // entities the script never called Billboard::ensure on).
        components::BillboardComponent* resolveBillboard(const value::Value& arg)
        {
            int64_t id = extractInt64(arg);
            if (id < 0) return nullptr;

            auto& registry = scene::EntityRegistry::getRegistry();
            auto entity = services::internal::fromHandle(
                services::EntityHandle{static_cast<uint64_t>(id)});
            if (!registry.valid(entity) ||
                !registry.all_of<components::BillboardComponent>(entity))
            {
                return nullptr;
            }
            return &registry.get<components::BillboardComponent>(entity);
        }
    }

    void BillboardAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // _native_billboard_ensure(entityId) -> bool
        // Adds a BillboardComponent if absent (idempotent) and configures it as a
        // play-mode world marker: editorOnly=false (renders in play, see
        // FramePreparationSystem::gatherBillboardData), worldMarker=true (routed
        // through the GPU billboard path), world-space sizing with a 1x1 default.
        interpreter->registerNativeFunction("_native_billboard_ensure",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(false);
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(false);

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) return value::Value(false);

                if (!registry.all_of<components::BillboardComponent>(entity))
                {
                    // Fresh marker: world-space, 1x1, Billboard icon type (a script
                    // texture set via setTexture overrides it). Mirrors
                    // BillboardComponentService::addBillboardComponent defaults.
                    auto& comp = registry.emplace<components::BillboardComponent>(entity);
                    comp.iconType = components::BillboardIconType::Billboard;
                    comp.sizeMode = components::BillboardSizeMode::WorldSpace;
                    comp.size = glm::vec2(1.0f, 1.0f);
                    comp.selectable = true;
                    comp.editorOnly = false;
                    comp.worldMarker = true;
                }
                else
                {
                    // Existing component (e.g. an editor debug icon): promote to a
                    // play-mode world marker without disturbing user-set fields.
                    auto& comp = registry.get<components::BillboardComponent>(entity);
                    comp.editorOnly = false;
                    comp.worldMarker = true;
                }
                return value::Value(true);
            }});

        // _native_billboard_remove(entityId) -> bool
        interpreter->registerNativeFunction("_native_billboard_remove",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(false);
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(false);

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::BillboardComponent>(entity))
                    return value::Value(false);

                registry.remove<components::BillboardComponent>(entity);
                return value::Value(true);
            }});

        // _native_billboard_setTexture(entityId, vfImagePath) -> void
        interpreter->registerNativeFunction("_native_billboard_setTexture",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                std::string path = extractString(args[1], "Billboard.setTexture");
                // A non-empty texturePath wins over the atlas icon in the billboard
                // renderer (BillboardPipeline keys on texturePath presence), so no
                // iconType change is needed here.
                comp->textureRef = asset::AssetRef::fromPath(path);
                return value::Value(std::monostate{});
            }});

        // _native_billboard_setTint(entityId, r, g, b, a) -> void
        interpreter->registerNativeFunction("_native_billboard_setTint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 5) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                comp->colorTint.r = extractFloat(args[1]);
                comp->colorTint.g = extractFloat(args[2]);
                comp->colorTint.b = extractFloat(args[3]);
                comp->colorTint.a = extractFloat(args[4]);
                return value::Value(std::monostate{});
            }});

        // _native_billboard_setSize(entityId, w, h) -> void
        interpreter->registerNativeFunction("_native_billboard_setSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 3) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                comp->size.x = extractFloat(args[1]);
                comp->size.y = extractFloat(args[2]);
                return value::Value(std::monostate{});
            }});

        // _native_billboard_setFlipbook(entityId, cols, rows, frameRate) -> void
        interpreter->registerNativeFunction("_native_billboard_setFlipbook",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                int64_t cols = extractInt64(args[1]);
                int64_t rows = extractInt64(args[2]);
                comp->flipbookColumns = cols > 0 ? static_cast<uint32_t>(cols) : 1u;
                comp->flipbookRows = rows > 0 ? static_cast<uint32_t>(rows) : 1u;
                comp->flipbookFrameRate = extractFloat(args[3]);
                return value::Value(std::monostate{});
            }});

        // _native_billboard_setScroll(entityId, u, v) -> void
        interpreter->registerNativeFunction("_native_billboard_setScroll",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 3) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                comp->scrollU = extractFloat(args[1]);
                comp->scrollV = extractFloat(args[2]);
                return value::Value(std::monostate{});
            }});

        // _native_billboard_setPulse(entityId, amplitude, frequency) -> void
        interpreter->registerNativeFunction("_native_billboard_setPulse",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 3) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                comp->pulseAmplitude = extractFloat(args[1]);
                comp->pulseFrequency = extractFloat(args[2]);
                return value::Value(std::monostate{});
            }});

        // _native_billboard_setSpin(entityId, radPerSec) -> void
        interpreter->registerNativeFunction("_native_billboard_setSpin",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                comp->spinSpeed = extractFloat(args[1]);
                return value::Value(std::monostate{});
            }});

        // _native_billboard_setVisible(entityId, visible) -> void
        // Gates play-mode rendering. gatherBillboardData skips billboards with
        // editorOnly=true while playing, so visible=false maps to editorOnly=true
        // (hidden in play) and visible=true to editorOnly=false (rendered).
        interpreter->registerNativeFunction("_native_billboard_setVisible",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                comp->editorOnly = !extractBool(args[1]);
                return value::Value(std::monostate{});
            }});

        // _native_billboard_has(entityId) -> bool
        // True only for a play-mode (non-editorOnly) billboard marker.
        interpreter->registerNativeFunction("_native_billboard_has",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(false);
                auto* comp = resolveBillboard(args[0]);
                return value::Value(comp != nullptr && !comp->editorOnly);
            }});

        // _native_billboard_setLoop(entityId, loop) -> void
        // loop=true  -> flipbook loops continuously (default).
        // loop=false -> flipbook plays once then holds the last frame.
        // Scroll/pulse/spin remain continuous regardless of this toggle.
        interpreter->registerNativeFunction("_native_billboard_setLoop",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                comp->loopAnimation = extractBool(args[1]);
                return value::Value(std::monostate{});
            }});

        // _native_billboard_restart(entityId) -> void
        // Re-anchor the animation origin to "now" so the flipbook (and the
        // continuous scroll/pulse/spin phase) restart from the current engine
        // time. Required for play-once animations and pooled/reused markers.
        // Uses engineTime::Timer::getElapsedTime() — the same clock that feeds
        // camera.time (ViewPort.cpp / RuntimeHandler.cpp) and the GPU billboard
        // pc.uTime, so the CPU restart point and the shader's t origin agree.
        interpreter->registerNativeFunction("_native_billboard_restart",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(std::monostate{});
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(std::monostate{});

                comp->animStartTime = static_cast<float>(engineTime::Timer::getElapsedTime());
                return value::Value(std::monostate{});
            }});

        // _native_billboard_isAnimationFinished(entityId) -> bool
        // True only for a one-shot (loopAnimation==false) flipbook whose single
        // cycle has completed: (engineTime - animStartTime)*frameRate >= cols*rows.
        // Returns false for looping or non-animated billboards (they never finish).
        interpreter->registerNativeFunction("_native_billboard_isAnimationFinished",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(false);
                auto* comp = resolveBillboard(args[0]);
                if (!comp) return value::Value(false);

                const float t = static_cast<float>(engineTime::Timer::getElapsedTime()) - comp->animStartTime;
                return value::Value(render::flipbookFinished(
                    t, comp->flipbookFrameRate,
                    static_cast<int>(comp->flipbookColumns),
                    static_cast<int>(comp->flipbookRows),
                    comp->loopAnimation));
            }});
    }
}
