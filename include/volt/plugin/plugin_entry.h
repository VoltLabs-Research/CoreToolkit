#pragma once

#include <volt/plugin/plugin_main.h>
#include <volt/plugin/option_binding.h>

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

} // namespace Volt::Plugin::Detail

#define VOLT_SERVICE_PLUGIN(id, desc, ServiceType, bindingsVar) \
    VOLT_PLUGIN_MAIN( \
        (Volt::Plugin::PluginDescriptor{id, desc, Volt::Plugin::optionsMeta(bindingsVar)}), \
        [](const Volt::Plugin::OptsMap& opts, const Volt::LammpsParser::Frame& frame, \
           const Volt::LammpsParser::Frame*, const std::string& outputBase) -> Volt::Plugin::json { \
        ServiceType svc; \
        Volt::Plugin::applyAll(svc, bindingsVar, opts); \
        return Volt::Plugin::Detail::invokeCompute(svc, frame, outputBase); \
    })
