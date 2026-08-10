#pragma once

#include <volt/plugin/plugin_main.h>

#include <stdexcept>
#include <string>

namespace Volt::Plugin {

class OptionReader {
public:
    OptionReader(const PluginDescriptor& descriptor, const OptsMap& opts)
        : _options(effectiveOptions(descriptor)), _opts(opts) {}

    bool boolean(const std::string& flag) const {
        const std::string& fallback = declared(flag);
        return CLI::getBool(_opts, flag, fallback == "true" || fallback == "1");
    }

    double number(const std::string& flag) const {
        return CLI::getDouble(_opts, flag, parse<double>(declared(flag), [](const std::string& s) {
            return std::stod(s);
        }));
    }

    int integer(const std::string& flag) const {
        return CLI::getInt(_opts, flag, parse<int>(declared(flag), [](const std::string& s) {
            return std::stoi(s);
        }));
    }

    std::string text(const std::string& flag) const {
        return CLI::getString(_opts, flag, declared(flag));
    }

private:
    template<typename T, typename Convert>
    static T parse(const std::string& raw, Convert convert) {
        if (raw.empty()) {
            return T{};
        }
        try {
            return convert(raw);
        } catch (const std::exception&) {
            return T{};
        }
    }

    const std::string& declared(const std::string& flag) const {
        for (const auto& option : _options) {
            if (option.name == flag) {
                return option.defaultVal;
            }
        }
        throw std::logic_error(
            "Option " + flag + " is read but not declared in this plugin's option table."
        );
    }

    std::vector<CliOption> _options;
    const OptsMap& _opts;
};

}
