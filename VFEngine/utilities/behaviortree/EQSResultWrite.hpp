#pragma once

#include "Blackboard.hpp"
#include "../eqs/EQSTypes.hpp"

#include <cstdint>
#include <string>

// VK-1660: how a finished EQS query reaches the blackboard, shared by the EnvironmentQuery task and the
// EQSRefresh service and free of the engine so it is CPU-testable.
namespace behaviortree
{
    enum class EQSKeyWrite : uint8_t
    {
        Unused,   // no key given
        Written,
        Missing,  // the key is not on the blackboard, so its type is unknown: nothing written
        WrongType // the key holds neither an Entity nor an Int
    };

    // Stores an entity id in an Entity key (EntityHandle) or an Int key (the id, scripts' convention);
    // eqs::kNoEntity clears it (EntityHandle::invalid() / -1).
    EQSKeyWrite writeEntityKey(Blackboard& blackboard, const std::string& key, uint64_t entityId);

    struct EQSResultWrite
    {
        bool positionWritten = false;
        EQSKeyWrite entity = EQSKeyWrite::Unused;
    };

    // Completed with results: the best position goes to resultKey and the best entity to resultEntityKey.
    // Completed without results: resultEntityKey is cleared and resultKey keeps its last value.
    // Pending, Running or Failed: nothing is written.
    EQSResultWrite applyEQSResult(Blackboard& blackboard, const eqs::EQSResult& result,
                                  const std::string& resultKey, const std::string& resultEntityKey);
}
