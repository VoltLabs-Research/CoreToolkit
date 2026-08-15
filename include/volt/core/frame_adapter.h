#pragma once

#include <volt/core/volt.h>
#include <volt/core/lammps_parser.h>
#include <volt/core/simulation_cell.h>
#include <volt/core/particle_property.h>
#include <memory>
#include <string>

namespace Volt{

class FrameAdapter{
public:
	struct PreparedAnalysisInput{
		std::shared_ptr<Particles::ParticleProperty> positions;
	};

	static std::shared_ptr<Particles::ParticleProperty> createPositionProperty(const LammpsParser::Frame& frame);

	static bool prepareAnalysisInput(
		const LammpsParser::Frame& frame,
		PreparedAnalysisInput& prepared,
		std::string* errorMessage = nullptr
	);

	static std::shared_ptr<Particles::ParticleProperty> createPositionPropertyShared(const LammpsParser::Frame& frame);

	static std::shared_ptr<Particles::ParticleProperty> createIdentifierProperty(const LammpsParser::Frame& frame);

	static std::shared_ptr<Particles::ParticleProperty> createIntPropertyShared(
		const LammpsParser::Frame& frame,
		const std::string& columnName
	);

	static std::shared_ptr<Particles::ParticleProperty> createInt64PropertyShared(
		const LammpsParser::Frame& frame,
		const std::string& columnName
	);

	static std::shared_ptr<Particles::ParticleProperty> createQuaternionPropertyShared(
		const LammpsParser::Frame& frame,
		const std::string& xColumn,
		const std::string& yColumn,
		const std::string& zColumn,
		const std::string& wColumn
	);

	static bool validateSimulationCell(const Particles::SimulationCell& cell);
};

}
