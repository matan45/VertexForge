#pragma once
#include "../../interfaces/ai/IBehaviorTreeService.hpp"
#include "../../providers/ai/IBehaviorTreeProvider.hpp"

namespace services
{
    class BehaviorTreeServiceImpl : public IBehaviorTreeService
    {
    private:
        IBehaviorTreeProvider* provider;

    public:
        explicit BehaviorTreeServiceImpl(IBehaviorTreeProvider* provider);
        ~BehaviorTreeServiceImpl() override;

        void registerEventHandlers() override;

        // === Tree Management ===
        bool attachTree(EntityHandle entity, const std::string& treePath) override;
        void detachTree(EntityHandle entity) override;
        void setEnabled(EntityHandle entity, bool enabled) override;

        // === Runtime ===
        void updateAll(float deltaTime) override;
        void stopAll() override;
    };
}
