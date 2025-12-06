#include "ServiceLocator.hpp"

namespace services {

    ServiceLocator& ServiceLocator::instance() {
        static ServiceLocator instance;
        return instance;
    }

    void ServiceLocator::clear() {
        std::unique_lock lock(mutex);
        services.clear();
    }

}
