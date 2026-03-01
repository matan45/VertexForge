#include "SceneSerialization.hpp"
#include "../components/IKComponent.hpp"
#include "../animator/IKTypes.hpp"

namespace serialization
{
    static std::string constraintTypeToJsonString(animator::ik::JointConstraintType type)
    {
        switch (type)
        {
        case animator::ik::JointConstraintType::Hinge: return "hinge";
        case animator::ik::JointConstraintType::Cone: return "cone";
        case animator::ik::JointConstraintType::BallAndSocket: return "ballAndSocket";
        default: return "none";
        }
    }

    static animator::ik::JointConstraintType jsonStringToConstraintType(const std::string& str)
    {
        if (str == "hinge") return animator::ik::JointConstraintType::Hinge;
        if (str == "cone") return animator::ik::JointConstraintType::Cone;
        if (str == "ballAndSocket") return animator::ik::JointConstraintType::BallAndSocket;
        return animator::ik::JointConstraintType::None;
    }

    static json serializeConstraint(const animator::ik::JointConstraint& constraint)
    {
        json j;
        j["type"] = constraintTypeToJsonString(constraint.type);

        switch (constraint.type)
        {
        case animator::ik::JointConstraintType::Hinge:
            j["hingeAxis"] = json::array({
                constraint.hingeAxis.x,
                constraint.hingeAxis.y,
                constraint.hingeAxis.z
            });
            break;
        case animator::ik::JointConstraintType::Cone:
            j["coneAngle"] = constraint.coneAngle;
            break;
        case animator::ik::JointConstraintType::BallAndSocket:
            j["swingAngle"] = constraint.swingAngle;
            j["twistMin"] = constraint.twistMin;
            j["twistMax"] = constraint.twistMax;
            break;
        default:
            break;
        }

        return j;
    }

    static animator::ik::JointConstraint deserializeConstraint(const json& j)
    {
        animator::ik::JointConstraint constraint;

        if (j.contains("type") && j["type"].is_string())
            constraint.type = jsonStringToConstraintType(j["type"].get<std::string>());

        if (j.contains("hingeAxis") && j["hingeAxis"].is_array() && j["hingeAxis"].size() >= 3)
        {
            constraint.hingeAxis.x = j["hingeAxis"][0].get<float>();
            constraint.hingeAxis.y = j["hingeAxis"][1].get<float>();
            constraint.hingeAxis.z = j["hingeAxis"][2].get<float>();
        }

        if (auto it = j.find("coneAngle"); it != j.end() && it->is_number())
            constraint.coneAngle = it->get<float>();

        if (auto it = j.find("swingAngle"); it != j.end() && it->is_number())
            constraint.swingAngle = it->get<float>();

        if (auto it = j.find("twistMin"); it != j.end() && it->is_number())
            constraint.twistMin = it->get<float>();

        if (auto it = j.find("twistMax"); it != j.end() && it->is_number())
            constraint.twistMax = it->get<float>();

        return constraint;
    }

    json SceneSerialization::serializeIKTarget(const components::IKTargetComponent& ikTarget)
    {
        json j;
        json chainsJson = json::array();

        for (const auto& chain : ikTarget.chains)
        {
            json chainJ;
            chainJ["chainName"] = chain.chainName;
            chainJ["tipBoneName"] = chain.tipBoneName;
            chainJ["weight"] = chain.weight;
            chainJ["enabled"] = chain.enabled;

            chainJ["chainBoneNames"] = json::array();
            for (const auto& boneName : chain.chainBoneNames)
                chainJ["chainBoneNames"].push_back(boneName);

            if (!chain.constraints.empty())
            {
                json constraintsJson = json::array();
                for (const auto& c : chain.constraints)
                    constraintsJson.push_back(serializeConstraint(c));
                chainJ["constraints"] = constraintsJson;
            }

            chainsJson.push_back(chainJ);
        }

        j["chains"] = chainsJson;
        return j;
    }

    void SceneSerialization::deserializeIKTarget(const json& j, components::IKTargetComponent& ikTarget)
    {
        ikTarget.chains.clear();
        ikTarget.runtimeStates.clear();
        ikTarget.isInitialized = false;

        if (!j.contains("chains") || !j["chains"].is_array())
            return;

        for (const auto& chainJ : j["chains"])
        {
            components::IKChainConfig chain;

            chain.chainName = chainJ.value("chainName", "");
            chain.tipBoneName = chainJ.value("tipBoneName", "");

            if (auto it = chainJ.find("weight"); it != chainJ.end() && it->is_number())
                chain.weight = it->get<float>();

            if (auto it = chainJ.find("enabled"); it != chainJ.end() && it->is_boolean())
                chain.enabled = it->get<bool>();

            if (chainJ.contains("chainBoneNames") && chainJ["chainBoneNames"].is_array())
            {
                for (const auto& boneName : chainJ["chainBoneNames"])
                {
                    if (boneName.is_string())
                        chain.chainBoneNames.push_back(boneName.get<std::string>());
                }
            }

            if (chainJ.contains("constraints") && chainJ["constraints"].is_array())
            {
                for (const auto& cJ : chainJ["constraints"])
                    chain.constraints.push_back(deserializeConstraint(cJ));
            }

            ikTarget.chains.push_back(std::move(chain));
        }
    }
}
