#include "AnimationAuthoringAdapter.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/animation/AnimationAuthoringEvents.hpp"
#include "../../services/events/editor/EditorModeEvents.hpp"
#include "../../services/events/project/ResourceEvents.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "../../graphics/animation/RuntimeAnimatorSystem.hpp"
#include "animator/AnimatorAsset.hpp"
#include "archive/VFPakFormat.hpp"
#include "asset/AssetRef.hpp"
#include "resource/AnimationResource.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "retargeting/RetargetAsset.hpp"
#include "scene/EntityRegistry.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace core
{
    namespace
    {
        namespace fs = std::filesystem;
        namespace authoring = events::animationAuthoring;

        // AnimatorAsset::readJsonFromFile refuses anything larger, so never pull a bigger
        // file into memory just to hash it.
        constexpr uintmax_t maxAnimatorFileBytes = 10 * 1024 * 1024;

        bool hasExtension(const std::string& path, std::string_view lowercaseExtension)
        {
            std::string extension = fs::path(path).extension().string();
            std::ranges::transform(extension, extension.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == lowercaseExtension;
        }

        // The file's bytes, or nullopt with `error` set when it is missing, too large or unreadable.
        std::optional<std::vector<uint8_t>> readFileBytes(const std::string& path, std::string& error)
        {
            std::error_code ec;
            if (!fs::exists(path, ec))
            {
                error = std::format("'{}' does not exist", path);
                return std::nullopt;
            }
            if (!fs::is_regular_file(path, ec))
            {
                error = std::format("'{}' is not a file", path);
                return std::nullopt;
            }

            const uintmax_t size = fs::file_size(path, ec);
            if (ec)
            {
                error = std::format("'{}' could not be read: {}", path, ec.message());
                return std::nullopt;
            }
            if (size > maxAnimatorFileBytes)
            {
                error = std::format("'{}' is too large for an animator ({} bytes)", path, size);
                return std::nullopt;
            }

            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open() ||
                (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))))
            {
                error = std::format("'{}' could not be read", path);
                return std::nullopt;
            }
            return bytes;
        }

        // 64-bit FNV-1a of the file bytes: what LoadAnimatorAssetQuery reports and
        // SaveAnimatorAssetCommand::expectedContentHash is compared against.
        uint64_t contentHashOf(const std::vector<uint8_t>& bytes)
        {
            return archive::hashBytes(bytes.data(), bytes.size());
        }

        services::AnimatorAssetLoadResult loadAnimatorAsset(const std::string& path)
        {
            services::AnimatorAssetLoadResult result;
            if (path.empty())
            {
                result.error = "No animator path given";
                return result;
            }

            // Hash before parsing: if the file changes in between, the stale hash makes the
            // next guarded save refuse instead of overwriting the newer file.
            const auto bytes = readFileBytes(path, result.error);
            if (!bytes)
                return result;
            const uint64_t contentHash = contentHashOf(*bytes);

            auto data = animator::AnimatorAsset::load(path);
            if (!data)
            {
                result.error = std::format("'{}' could not be parsed as an animator (see the log)", path);
                return result;
            }

            result.data = std::move(*data);
            result.contentHash = contentHash;
            result.success = true;
            return result;
        }

        services::AnimatorAssetSaveResult saveAnimatorAsset(const authoring::SaveAnimatorAssetCommand& cmd)
        {
            services::AnimatorAssetSaveResult result;
            const std::string& path = cmd.path;
            if (path.empty())
            {
                result.error = "No animator path given";
                return result;
            }
            // The last guard before a JSON write: never clobber a clip, mesh or other asset.
            if (!hasExtension(path, ".vfanimator"))
            {
                result.error = std::format("'{}' is not a .vfAnimator path", path);
                return result;
            }

            std::error_code ec;
            const bool existed = fs::exists(path, ec);
            if (cmd.createNew && existed)
            {
                result.error = std::format("'{}' already exists", path);
                return result;
            }

            if (cmd.expectedContentHash.has_value())
            {
                if (!existed)
                {
                    result.error = std::format("'{}' no longer exists", path);
                    return result;
                }

                const auto current = readFileBytes(path, result.error);
                if (!current)
                    return result;
                if (contentHashOf(*current) != *cmd.expectedContentHash)
                {
                    result.error = std::format("'{}' changed on disk since it was read", path);
                    return result;
                }
            }

            const fs::path parent = fs::path(path).parent_path();
            if (!parent.empty())
            {
                fs::create_directories(parent, ec);
                if (ec)
                {
                    result.error = std::format("could not create the folder '{}': {}", parent.string(), ec.message());
                    return result;
                }
            }

            if (!animator::AnimatorAsset::save(path, cmd.data))
            {
                result.error = std::format("'{}' could not be written (see the log)", path);
                return result;
            }

            // Hash exactly what was written, before any subscriber reacts to the save.
            std::string readError;
            const auto written = readFileBytes(path, readError);

            // The asset database registers the GUID/.vfmeta, live animators using the file
            // reload, and an open Animator Editor reloads or shows a conflict.
            events::resource::AssetSavedNotification notification;
            notification.filePath = path;
            events::EventDispatcher::instance().publish(notification);

            if (!written)
            {
                result.error = std::format("saved, but the file could not be read back: {}", readError);
                return result;
            }

            result.success = true;
            result.created = !existed;
            result.contentHash = contentHashOf(*written);
            return result;
        }

        services::MeshSkeletonInfo getMeshSkeleton(const std::string& meshPath)
        {
            services::MeshSkeletonInfo info;
            if (meshPath.empty())
            {
                info.error = "No mesh path given";
                return info;
            }

            std::error_code ec;
            if (!fs::exists(meshPath, ec))
            {
                info.error = std::format("'{}' does not exist", meshPath);
                return info;
            }

            // A fresh read: the runtime skeleton cache can be stale after socket/IK edits.
            auto stream = resource::MeshStreamResource::openStream(meshPath);
            if (!stream)
            {
                info.error = std::format("'{}' could not be opened as a .vfMesh", meshPath);
                return info;
            }

            // A skeleton block can exist with skinning off (static mesh): readSkeleton then
            // succeeds with no bones and stops before the sockets.
            resource::SkeletonData skeleton;
            if (stream->hasSkeletonData() && !stream->readSkeleton(skeleton))
            {
                info.error = std::format("the skeleton of '{}' could not be read", meshPath);
                return info;
            }

            if (skeleton.hasBones())
            {
                info.skinned = true;
                info.boneNames.reserve(skeleton.bones.size());
                info.parents.reserve(skeleton.bones.size());
                for (const auto& bone : skeleton.bones)
                {
                    info.boneNames.push_back(bone.name);
                    info.parents.push_back(bone.parentIndex);
                }
                // Socket boneIndex values were resolved against these bones by the reader.
                info.sockets = std::move(skeleton.sockets);
                info.ikChains = std::move(skeleton.ikChains);
            }
            else if (stream->hasSocketData())
            {
                // Static mesh: the SOK2 block, boneIndex -1 (AnimationDataCache::loadStaticSockets).
                resource::SkeletonData staticSockets;
                if (!stream->readSockets(staticSockets))
                {
                    info.error = std::format("the sockets of '{}' could not be read", meshPath);
                    return info;
                }
                info.sockets = std::move(staticSockets.sockets);
            }

            info.success = true;
            return info;
        }

        std::vector<services::AnimationClipSummary> getClipSummaries(const std::vector<std::string>& paths)
        {
            std::vector<services::AnimationClipSummary> summaries;
            summaries.reserve(paths.size());
            for (const std::string& path : paths)
            {
                resource::AnimationClipHeader header = resource::AnimationResource::readClipSummary(path);

                services::AnimationClipSummary& summary = summaries.emplace_back();
                summary.path = path;
                if (!header.success)
                {
                    summary.error = std::move(header.error);
                    continue;
                }

                summary.success = true;
                summary.name = std::move(header.name);
                summary.durationSeconds = header.durationSeconds;
                summary.ticksPerSecond = header.ticksPerSecond;
                summary.channelNames = std::move(header.channelNames);
                summary.eventCount = header.eventCount;
            }
            return summaries;
        }

        // Loads one rig of a retarget map the way RuntimeAnimatorSystem does (GUID hex ->
        // AssetDatabase path), but straight from disk rather than the ResourceManager cache.
        bool loadRetargetRig(const std::string& guidHex, std::string_view role,
                             retargeting::HumanoidRigData& outRig, std::string& error)
        {
            // Copy: resolve() returns a reference into the temporary AssetRef.
            const std::string rigPath = asset::AssetRef::fromHexString(guidHex).resolve();
            if (rigPath.empty())
            {
                error = std::format("the {} rig ({}) does not resolve to a .vfrig file", role,
                                    guidHex.empty() ? std::string("not set") : guidHex);
                return false;
            }

            auto rig = retargeting::HumanoidRigAsset::load(rigPath);
            if (!rig)
            {
                error = std::format("the {} rig '{}' could not be read", role, rigPath);
                return false;
            }

            outRig = std::move(*rig);
            return true;
        }

        services::RetargetBindingInfo getRetargetBinding(const std::string& retargetPath)
        {
            services::RetargetBindingInfo info;
            if (retargetPath.empty())
            {
                info.error = "No retarget map path given";
                return info;
            }

            auto map = retargeting::RetargetMapAsset::load(retargetPath);
            if (!map)
            {
                info.error = std::format("'{}' could not be read as a retarget map", retargetPath);
                return info;
            }
            info.map = std::move(*map);

            // The runtime builds no retarget context unless both rigs load.
            if (!loadRetargetRig(info.map.sourceRigAssetGuid, "source", info.sourceRig, info.error) ||
                !loadRetargetRig(info.map.targetRigAssetGuid, "target", info.targetRig, info.error))
            {
                return info;
            }

            info.success = true;
            return info;
        }

        services::AnimationEditPreviewResult startEditPreview(services::EntityHandle entity,
                                                              const services::AnimationEditPreviewRequest& request)
        {
            services::AnimationEditPreviewResult refused;
            if (events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{}))
            {
                refused.error = "The animation preview only runs in edit mode; stop Play mode first";
                return refused;
            }

            if (!services::internal::isValidHandle(entity, scene::EntityRegistry::getRegistry()))
            {
                refused.error = "The entity does not exist";
                return refused;
            }

            return animation::RuntimeAnimatorSystem::instance().startEditPreview(
                services::internal::fromHandle(entity), request);
        }

        bool stopEditPreview(services::EntityHandle entity)
        {
            auto& animators = animation::RuntimeAnimatorSystem::instance();
            if (!entity.isValid())
                return animators.stopAllEditPreviews();
            // No liveness check: a destroyed entity's session is still stopped.
            return animators.stopEditPreview(services::internal::fromHandle(entity));
        }

        std::optional<services::AnimationEditPreviewStatus> getEditPreview(services::EntityHandle entity)
        {
            if (!entity.isValid())
                return std::nullopt;
            return animation::RuntimeAnimatorSystem::instance().getEditPreviewStatus(
                services::internal::fromHandle(entity));
        }
    }

    AnimationAuthoringAdapter::~AnimationAuthoringAdapter()
    {
        cleanUp();
    }

    void AnimationAuthoringAdapter::init()
    {
        if (handlersRegistered)
            return;
        handlersRegistered = true;

        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerQueryHandler<authoring::LoadAnimatorAssetQuery>(
            [](const authoring::LoadAnimatorAssetQuery& query) { return loadAnimatorAsset(query.path); });

        dispatcher.registerCommandHandler<authoring::SaveAnimatorAssetCommand>(
            [](const authoring::SaveAnimatorAssetCommand& cmd) { return saveAnimatorAsset(cmd); });

        dispatcher.registerQueryHandler<authoring::GetMeshSkeletonQuery>(
            [](const authoring::GetMeshSkeletonQuery& query) { return getMeshSkeleton(query.meshPath); });

        dispatcher.registerQueryHandler<authoring::GetAnimationClipSummariesQuery>(
            [](const authoring::GetAnimationClipSummariesQuery& query) { return getClipSummaries(query.paths); });

        dispatcher.registerQueryHandler<authoring::GetRetargetBindingQuery>(
            [](const authoring::GetRetargetBindingQuery& query) { return getRetargetBinding(query.retargetPath); });

        dispatcher.registerCommandHandler<authoring::StartAnimationEditPreviewCommand>(
            [](const authoring::StartAnimationEditPreviewCommand& cmd)
            {
                return startEditPreview(cmd.entity, cmd.request);
            });

        dispatcher.registerCommandHandler<authoring::StopAnimationEditPreviewCommand>(
            [](const authoring::StopAnimationEditPreviewCommand& cmd) { return stopEditPreview(cmd.entity); });

        dispatcher.registerQueryHandler<authoring::GetAnimationEditPreviewQuery>(
            [](const authoring::GetAnimationEditPreviewQuery& query) { return getEditPreview(query.entity); });
    }

    void AnimationAuthoringAdapter::cleanUp()
    {
        if (!handlersRegistered)
            return;
        handlersRegistered = false;

        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unregisterQueryHandler<authoring::LoadAnimatorAssetQuery>();
        dispatcher.unregisterCommandHandler<authoring::SaveAnimatorAssetCommand>();
        dispatcher.unregisterQueryHandler<authoring::GetMeshSkeletonQuery>();
        dispatcher.unregisterQueryHandler<authoring::GetAnimationClipSummariesQuery>();
        dispatcher.unregisterQueryHandler<authoring::GetRetargetBindingQuery>();
        dispatcher.unregisterCommandHandler<authoring::StartAnimationEditPreviewCommand>();
        dispatcher.unregisterCommandHandler<authoring::StopAnimationEditPreviewCommand>();
        dispatcher.unregisterQueryHandler<authoring::GetAnimationEditPreviewQuery>();
    }
}
