#pragma once

#include <volt/core/volt.h>
#include <volt/core/lammps_parser.h>
#include <volt/core/simulation_cell.h>
#include <volt/core/particle_property.h>
#include <memory>
#include <string>

namespace Volt{

/**
 * Centralizes the conversion from LammpsParser::Frame data
 * into ParticleProperty objects required by analysis engines.
 * Replaces the duplicated createPositionProperty / createIdentifierProperty /
 * validateSimulationCell helpers that were copy-pasted across every service.
 */
class FrameAdapter{
public:
	struct PreparedAnalysisInput{
		std::shared_ptr<Particles::ParticleProperty> positions;
	};

	/**
	 * Builds a PositionProperty from the positions vector in a parsed frame.
	 * Returns nullptr on allocation failure.
	 */
	static std::shared_ptr<Particles::ParticleProperty> createPositionProperty(const LammpsParser::Frame& frame);

	/**
	 * Runs the common frame preflight checks shared by analysis services
	 * and prepares a zero-copy position property on success.
	 */
	static bool prepareAnalysisInput(
		const LammpsParser::Frame& frame,
		PreparedAnalysisInput& prepared,
		std::string* errorMessage = nullptr
	);

	/**
	 * Zero-copy position property bound to frame.positions memory.
	 * The caller must keep the frame alive while the property is used.
	 */
	static std::shared_ptr<Particles::ParticleProperty> createPositionPropertyShared(const LammpsParser::Frame& frame);

	/**
	 * Builds an IdentifierProperty from the ids vector in a parsed frame.
	 * Returns nullptr on allocation failure.
	 */
	static std::shared_ptr<Particles::ParticleProperty> createIdentifierProperty(const LammpsParser::Frame& frame);

	/**
	 * Zero-copy scalar integer property bound to a parsed extra dump column.
	 * Returns nullptr if the column does not exist or has the wrong type.
	 */
	static std::shared_ptr<Particles::ParticleProperty> createIntPropertyShared(
		const LammpsParser::Frame& frame,
		const std::string& columnName
	);

	/**
	 * Zero-copy scalar int64 property bound to a parsed extra dump column.
	 * Returns nullptr if the column does not exist or has the wrong type.
	 */
	static std::shared_ptr<Particles::ParticleProperty> createInt64PropertyShared(
		const LammpsParser::Frame& frame,
		const std::string& columnName
	);

	/**
	 * Builds a quaternion property by stitching together four scalar dump columns.
	 * The resulting property owns its own storage.
	 */
	static std::shared_ptr<Particles::ParticleProperty> createQuaternionPropertyShared(
		const LammpsParser::Frame& frame,
		const std::string& xColumn,
		const std::string& yColumn,
		const std::string& zColumn,
		const std::string& wColumn
	);

	/**
	 * Validates that the simulation cell matrix has no NaN/Inf components
	 * and that the cell volume is positive and finite.
	 */
	static bool validateSimulationCell(const Particles::SimulationCell& cell);
};

}
