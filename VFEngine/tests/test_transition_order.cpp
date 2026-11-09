#include <doctest.h>

// VK-1656: AnimatorGraph::getTransitionsFromState orders a state's outgoing transitions (its own
// plus the Any State ones) by priority, lower first, and keeps the listed order between equal
// priorities. AnimatorStateMachine::evaluateTransitions fires the first one whose conditions
// hold, so among equally ranked transitions the one listed first wins, as authored.

#include <animator/AnimatorTypes.hpp>

#include <cstdint>
#include <vector>

namespace
{
    constexpr uint32_t anyState = 0;

    animator::AnimatorTransition transition(uint32_t id, uint32_t source, uint32_t target, int32_t priority)
    {
        animator::AnimatorTransition t;
        t.id = id;
        t.sourceStateId = source;
        t.targetStateId = target;
        t.priority = priority;
        return t;
    }

    std::vector<uint32_t> orderedIds(const animator::AnimatorGraph& graph, uint32_t stateId)
    {
        std::vector<uint32_t> ids;
        for (const animator::AnimatorTransition* t : graph.getTransitionsFromState(stateId))
            ids.push_back(t->id);
        return ids;
    }
}

TEST_SUITE("AnimatorTransitionOrder")
{
    TEST_CASE("equal priorities keep their listed order")
    {
        animator::AnimatorGraph graph;
        graph.transitions = {
            transition(1, 1, 2, 0),
            transition(2, 1, 3, 0),
            transition(3, 1, 4, 0),
        };

        CHECK(orderedIds(graph, 1) == std::vector<uint32_t>{1, 2, 3});
    }

    TEST_CASE("Any State transitions keep their listed place among equal priorities")
    {
        animator::AnimatorGraph graph;
        graph.transitions = {
            transition(1, 1, 2, 0),
            transition(2, anyState, 3, 0),
            transition(3, 1, 4, 0),
            transition(4, anyState, 5, 0),
        };

        CHECK(orderedIds(graph, 1) == std::vector<uint32_t>{1, 2, 3, 4});
        // Any other state sees only the Any State transitions, still in listed order.
        CHECK(orderedIds(graph, 7) == std::vector<uint32_t>{2, 4});
    }

    TEST_CASE("lower priority comes first, ties in listed order")
    {
        animator::AnimatorGraph graph;
        graph.transitions = {
            transition(1, 1, 2, 2),
            transition(2, anyState, 3, -1),
            transition(3, 1, 4, 0),
            transition(4, 1, 5, 2),
            transition(5, anyState, 6, 0),
            transition(6, 1, 7, -1),
        };

        CHECK(orderedIds(graph, 1) == std::vector<uint32_t>{2, 6, 3, 5, 1, 4});
    }

    TEST_CASE("transitions leaving other states are not included")
    {
        animator::AnimatorGraph graph;
        graph.transitions = {
            transition(1, 2, 3, 0),
            transition(2, 1, 3, 0),
            transition(3, 3, 1, -5),
            transition(4, anyState, 2, 1),
        };

        CHECK(orderedIds(graph, 1) == std::vector<uint32_t>{2, 4});
    }

    TEST_CASE("order stays stable across many interleaved priorities")
    {
        // Large enough that an unstable sort leaves its small-range insertion sort (stable by
        // accident) and partitions, which reorders equal elements.
        animator::AnimatorGraph graph;
        constexpr uint32_t count = 256;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t source = (i % 5 == 0) ? anyState : 1;
            const int32_t priority = static_cast<int32_t>((i * 7) % 4) - 1; // -1..2, interleaved
            graph.transitions.push_back(transition(i + 1, source, 2, priority));
        }

        std::vector<uint32_t> expected;
        for (int32_t priority = -1; priority <= 2; ++priority)
        {
            for (const auto& t : graph.transitions)
            {
                if (t.priority == priority)
                    expected.push_back(t.id);
            }
        }

        REQUIRE(expected.size() == count);
        CHECK(orderedIds(graph, 1) == expected);
    }
}
