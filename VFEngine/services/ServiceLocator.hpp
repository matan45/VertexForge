#pragma once
#include <memory>
#include <shared_mutex>
#include <typeindex>
#include <unordered_map>
#include <stdexcept>
#include <string>

namespace services {

    // Service Locator - provides dependency injection for services
    // Usage:
    //   ServiceLocator::instance().registerService<ISceneService>(sceneServiceImpl);
    //   auto& sceneService = ServiceLocator::instance().get<ISceneService>();
    class ServiceLocator {
    public:
        static ServiceLocator& instance();

        // Prevent copying
        ServiceLocator(const ServiceLocator&) = delete;
        ServiceLocator& operator=(const ServiceLocator&) = delete;

        // Register a service implementation
        template<typename TInterface>
        void registerService(std::shared_ptr<TInterface> service);

        // Check if a service is registered
        template<typename TInterface>
        bool hasService() const;

        // Get a service (throws if not found)
        template<typename TInterface>
        TInterface& get();

        // Get a service (returns nullptr if not found)
        template<typename TInterface>
        std::shared_ptr<TInterface> tryGet();

        // Unregister a specific service
        template<typename TInterface>
        void unregisterService();

        // Clear all registered services (call during shutdown)
        void clear();

    private:
        ServiceLocator() = default;
        ~ServiceLocator() = default;

        std::unordered_map<std::type_index, std::shared_ptr<void>> services;
        mutable std::shared_mutex mutex;
    };

    // Template implementations

    template<typename TInterface>
    void ServiceLocator::registerService(std::shared_ptr<TInterface> service) {
        std::unique_lock lock(mutex);
        services[std::type_index(typeid(TInterface))] = std::move(service);
    }

    template<typename TInterface>
    bool ServiceLocator::hasService() const {
        std::shared_lock lock(mutex);
        return services.find(std::type_index(typeid(TInterface))) != services.end();
    }

    template<typename TInterface>
    TInterface& ServiceLocator::get() {
        std::shared_lock lock(mutex);

        auto it = services.find(std::type_index(typeid(TInterface)));
        if (it == services.end()) {
            throw std::runtime_error(
                std::string("Service not registered: ") + typeid(TInterface).name());
        }

        auto ptr = std::static_pointer_cast<TInterface>(it->second);
        return *ptr;
    }

    template<typename TInterface>
    std::shared_ptr<TInterface> ServiceLocator::tryGet() {
        std::shared_lock lock(mutex);

        auto it = services.find(std::type_index(typeid(TInterface)));
        if (it == services.end()) {
            return nullptr;
        }

        return std::static_pointer_cast<TInterface>(it->second);
    }

    template<typename TInterface>
    void ServiceLocator::unregisterService() {
        std::unique_lock lock(mutex);
        services.erase(std::type_index(typeid(TInterface)));
    }

    // Convenience macro for resolving services
    #define RESOLVE_SERVICE(Type) services::ServiceLocator::instance().get<Type>()
    #define TRY_RESOLVE_SERVICE(Type) services::ServiceLocator::instance().tryGet<Type>()

}
