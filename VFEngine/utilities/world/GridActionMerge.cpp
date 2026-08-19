#include "GridActionMerge.hpp"
#include <algorithm>

namespace world
{
    void mergeGridStreamingActions(
        const std::vector<std::vector<SectorStreamingAction>>& perGridActions,
        std::vector<GridStreamingAction>& outMerged)
    {
        outMerged.clear();

        size_t total = 0;
        size_t longest = 0;
        for (const auto& actions : perGridActions)
        {
            total += actions.size();
            longest = std::max(longest, actions.size());
        }

        if (total == 0)
            return;

        outMerged.reserve(total);

        // One pass per round rather than per grid: at round r we take element r from every grid
        // that still has one. Grids that ran out are simply skipped, so a short list never holds
        // up the rest and a single non-empty grid degenerates to a straight copy.
        for (size_t round = 0; round < longest; ++round)
        {
            for (size_t gridIndex = 0; gridIndex < perGridActions.size(); ++gridIndex)
            {
                const auto& actions = perGridActions[gridIndex];
                if (round >= actions.size())
                    continue;

                outMerged.push_back(GridStreamingAction{
                    static_cast<uint8_t>(gridIndex), actions[round]});
            }
        }
    }

} // namespace world
