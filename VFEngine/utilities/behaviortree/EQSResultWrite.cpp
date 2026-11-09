#include "EQSResultWrite.hpp"

#include <variant>

namespace behaviortree
{
    EQSKeyWrite writeEntityKey(Blackboard& blackboard, const std::string& key, uint64_t entityId)
    {
        if (key.empty())
            return EQSKeyWrite::Unused;
        if (!blackboard.has(key))
            return EQSKeyWrite::Missing;

        const BlackboardValue current = blackboard.get(key);
        if (std::holds_alternative<services::EntityHandle>(current))
        {
            blackboard.set(key, entityId == eqs::kNoEntity ? services::EntityHandle::invalid()
                                                           : services::EntityHandle{entityId});
            return EQSKeyWrite::Written;
        }
        if (std::holds_alternative<int32_t>(current))
        {
            // An entity id is the entt entity's 32 bits; -1 (entt::null) is "none".
            const int32_t id = entityId == eqs::kNoEntity ? -1
                                                          : static_cast<int32_t>(static_cast<uint32_t>(entityId));
            blackboard.set(key, id);
            return EQSKeyWrite::Written;
        }
        return EQSKeyWrite::WrongType;
    }

    EQSResultWrite applyEQSResult(Blackboard& blackboard, const eqs::EQSResult& result,
                                  const std::string& resultKey, const std::string& resultEntityKey)
    {
        EQSResultWrite write;
        if (result.status != eqs::EQSQueryStatus::Completed)
            return write;

        if (result.hasResults())
        {
            if (!resultKey.empty())
            {
                blackboard.set(resultKey, result.getBestPosition());
                write.positionWritten = true;
            }
            write.entity = writeEntityKey(blackboard, resultEntityKey, result.getBestEntity());
        }
        else
        {
            write.entity = writeEntityKey(blackboard, resultEntityKey, eqs::kNoEntity);
        }
        return write;
    }
}
