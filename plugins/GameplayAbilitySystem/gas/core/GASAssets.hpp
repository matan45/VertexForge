#pragma once

// Gameplay Ability System (VK-816) — asset (de)serialization.
// ENGINE-FREE: depends only on the C++ standard library and nlohmann/json.
//
// Four asset types, each with fromJson / toJson / createDefault / load / save:
//   .vfAbility       -> AbilitySpec      (AbilityAsset)
//   .vfGameplayEffect-> GameplayEffectSpec (EffectAsset)
//   .vfGameplayCue   -> GameplayCueSpec   (CueAsset)
//   .vfGameplayTags  -> TagTable          (TagsAsset)
//
// Parsing is TOLERANT: missing/null fields fall back to defaults; enums are
// strings (unknown values fall back to the type's default). Floats round-trip
// exactly (float -> JSON double -> float is lossless). load/save use plain
// std::ifstream/ofstream + nlohmann/json (no engine I/O helpers).

#include "AbilitySpec.hpp"
#include "Effects.hpp"
#include "Tags.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gas
{
    inline constexpr const char* GAS_FORMAT_VERSION = "1.0";

    // Trigger point for a gameplay cue (cosmetic feedback the engine dispatches).
    enum class CueTrigger
    {
        OnApply,
        OnRemove,
        OnActive,
        OnExecute
    };

    // Parsed form of a .vfGameplayCue asset. Purely descriptive — the engine maps
    // vfxPath/audioPath to its own systems.
    struct GameplayCueSpec
    {
        std::string version;
        std::string id;
        CueTrigger trigger = CueTrigger::OnApply;
        std::string vfxPath;
        std::string attachSocket;
        std::string audioPath;
        std::map<std::string, float> params;

        bool operator==(const GameplayCueSpec& o) const
        {
            return version == o.version && id == o.id && trigger == o.trigger &&
                   vfxPath == o.vfxPath && attachSocket == o.attachSocket &&
                   audioPath == o.audioPath && params == o.params;
        }
        bool operator!=(const GameplayCueSpec& o) const { return !(*this == o); }
    };

    // ----------------------------------------------------------------------------
    // Enum <-> string
    // ----------------------------------------------------------------------------
    inline const char* durationPolicyToString(DurationPolicy p)
    {
        switch (p)
        {
        case DurationPolicy::Instant: return "Instant";
        case DurationPolicy::Duration: return "Duration";
        case DurationPolicy::Infinite: return "Infinite";
        }
        return "Instant";
    }
    inline DurationPolicy durationPolicyFromString(const std::string& s, DurationPolicy def = DurationPolicy::Instant)
    {
        if (s == "Instant") return DurationPolicy::Instant;
        if (s == "Duration") return DurationPolicy::Duration;
        if (s == "Infinite") return DurationPolicy::Infinite;
        return def;
    }

    inline const char* modifierOpToString(ModifierOp o)
    {
        switch (o)
        {
        case ModifierOp::Add: return "Add";
        case ModifierOp::Multiply: return "Multiply";
        case ModifierOp::Override: return "Override";
        }
        return "Add";
    }
    inline ModifierOp modifierOpFromString(const std::string& s, ModifierOp def = ModifierOp::Add)
    {
        if (s == "Add") return ModifierOp::Add;
        if (s == "Multiply") return ModifierOp::Multiply;
        if (s == "Override") return ModifierOp::Override;
        return def;
    }

    inline const char* stackingPolicyToString(StackingPolicy p)
    {
        switch (p)
        {
        case StackingPolicy::None: return "None";
        case StackingPolicy::BySource: return "BySource";
        case StackingPolicy::ByTarget: return "ByTarget";
        }
        return "None";
    }
    inline StackingPolicy stackingPolicyFromString(const std::string& s, StackingPolicy def = StackingPolicy::None)
    {
        if (s == "None") return StackingPolicy::None;
        if (s == "BySource") return StackingPolicy::BySource;
        if (s == "ByTarget") return StackingPolicy::ByTarget;
        return def;
    }

    inline const char* stackExpirationToString(StackExpiration e)
    {
        switch (e)
        {
        case StackExpiration::ClearStack: return "ClearStack";
        case StackExpiration::RemoveSingle: return "RemoveSingle";
        }
        return "ClearStack";
    }
    inline StackExpiration stackExpirationFromString(const std::string& s, StackExpiration def = StackExpiration::ClearStack)
    {
        if (s == "ClearStack") return StackExpiration::ClearStack;
        if (s == "RemoveSingle") return StackExpiration::RemoveSingle;
        return def;
    }

    inline const char* activationPolicyToString(ActivationPolicy p)
    {
        switch (p)
        {
        case ActivationPolicy::OnPressed: return "OnPressed";
        case ActivationPolicy::OnHeld: return "OnHeld";
        case ActivationPolicy::OnGranted: return "OnGranted";
        case ActivationPolicy::Manual: return "Manual";
        }
        return "OnPressed";
    }
    inline ActivationPolicy activationPolicyFromString(const std::string& s, ActivationPolicy def = ActivationPolicy::OnPressed)
    {
        if (s == "OnPressed") return ActivationPolicy::OnPressed;
        if (s == "OnHeld") return ActivationPolicy::OnHeld;
        if (s == "OnGranted") return ActivationPolicy::OnGranted;
        if (s == "Manual") return ActivationPolicy::Manual;
        return def;
    }

    inline const char* targetingTypeToString(TargetingType t)
    {
        switch (t)
        {
        case TargetingType::Self: return "Self";
        case TargetingType::Single: return "Single";
        case TargetingType::AOE: return "AOE";
        }
        return "Self";
    }
    inline TargetingType targetingTypeFromString(const std::string& s, TargetingType def = TargetingType::Self)
    {
        if (s == "Self") return TargetingType::Self;
        if (s == "Single") return TargetingType::Single;
        if (s == "AOE") return TargetingType::AOE;
        return def;
    }

    inline const char* taskTypeToString(TaskType t)
    {
        switch (t)
        {
        case TaskType::WaitDelay: return "WaitDelay";
        case TaskType::PlayMontage: return "PlayMontage";
        case TaskType::ApplyEffect: return "ApplyEffect";
        case TaskType::SpawnCue: return "SpawnCue";
        }
        return "WaitDelay";
    }
    inline TaskType taskTypeFromString(const std::string& s, TaskType def = TaskType::WaitDelay)
    {
        if (s == "WaitDelay") return TaskType::WaitDelay;
        if (s == "PlayMontage") return TaskType::PlayMontage;
        if (s == "ApplyEffect") return TaskType::ApplyEffect;
        if (s == "SpawnCue") return TaskType::SpawnCue;
        return def;
    }

    inline const char* cueTriggerToString(CueTrigger t)
    {
        switch (t)
        {
        case CueTrigger::OnApply: return "OnApply";
        case CueTrigger::OnRemove: return "OnRemove";
        case CueTrigger::OnActive: return "OnActive";
        case CueTrigger::OnExecute: return "OnExecute";
        }
        return "OnApply";
    }
    inline CueTrigger cueTriggerFromString(const std::string& s, CueTrigger def = CueTrigger::OnApply)
    {
        if (s == "OnApply") return CueTrigger::OnApply;
        if (s == "OnRemove") return CueTrigger::OnRemove;
        if (s == "OnActive") return CueTrigger::OnActive;
        if (s == "OnExecute") return CueTrigger::OnExecute;
        return def;
    }

    // ----------------------------------------------------------------------------
    // Tolerant JSON field readers
    // ----------------------------------------------------------------------------
    namespace detail
    {
        template <class T>
        T jget(const nlohmann::json& j, const char* key, const T& def)
        {
            auto it = j.find(key);
            if (it == j.end() || it->is_null())
                return def;
            try
            {
                return it->get<T>();
            }
            catch (...)
            {
                return def;
            }
        }

        inline std::string jstr(const nlohmann::json& j, const char* key, const std::string& def = "")
        {
            return jget<std::string>(j, key, def);
        }
        inline float jflt(const nlohmann::json& j, const char* key, float def = 0.0f)
        {
            return jget<float>(j, key, def);
        }
        inline int jint(const nlohmann::json& j, const char* key, int def = 0)
        {
            return jget<int>(j, key, def);
        }
        inline bool jbool(const nlohmann::json& j, const char* key, bool def = false)
        {
            return jget<bool>(j, key, def);
        }

        inline std::vector<std::string> jstrArray(const nlohmann::json& j, const char* key)
        {
            std::vector<std::string> out;
            auto it = j.find(key);
            if (it != j.end() && it->is_array())
                for (const auto& e : *it)
                    if (e.is_string())
                        out.push_back(e.get<std::string>());
            return out;
        }

        // --- attribute modifier ---
        inline nlohmann::json modifierToJson(const AttributeModifier& m)
        {
            return nlohmann::json{
                {"attribute", m.attribute},
                {"op", modifierOpToString(m.op)},
                {"magnitude", m.magnitude}};
        }
        inline AttributeModifier modifierFromJson(const nlohmann::json& j)
        {
            AttributeModifier m;
            m.attribute = jstr(j, "attribute");
            m.op = modifierOpFromString(jstr(j, "op", "Add"));
            m.magnitude = jflt(j, "magnitude", 0.0f);
            return m;
        }
        inline std::vector<AttributeModifier> modifiersFromJson(const nlohmann::json& j, const char* key)
        {
            std::vector<AttributeModifier> out;
            auto it = j.find(key);
            if (it != j.end() && it->is_array())
                for (const auto& e : *it)
                    if (e.is_object())
                        out.push_back(modifierFromJson(e));
            return out;
        }
    } // namespace detail

    // ----------------------------------------------------------------------------
    // GameplayEffect asset (.vfGameplayEffect)
    // ----------------------------------------------------------------------------
    class EffectAsset
    {
    public:
        static nlohmann::json toJson(const GameplayEffectSpec& s)
        {
            using nlohmann::json;
            json mods = json::array();
            for (const auto& m : s.modifiers)
                mods.push_back(detail::modifierToJson(m));

            json stacking{
                {"policy", stackingPolicyToString(s.stacking.policy)},
                {"limit", s.stacking.limit},
                {"durationRefresh", s.stacking.durationRefresh},
                {"expiration", stackExpirationToString(s.stacking.expiration)}};

            return json{
                {"version", s.version.empty() ? GAS_FORMAT_VERSION : s.version},
                {"id", s.id},
                {"displayName", s.displayName},
                {"durationPolicy", durationPolicyToString(s.durationPolicy)},
                {"duration", s.duration},
                {"period", s.period},
                {"modifiers", mods},
                {"grantedTags", s.grantedTags},
                {"ongoingRequiredTags", s.ongoingRequiredTags},
                {"removalTags", s.removalTags},
                {"stacking", stacking},
                {"cueIds", s.cueIds}};
        }

        static GameplayEffectSpec fromJson(const nlohmann::json& j)
        {
            using namespace detail;
            GameplayEffectSpec s;
            s.version = jstr(j, "version", GAS_FORMAT_VERSION);
            s.id = jstr(j, "id");
            s.displayName = jstr(j, "displayName");
            s.durationPolicy = durationPolicyFromString(jstr(j, "durationPolicy", "Instant"));
            s.duration = jflt(j, "duration", 0.0f);
            s.period = jflt(j, "period", 0.0f);
            s.modifiers = modifiersFromJson(j, "modifiers");
            s.grantedTags = jstrArray(j, "grantedTags");
            s.ongoingRequiredTags = jstrArray(j, "ongoingRequiredTags");
            s.removalTags = jstrArray(j, "removalTags");

            auto sit = j.find("stacking");
            if (sit != j.end() && sit->is_object())
            {
                const auto& sj = *sit;
                s.stacking.policy = stackingPolicyFromString(jstr(sj, "policy", "None"));
                s.stacking.limit = jint(sj, "limit", 1);
                s.stacking.durationRefresh = jbool(sj, "durationRefresh", false);
                s.stacking.expiration = stackExpirationFromString(jstr(sj, "expiration", "ClearStack"));
            }

            s.cueIds = jstrArray(j, "cueIds");
            return s;
        }

        static GameplayEffectSpec createDefault(const std::string& name = "New Effect")
        {
            GameplayEffectSpec s;
            s.version = GAS_FORMAT_VERSION;
            s.id = name;
            s.displayName = name;
            s.durationPolicy = DurationPolicy::Instant;
            s.modifiers.push_back({"Health", ModifierOp::Add, 10.0f, 0});
            return s;
        }

        static std::optional<GameplayEffectSpec> load(std::string_view path)
        {
            std::ifstream f{std::string(path)};
            if (!f.is_open())
                return std::nullopt;
            nlohmann::json j;
            try
            {
                f >> j;
            }
            catch (...)
            {
                return std::nullopt;
            }
            if (!j.is_object())
                return std::nullopt;
            return fromJson(j);
        }

        static bool save(std::string_view path, const GameplayEffectSpec& s)
        {
            std::ofstream f{std::string(path)};
            if (!f.is_open())
                return false;
            try
            {
                f << toJson(s).dump(4);
            }
            catch (...)
            {
                return false;
            }
            return true;
        }
    };

    // ----------------------------------------------------------------------------
    // Ability asset (.vfAbility)
    // ----------------------------------------------------------------------------
    class AbilityAsset
    {
    public:
        static nlohmann::json toJson(const AbilitySpec& s)
        {
            using nlohmann::json;
            json tasks = json::array();
            for (const auto& t : s.tasks)
                tasks.push_back(json{
                    {"type", taskTypeToString(t.type)},
                    {"param", t.param},
                    {"delay", t.delay}});

            return json{
                {"version", s.version.empty() ? GAS_FORMAT_VERSION : s.version},
                {"id", s.id},
                {"displayName", s.displayName},
                {"abilityTags", s.abilityTags},
                {"activationRequiredTags", s.activationRequiredTags},
                {"activationBlockedTags", s.activationBlockedTags},
                {"activationOwnedTags", s.activationOwnedTags},
                {"targetRequiredTags", s.targetRequiredTags},
                {"cancelAbilitiesWithTag", s.cancelAbilitiesWithTag},
                {"cost", {{"effectId", s.cost.effectId}}},
                {"cooldown", {{"duration", s.cooldown.duration}, {"cooldownTags", s.cooldown.cooldownTags}, {"effectId", s.cooldown.effectId}}},
                {"activation", {{"inputAction", s.activation.inputAction}, {"policy", activationPolicyToString(s.activation.policy)}}},
                {"targeting", {{"type", targetingTypeToString(s.targeting.type)}, {"range", s.targeting.range}, {"radius", s.targeting.radius}}},
                {"tasks", tasks},
                {"effectsOnActivate", s.effectsOnActivate},
                {"cueIds", s.cueIds}};
        }

        static AbilitySpec fromJson(const nlohmann::json& j)
        {
            using namespace detail;
            AbilitySpec s;
            s.version = jstr(j, "version", GAS_FORMAT_VERSION);
            s.id = jstr(j, "id");
            s.displayName = jstr(j, "displayName");
            s.abilityTags = jstrArray(j, "abilityTags");
            s.activationRequiredTags = jstrArray(j, "activationRequiredTags");
            s.activationBlockedTags = jstrArray(j, "activationBlockedTags");
            s.activationOwnedTags = jstrArray(j, "activationOwnedTags");
            s.targetRequiredTags = jstrArray(j, "targetRequiredTags");
            s.cancelAbilitiesWithTag = jstrArray(j, "cancelAbilitiesWithTag");

            auto cit = j.find("cost");
            if (cit != j.end() && cit->is_object())
                s.cost.effectId = jstr(*cit, "effectId");

            auto cdit = j.find("cooldown");
            if (cdit != j.end() && cdit->is_object())
            {
                s.cooldown.duration = jflt(*cdit, "duration", 0.0f);
                s.cooldown.cooldownTags = jstrArray(*cdit, "cooldownTags");
                s.cooldown.effectId = jstr(*cdit, "effectId");
            }

            auto ait = j.find("activation");
            if (ait != j.end() && ait->is_object())
            {
                s.activation.inputAction = jstr(*ait, "inputAction");
                s.activation.policy = activationPolicyFromString(jstr(*ait, "policy", "OnPressed"));
            }

            auto tit = j.find("targeting");
            if (tit != j.end() && tit->is_object())
            {
                s.targeting.type = targetingTypeFromString(jstr(*tit, "type", "Self"));
                s.targeting.range = jflt(*tit, "range", 0.0f);
                s.targeting.radius = jflt(*tit, "radius", 0.0f);
            }

            auto tsit = j.find("tasks");
            if (tsit != j.end() && tsit->is_array())
                for (const auto& e : *tsit)
                    if (e.is_object())
                    {
                        AbilityTask task;
                        task.type = taskTypeFromString(jstr(e, "type", "WaitDelay"));
                        task.param = jstr(e, "param");
                        if (task.param.empty())
                            for (const char* k : {"effectId", "montage", "montagePath", "cueId", "asset"})
                            {
                                std::string v = jstr(e, k);
                                if (!v.empty())
                                {
                                    task.param = v;
                                    break;
                                }
                            }
                        task.delay = jflt(e, "delay", 0.0f);
                        s.tasks.push_back(task);
                    }

            s.effectsOnActivate = jstrArray(j, "effectsOnActivate");
            s.cueIds = jstrArray(j, "cueIds");
            return s;
        }

        static AbilitySpec createDefault(const std::string& name = "New Ability")
        {
            AbilitySpec s;
            s.version = GAS_FORMAT_VERSION;
            s.id = name;
            s.displayName = name;
            s.activation.policy = ActivationPolicy::OnPressed;
            s.targeting.type = TargetingType::Self;
            return s;
        }

        static std::optional<AbilitySpec> load(std::string_view path)
        {
            std::ifstream f{std::string(path)};
            if (!f.is_open())
                return std::nullopt;
            nlohmann::json j;
            try
            {
                f >> j;
            }
            catch (...)
            {
                return std::nullopt;
            }
            if (!j.is_object())
                return std::nullopt;
            return fromJson(j);
        }

        static bool save(std::string_view path, const AbilitySpec& s)
        {
            std::ofstream f{std::string(path)};
            if (!f.is_open())
                return false;
            try
            {
                f << toJson(s).dump(4);
            }
            catch (...)
            {
                return false;
            }
            return true;
        }
    };

    // ----------------------------------------------------------------------------
    // GameplayCue asset (.vfGameplayCue)
    // ----------------------------------------------------------------------------
    class CueAsset
    {
    public:
        static nlohmann::json toJson(const GameplayCueSpec& s)
        {
            using nlohmann::json;
            json params = json::object();
            for (const auto& [k, v] : s.params)
                params[k] = v;

            return json{
                {"version", s.version.empty() ? GAS_FORMAT_VERSION : s.version},
                {"id", s.id},
                {"trigger", cueTriggerToString(s.trigger)},
                {"vfxPath", s.vfxPath},
                {"attachSocket", s.attachSocket},
                {"audioPath", s.audioPath},
                {"params", params}};
        }

        static GameplayCueSpec fromJson(const nlohmann::json& j)
        {
            using namespace detail;
            GameplayCueSpec s;
            s.version = jstr(j, "version", GAS_FORMAT_VERSION);
            s.id = jstr(j, "id");
            s.trigger = cueTriggerFromString(jstr(j, "trigger", "OnApply"));
            s.vfxPath = jstr(j, "vfxPath");
            s.attachSocket = jstr(j, "attachSocket");
            s.audioPath = jstr(j, "audioPath");

            auto pit = j.find("params");
            if (pit != j.end() && pit->is_object())
                for (const auto& [k, v] : pit->items())
                    if (v.is_number())
                        s.params[k] = v.get<float>();
            return s;
        }

        static GameplayCueSpec createDefault(const std::string& name = "New Cue")
        {
            GameplayCueSpec s;
            s.version = GAS_FORMAT_VERSION;
            s.id = name;
            s.trigger = CueTrigger::OnApply;
            return s;
        }

        static std::optional<GameplayCueSpec> load(std::string_view path)
        {
            std::ifstream f{std::string(path)};
            if (!f.is_open())
                return std::nullopt;
            nlohmann::json j;
            try
            {
                f >> j;
            }
            catch (...)
            {
                return std::nullopt;
            }
            if (!j.is_object())
                return std::nullopt;
            return fromJson(j);
        }

        static bool save(std::string_view path, const GameplayCueSpec& s)
        {
            std::ofstream f{std::string(path)};
            if (!f.is_open())
                return false;
            try
            {
                f << toJson(s).dump(4);
            }
            catch (...)
            {
                return false;
            }
            return true;
        }
    };

    // ----------------------------------------------------------------------------
    // GameplayTags asset (.vfGameplayTags) — the project tag dictionary (TagTable)
    // ----------------------------------------------------------------------------
    class TagsAsset
    {
    public:
        static nlohmann::json toJson(const TagTable& t)
        {
            using nlohmann::json;
            json tags = json::array();
            for (const auto& def : t.definitions())
                tags.push_back(json{{"tag", def.tag}, {"comment", def.comment}});

            return json{
                {"version", GAS_FORMAT_VERSION},
                {"tags", tags}};
        }

        static TagTable fromJson(const nlohmann::json& j)
        {
            TagTable t;
            auto it = j.find("tags");
            if (it != j.end() && it->is_array())
            {
                for (const auto& e : *it)
                {
                    if (e.is_string())
                        t.declare(e.get<std::string>());
                    else if (e.is_object())
                        t.declare(detail::jstr(e, "tag"), detail::jstr(e, "comment"));
                }
            }
            return t;
        }

        static TagTable createDefault(const std::string& /*name*/ = "Project Tags")
        {
            TagTable t;
            t.declare("Character", "Root tag for character-state tags");
            t.declare("Character.Stunned", "Cannot act while present");
            t.declare("Character.Dead", "Removed from play");
            t.declare("Ability", "Root tag for ability identity tags");
            t.declare("Cooldown", "Root tag for ability cooldown tags");
            t.declare("State", "Root tag for generic gameplay state");
            return t;
        }

        static std::optional<TagTable> load(std::string_view path)
        {
            std::ifstream f{std::string(path)};
            if (!f.is_open())
                return std::nullopt;
            nlohmann::json j;
            try
            {
                f >> j;
            }
            catch (...)
            {
                return std::nullopt;
            }
            if (!j.is_object())
                return std::nullopt;
            return fromJson(j);
        }

        static bool save(std::string_view path, const TagTable& t)
        {
            std::ofstream f{std::string(path)};
            if (!f.is_open())
                return false;
            try
            {
                f << toJson(t).dump(4);
            }
            catch (...)
            {
                return false;
            }
            return true;
        }
    };
}
