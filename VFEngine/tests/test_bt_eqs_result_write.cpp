#include <doctest.h>
#include <behaviortree/EQSResultWrite.hpp>
#include <behaviortree/Blackboard.hpp>
#include <eqs/EQSTypes.hpp>
#include <glm/glm.hpp>
#include <string>
#include <variant>

// ============================================================
// VK-1660: a finished EQS query -> the blackboard (behaviortree::applyEQSResult), shared by the
// EnvironmentQuery task and the EQSRefresh service.
// ============================================================

namespace
{
    namespace bt = behaviortree;

    eqs::EQSResult completed(glm::vec3 position, uint64_t entityId)
    {
        eqs::EQSResult result;
        result.status = eqs::EQSQueryStatus::Completed;
        eqs::EQSCandidate best;
        best.position = position;
        best.entityId = entityId;
        best.totalScore = 1.0f;
        result.candidates.push_back(best);
        return result;
    }

    eqs::EQSResult completedEmpty()
    {
        eqs::EQSResult result;
        result.status = eqs::EQSQueryStatus::Completed;
        return result;
    }
}

TEST_SUITE("BTEQSResultWrite")
{
    TEST_CASE("a found entity reaches an Entity key and an Int key, and the position reaches resultKey")
    {
        bt::Blackboard blackboard;
        blackboard.set("enemyPos", glm::vec3(0.0f));
        blackboard.set("enemy", services::EntityHandle::invalid());
        blackboard.set("enemyId", int32_t{-1});

        const auto result = completed(glm::vec3(3.0f, 0.0f, 4.0f), 42);

        auto write = bt::applyEQSResult(blackboard, result, "enemyPos", "enemy");
        CHECK(write.positionWritten);
        CHECK(write.entity == bt::EQSKeyWrite::Written);
        CHECK(blackboard.getVec3("enemyPos") == glm::vec3(3.0f, 0.0f, 4.0f));
        CHECK(blackboard.getEntity("enemy") == services::EntityHandle{42});

        write = bt::applyEQSResult(blackboard, result, "enemyPos", "enemyId");
        CHECK(write.entity == bt::EQSKeyWrite::Written);
        CHECK(blackboard.getInt("enemyId") == 42);
    }

    TEST_CASE("a point result (no entity) clears the entity key")
    {
        bt::Blackboard blackboard;
        blackboard.set("enemy", services::EntityHandle{7});
        blackboard.set("enemyId", int32_t{7});

        const auto result = completed(glm::vec3(1.0f), eqs::kNoEntity);
        CHECK(bt::applyEQSResult(blackboard, result, "", "enemy").entity == bt::EQSKeyWrite::Written);
        CHECK_FALSE(blackboard.getEntity("enemy").isValid());
        CHECK(bt::applyEQSResult(blackboard, result, "", "enemyId").entity == bt::EQSKeyWrite::Written);
        CHECK(blackboard.getInt("enemyId") == -1);
    }

    TEST_CASE("no result clears the entity key and keeps the last position")
    {
        bt::Blackboard blackboard;
        blackboard.set("enemyPos", glm::vec3(5.0f, 0.0f, 5.0f));
        blackboard.set("enemyId", int32_t{12});

        const auto write = bt::applyEQSResult(blackboard, completedEmpty(), "enemyPos", "enemyId");
        CHECK_FALSE(write.positionWritten);
        CHECK(write.entity == bt::EQSKeyWrite::Written);
        CHECK(blackboard.getVec3("enemyPos") == glm::vec3(5.0f, 0.0f, 5.0f));
        CHECK(blackboard.getInt("enemyId") == -1);
    }

    TEST_CASE("an unfinished or failed query writes nothing")
    {
        bt::Blackboard blackboard;
        blackboard.set("enemyPos", glm::vec3(2.0f));
        blackboard.set("enemyId", int32_t{9});

        for (auto status : {eqs::EQSQueryStatus::Pending, eqs::EQSQueryStatus::Running, eqs::EQSQueryStatus::Failed})
        {
            auto result = completed(glm::vec3(8.0f), 1);
            result.status = status;
            const auto write = bt::applyEQSResult(blackboard, result, "enemyPos", "enemyId");
            CHECK_FALSE(write.positionWritten);
            CHECK(write.entity == bt::EQSKeyWrite::Unused);
        }
        CHECK(blackboard.getVec3("enemyPos") == glm::vec3(2.0f));
        CHECK(blackboard.getInt("enemyId") == 9);
    }

    TEST_CASE("an unused, missing or mistyped entity key is reported and left alone")
    {
        bt::Blackboard blackboard;
        blackboard.set("distance", 3.5f);
        const auto result = completed(glm::vec3(0.0f), 4);

        CHECK(bt::writeEntityKey(blackboard, "", 4) == bt::EQSKeyWrite::Unused);
        CHECK(bt::writeEntityKey(blackboard, "nowhere", 4) == bt::EQSKeyWrite::Missing);
        CHECK_FALSE(blackboard.has("nowhere"));
        CHECK(bt::applyEQSResult(blackboard, result, "", "distance").entity == bt::EQSKeyWrite::WrongType);
        CHECK(blackboard.getFloat("distance") == doctest::Approx(3.5f));
    }
}
