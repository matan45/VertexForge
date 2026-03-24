#pragma once
#include "../EventTypes.hpp"
#include <string>

namespace events::save {

    // ============================================
    // Config Set Commands
    // ============================================

    struct SetConfigIntCommand : ICommand<bool> {
        std::string key;
        int64_t value;

        std::string_view getName() const override { return "SetConfigInt"; }
    };

    struct SetConfigFloatCommand : ICommand<bool> {
        std::string key;
        double value;

        std::string_view getName() const override { return "SetConfigFloat"; }
    };

    struct SetConfigStringCommand : ICommand<bool> {
        std::string key;
        std::string value;

        std::string_view getName() const override { return "SetConfigString"; }
    };

    struct SetConfigBoolCommand : ICommand<bool> {
        std::string key;
        bool value;

        std::string_view getName() const override { return "SetConfigBool"; }
    };

    // ============================================
    // Config Get Queries
    // ============================================

    struct GetConfigIntQuery : IQuery<int64_t> {
        std::string key;
        int64_t defaultValue = 0;

        std::string_view getName() const override { return "GetConfigInt"; }
    };

    struct GetConfigFloatQuery : IQuery<double> {
        std::string key;
        double defaultValue = 0.0;

        std::string_view getName() const override { return "GetConfigFloat"; }
    };

    struct GetConfigStringQuery : IQuery<std::string> {
        std::string key;
        std::string defaultValue;

        std::string_view getName() const override { return "GetConfigString"; }
    };

    struct GetConfigBoolQuery : IQuery<bool> {
        std::string key;
        bool defaultValue = false;

        std::string_view getName() const override { return "GetConfigBool"; }
    };

}
