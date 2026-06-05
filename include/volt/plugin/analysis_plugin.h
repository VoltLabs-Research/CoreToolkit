#pragma once

#include <volt/core/frame_adapter.h>
#include <volt/core/analysis_result.h>
#include <volt/core/lammps_parser.h>
#include <volt/core/particle_property.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <memory>

namespace Volt::Plugin {

using json = nlohmann::json;

template<typename Derived>
class AnalysisPlugin {
public:
    json compute(const LammpsParser::Frame& frame, const std::string& outputBase) {
        if (frame.natoms <= 0)
            return AnalysisResult::failure("Invalid number of atoms");

        auto validationError = derived().validate(frame);
        if (!validationError.empty())
            return AnalysisResult::failure(validationError);

        auto positions = FrameAdapter::createPositionPropertyShared(frame);
        if (!positions)
            return AnalysisResult::failure("Failed to create position property");

        json result = derived().run(frame, positions, outputBase);

        if (!result.value("is_failed", false) && !outputBase.empty()) {
            derived().serialize(frame, positions, result, outputBase);
        }

        return result;
    }

    std::string validate(const LammpsParser::Frame&) { return ""; }

    void serialize(const LammpsParser::Frame&,
                   const std::shared_ptr<Volt::Particles::ParticleProperty>&,
                   const json&,
                   const std::string&) {}

private:
    Derived& derived() { return static_cast<Derived&>(*this); }
    const Derived& derived() const { return static_cast<const Derived&>(*this); }
};

} // namespace Volt::Plugin
