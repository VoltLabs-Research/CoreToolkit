#pragma once

#include <volt/core/volt.h>
#include <volt/core/lammps_parser.h>
#include <volt/core/simulation_cell.h>
#include <volt/core/particle_property.h>
#include <memory>

namespace Volt{

/**
 * Centralizes the conversion from LammpsParser::Frame data
 * into ParticleProperty objects required by analysis engines.
 * Replaces the duplicated createPositionProperty / createIdentifierProperty /
 * validateSimulationCell helpers that were copy-pasted across every service.
 */
class FrameAdapter{
public:
	/**
	 * Builds a PositionProperty from the positions vector in a parsed frame.
	 * Returns nullptr on allocation failure.
	 */
	static std::shared_ptr<Particles::ParticleProperty> createPositionProperty(const LammpsParser::Frame& frame);

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
	 * Validates that the simulation cell matrix has no NaN/Inf components
	 * and that the cell volume is positive and finite.
	 */
	static bool validateSimulationCell(const Particles::SimulationCell& cell);
};

}
