#pragma once
#include <animator/AnimatorTypes.hpp>
#include <animator/IKTypes.hpp>
#include <animator/SocketTypes.hpp>
#include <retargeting/RetargetTypes.hpp>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// VK-1656: data carried by events::animationAuthoring (services/events/animation/
// AnimationAuthoringEvents.hpp). Header-only value types.
namespace services
{
    struct AnimatorAssetLoadResult
    {
        bool success = false;
        std::string error;
        animator::AnimatorData data;
        uint64_t contentHash = 0; // hash of the file bytes `data` was parsed from
    };

    struct AnimatorAssetSaveResult
    {
        bool success = false;
        std::string error;
        bool created = false;     // the file did not exist before this save
        uint64_t contentHash = 0; // hash of the bytes now on disk
    };

    struct MeshSkeletonInfo
    {
        bool success = false;
        std::string error;
        bool skinned = false;                          // the mesh carries skeleton data
        std::vector<std::string> boneNames;            // skeleton order
        std::vector<int32_t> parents;                  // parent bone index per bone, -1 = root
        // Skinned: the skeleton's sockets. Static: the SOK2 block. boneIndex is -1 for a
        // static socket or a bone name the skeleton does not have.
        std::vector<animator::SocketDefinition> sockets;
        std::vector<animator::ik::IKChainConfig> ikChains; // chains authored into the mesh
    };

    struct AnimationClipSummary
    {
        std::string path;
        bool success = false;
        std::string error;
        std::string name;
        float durationSeconds = 0.0f;
        float ticksPerSecond = 0.0f;
        std::vector<std::string> channelNames; // one per animated node, file order
        uint32_t eventCount = 0;
    };

    struct RetargetBindingInfo
    {
        bool success = false;
        std::string error;
        retargeting::RetargetMapData map;
        retargeting::HumanoidRigData sourceRig;
        retargeting::HumanoidRigData targetRig;
    };

    enum class AnimationPreviewSource : uint8_t
    {
        State = 0, // a state of the entity's own animator (base layer)
        Clip       // any .vfAnim played on the entity's skeleton
    };

    struct AnimationEditPreviewRequest
    {
        AnimationPreviewSource source = AnimationPreviewSource::Clip;
        std::string state;            // State: state name in the base layer
        std::string clipPath;         // Clip: absolute .vfAnim path
        float normalizedTime = 0.0f;  // start time when playing, fixed pose time when paused (0-1)
        bool playing = true;
        float speed = 1.0f;           // Clip: playback speed. State: multiplies the state's own speed
        bool loop = true;             // Clip only
        // State only: values for the animator's parameters (e.g. a blend-tree axis).
        std::vector<std::pair<std::string, animator::AnimatorParameterValue>> parameters;
    };

    struct AnimationEditPreviewResult
    {
        bool success = false;
        std::string error;
        float durationSeconds = 0.0f;
        bool retargeted = false;
    };

    struct AnimationEditPreviewStatus
    {
        AnimationPreviewSource source = AnimationPreviewSource::Clip;
        std::string state;
        std::string clipPath;
        float normalizedTime = 0.0f;
        float durationSeconds = 0.0f;
        bool playing = false;
    };
}
