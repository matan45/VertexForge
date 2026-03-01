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
        if (str == "Hinge")          return JointConstraintType::Hinge;
        if (str == "Cone")           return JointConstraintType::Cone;
        if (str == "BallAndSocket")  return JointConstraintType::BallAndSocket;
        return JointConstraintType::None;
    }
}
