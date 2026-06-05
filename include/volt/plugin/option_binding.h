#pragma once

#include <volt/plugin/plugin_main.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Volt::Plugin {

template<typename Service>
struct OptionBinding {
    CliOption meta;
    std::function<void(Service&, const OptsMap&)> apply;
};

template<typename T, typename S>
OptionBinding<S> opt(const char* name, const char* help, T def, void(S::*setter)(T)) {
    if constexpr (std::is_same_v<T, bool>) {
        return {
            {name, "bool", help, def ? "true" : "false"},
            [setter, name, def](S& s, const OptsMap& opts) {
                (s.*setter)(CLI::getBool(opts, name, def));
            }
        };
    } else if constexpr (std::is_same_v<T, float>) {
        return {
            {name, "float", help, std::to_string(def)},
            [setter, name, def](S& s, const OptsMap& opts) {
                (s.*setter)(static_cast<float>(CLI::getDouble(opts, name, static_cast<double>(def))));
            }
        };
    } else if constexpr (std::is_same_v<T, double>) {
        return {
            {name, "float", help, std::to_string(def)},
            [setter, name, def](S& s, const OptsMap& opts) {
                (s.*setter)(CLI::getDouble(opts, name, def));
            }
        };
    } else if constexpr (std::is_same_v<T, int>) {
        return {
            {name, "int", help, std::to_string(def)},
            [setter, name, def](S& s, const OptsMap& opts) {
                (s.*setter)(CLI::getInt(opts, name, def));
            }
        };
    }
}

template<typename S>
OptionBinding<S> opt(const char* name, const char* help, const char* def, void(S::*setter)(std::string)) {
    return {
        {name, "string", help, def},
        [setter, name, dv = std::string(def)](S& s, const OptsMap& opts) {
            (s.*setter)(CLI::getString(opts, name, dv));
        }
    };
}

template<typename S>
OptionBinding<S> opt(CliOption meta, std::function<void(S&, const OptsMap&)> fn) {
    return {std::move(meta), std::move(fn)};
}

inline std::optional<json> requireOptions(const OptsMap& opts, std::initializer_list<const char*> required) {
    for (const char* key : required) {
        if (!CLI::hasOption(opts, key))
            return AnalysisResult::failure(std::string(key) + " is required");
    }
    return std::nullopt;
}

template<typename S>
void applyAll(S& service, const std::vector<OptionBinding<S>>& bindings, const OptsMap& opts) {
    for (const auto& b : bindings)
        b.apply(service, opts);
}

template<typename S>
std::vector<CliOption> optionsMeta(const std::vector<OptionBinding<S>>& bindings) {
    std::vector<CliOption> result;
    result.reserve(bindings.size());
    for (const auto& b : bindings)
        result.push_back(b.meta);
    return result;
}

} // namespace Volt::Plugin
