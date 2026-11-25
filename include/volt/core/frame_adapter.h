#pragma once

#include <volt/core/volt.h>
#include <volt/core/lammps_parser.h>
#include <volt/core/simulation_cell.h>
#include <volt/core/particle_property.h>
#include <initializer_list>
#include <memory>
#include <string>

namespace Volt{

// Each function that can fail returns null or false and, given errorMessage, says there why it failed.
class FrameAdapter{
public:
	struct PreparedAnalysisInput{
		std::shared_ptr<Particles::ParticleProperty> positions;
	};

	static std::shared_ptr<Particles::ParticleProperty> createPositionProperty(
		const LammpsParser::Frame& frame,
		std::string* errorMessage = nullptr
	);

	static bool prepareAnalysisInput(
		const LammpsParser::Frame& frame,
		PreparedAnalysisInput& prepared,
		std::string* errorMessage = nullptr
	);

	static std::shared_ptr<Particles::ParticleProperty> createPositionPropertyShared(
		const LammpsParser::Frame& frame,
		std::string* errorMessage = nullptr
	);

	static std::shared_ptr<Particles::ParticleProperty> createIdentifierProperty(
		const LammpsParser::Frame& frame,
		std::string* errorMessage = nullptr
	);

	static std::shared_ptr<Particles::ParticleProperty> createIntPropertyShared(
		const LammpsParser::Frame& frame,
		const std::string& columnName,
		std::string* errorMessage = nullptr
	);

	static std::shared_ptr<Particles::ParticleProperty> createInt64PropertyShared(
		const LammpsParser::Frame& frame,
		const std::string& columnName,
		std::string* errorMessage = nullptr
	);

	static std::shared_ptr<Particles::ParticleProperty> createQuaternionPropertyShared(
		const LammpsParser::Frame& frame,
		const std::string& xColumn,
		const std::string& yColumn,
		const std::string& zColumn,
		const std::string& wColumn,
		std::string* errorMessage = nullptr
	);

	// The first of these optional columns the frame has with this data type and one value per atom, bound in place;
	// null when it has none of them, which is no error.
	static std::shared_ptr<Particles::ParticleProperty> findColumnShared(
		const LammpsParser::Frame& frame,
		std::initializer_list<const char*> columnNames,
		DataType dataType
	);

	static bool validateSimulationCell(const Particles::SimulationCell& cell, std::string* errorMessage = nullptr);
};

namespace FrameAdapterDetail{

void setError(std::string* errorMessage, std::string error);
std::shared_ptr<void> frameOwner(const LammpsParser::Frame& frame);
const LammpsParser::AtomColumn* requireColumn(
	const LammpsParser::Frame& frame,
	const std::string& columnName,
	DataType expectedType,
	std::string* errorMessage
);
std::shared_ptr<ParticleProperty> bindColumnShared(const LammpsParser::Frame& frame, const LammpsParser::AtomColumn& column);

}

}
