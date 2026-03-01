#include "IKTypes.hpp"

namespace animator::ik
{
    const char* constraintTypeToString(JointConstraintType type)
    {
        switch (type)
        {
        case JointConstraintType::None:           return "None";
        case JointConstraintType::Hinge:          return "Hinge";
        case JointConstraintType::Cone:           return "Cone";
        case JointConstraintType::BallAndSocket:  return "BallAndSocket";
        default:                                  return "None";
        }
    }

    JointConstraintType stringToConstraintType(const std::string& str)
    {
        if (str == "Hinge"          || str == "hinge")          return JointConstraintType::Hinge;
        if (str == "Cone"           || str == "cone")           return JointConstraintType::Cone;
        if (str == "BallAndSocket"  || str == "ballAndSocket")  return JointConstraintType::BallAndSocket;
        if (str == "None"           || str == "none")           return JointConstraintType::None;
        return JointConstraintType::None;
    }
}
