#pragma once

#include <volt/plugin/plugin_main.h>
#include <volt/plugin/option_binding.h>

#include <utility>
#include <vector>

namespace Volt::Plugin::Detail {

template<typename S>
concept HasCompute3 = requires(S s, const LammpsParser::Frame& f, const std::string& o) {
    { s.compute(f, o, o) } -> std::convertible_to<json>;
};

template<typename S>
json invokeCompute(S& svc, const LammpsParser::Frame& frame, const std::string& outputBase) {
    if constexpr (HasCompute3<S>)
        return svc.compute(frame, outputBase, outputBase);
    else
        return svc.compute(frame, outputBase);
}

}

namespace Volt::Plugin {

// The run of a plugin that sets a service's options from the flags and computes it on the frame. A run without one of
// the required flags fails before the service is set up.
template<typename S>
PluginRunFn serviceRun(const std::vector<OptionBinding<S>>& bindings, std::vector<const char*> required = {}) {
    return [&bindings, required = std::move(required)](const OptsMap& opts, const LammpsParser::Frame& frame,
                                                       const LammpsParser::Frame*, const std::string& outputBase) -> json {
        if (auto error = requireOptions(opts, required)) {
            return *error;
        }
        S service;
        applyAll(service, bindings, opts);
        return Detail::invokeCompute(service, frame, outputBase);
    };
}

}

#define VOLT_SERVICE_PLUGIN(id, desc, ServiceType, bindingsVar) \
    VOLT_PLUGIN_MAIN( \
        (Volt::Plugin::PluginDescriptor{id, desc, Volt::Plugin::optionsMeta(bindingsVar)}), \
        Volt::Plugin::serviceRun<ServiceType>(bindingsVar))
