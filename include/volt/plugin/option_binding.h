#pragma once

#include <volt/plugin/plugin_main.h>
#include <volt/structures/crystal_structure_types.h>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace Volt::Plugin {

namespace Detail {
inline std::string formatDefault(double v) {
    std::string s = std::to_string(v);
    auto dot = s.find('.');
    if (dot != std::string::npos) {
        auto last = s.find_last_not_of('0');
        if (last == dot) ++last;
        s.erase(last + 1);
    }
    return s;
}
}

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
            {name, "float", help, Detail::formatDefault(static_cast<double>(def))},
            [setter, name, def](S& s, const OptsMap& opts) {
                (s.*setter)(static_cast<float>(CLI::getDouble(opts, name, static_cast<double>(def))));
            }
        };
    } else if constexpr (std::is_same_v<T, double>) {
        return {
            {name, "float", help, Detail::formatDefault(def)},
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

// An option that sets a field of a struct of options; the field's own value is its default.
template<typename T, typename S>
    requires std::is_arithmetic_v<T>
OptionBinding<S> opt(const char* name, const char* help, T S::* field) {
    const T def = S{}.*field;
    if constexpr (std::is_same_v<T, bool>) {
        return {
            {name, "bool", help, def ? "true" : "false"},
            [field, name](S& s, const OptsMap& opts) {
                s.*field = CLI::getBool(opts, name, s.*field);
            }
        };
    } else if constexpr (std::is_same_v<T, float>) {
        return {
            {name, "float", help, Detail::formatDefault(static_cast<double>(def))},
            [field, name](S& s, const OptsMap& opts) {
                s.*field = static_cast<float>(CLI::getDouble(opts, name, static_cast<double>(s.*field)));
            }
        };
    } else if constexpr (std::is_same_v<T, double>) {
        return {
            {name, "float", help, Detail::formatDefault(def)},
            [field, name](S& s, const OptsMap& opts) {
                s.*field = CLI::getDouble(opts, name, s.*field);
            }
        };
    } else if constexpr (std::is_same_v<T, int>) {
        return {
            {name, "int", help, std::to_string(def)},
            [field, name](S& s, const OptsMap& opts) {
                s.*field = CLI::getInt(opts, name, s.*field);
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

template<typename S>
OptionBinding<S> optLattice(const char* name, const char* allowed, const char* def,
                            void(S::*setter)(LatticeStructureType)) {
    return {
        {name, "enum", "Crystal structure to match against.", def, CLI::splitList(allowed, '|')},
        [setter, name, dv = std::string(def)](S& s, const OptsMap& opts) {
            LatticeStructureType t{};
            parseLatticeStructureType(dv, t);
            parseLatticeStructureType(CLI::getString(opts, name, dv), t);
            (s.*setter)(t);
        }
    };
}

inline std::optional<json> requireOptions(const OptsMap& opts, const std::vector<const char*>& required) {
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

template<typename Options>
using OptionsRunFn = std::function<json(
    const Options& options,
    const LammpsParser::Frame& frame,
    const LammpsParser::Frame* refFrame,
    const std::string& outputBase
)>;

// pluginMain for a plugin whose flags fill a struct of options. They are read once, where the run's parameters are
// logged, and logResolved logs the values they resolved to instead of the generic "Parameters:" line, which shows the
// flags as parsed: a negative number such as "--typeA -1" reaches parseArgs as a bare flag, "true".
template<typename Options>
int pluginMain(
    int argc,
    char* argv[],
    const PluginDescriptor& desc,
    const std::vector<OptionBinding<Options>>& bindings,
    std::type_identity_t<std::function<void(const Options&)>> logResolved,
    std::type_identity_t<OptionsRunFn<Options>> run
) {
    Options options;
    return Detail::runPlugin(argc, argv, desc,
        [&](const OptsMap& opts) {
            applyAll(options, bindings, opts);
            logResolved(options);
        },
        [&](const OptsMap&, const LammpsParser::Frame& frame, const LammpsParser::Frame* refFrame,
            const std::string& outputBase) {
            return run(options, frame, refFrame, outputBase);
        });
}

}
