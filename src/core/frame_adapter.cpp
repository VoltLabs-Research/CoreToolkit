#include <volt/core/frame_adapter.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <utility>

namespace Volt{

using namespace Volt::Particles;

namespace FrameAdapterDetail {

void setError(std::string* errorMessage, std::string error){
	if(errorMessage){
		*errorMessage = std::move(error);
	}
}

std::shared_ptr<void> frameOwner(const LammpsParser::Frame& frame){
	return std::shared_ptr<void>(const_cast<LammpsParser::Frame*>(&frame), [](void*){});
}

const LammpsParser::AtomColumn* requireColumn(
	const LammpsParser::Frame& frame,
	const std::string& columnName,
	DataType expectedType
){
	const auto* column = frame.findAtomProperty(columnName);
	if(!column){
		spdlog::error("Frame is missing required atom column '{}'", columnName);
		return nullptr;
	}
	if(column->dataType != expectedType){
		spdlog::error(
			"Atom column '{}' has unexpected data type {} (expected {})",
			columnName,
			static_cast<int>(column->dataType),
			static_cast<int>(expectedType)
		);
		return nullptr;
	}
	if(column->size() != static_cast<std::size_t>(frame.natoms)){
		spdlog::error(
			"Atom column '{}' has {} rows but frame reports {} atoms",
			columnName,
			column->size(),
			frame.natoms
		);
		return nullptr;
	}
	return column;
}

}

using namespace FrameAdapterDetail;

std::shared_ptr<ParticleProperty> FrameAdapter::createPositionProperty(const LammpsParser::Frame& frame){
	std::shared_ptr<ParticleProperty> property(new ParticleProperty(frame.natoms, ParticleProperty::PositionProperty, 0, true));

	if(!property || property->size() != static_cast<size_t>(frame.natoms)){
		spdlog::error("Failed to allocate ParticleProperty for positions with correct size");
		return nullptr;
	}

	Point3* data = property->dataPoint3();
	if(!data){
		spdlog::error("Failed to get position data pointer from ParticleProperty");
		return nullptr;
	}

	for(size_t i = 0; i < frame.positions.size() && i < static_cast<size_t>(frame.natoms); i++){
		data[i] = frame.positions[i];
	}

	return property;
}

std::shared_ptr<ParticleProperty> FrameAdapter::createPositionPropertyShared(const LammpsParser::Frame& frame){
	auto property = std::make_shared<ParticleProperty>();
	if(frame.positions.empty()){
		spdlog::error("Frame positions buffer is empty");
		return nullptr;
	}
	if(frame.positions.size() < static_cast<std::size_t>(frame.natoms)){
		spdlog::error(
			"Frame positions buffer has {} entries but frame reports {} atoms",
			frame.positions.size(),
			frame.natoms
		);
		return nullptr;
	}

	std::shared_ptr<void> owner(const_cast<std::vector<Point3>*>(&frame.positions), [](void*){});
	property->bindExternalData(
		const_cast<Point3*>(frame.positions.data()),
		static_cast<size_t>(frame.natoms),
		DataType::Double,
		3,
		sizeof(Point3),
		std::move(owner)
	);
	property->setType(ParticleProperty::PositionProperty);
	return property;
}

bool FrameAdapter::prepareAnalysisInput(
	const LammpsParser::Frame& frame,
	PreparedAnalysisInput& prepared,
	std::string* errorMessage
){
	prepared = {};

	if(frame.natoms <= 0){
		setError(errorMessage, "Invalid number of atoms: " + std::to_string(frame.natoms));
		return false;
	}

	if(frame.positions.empty()){
		setError(errorMessage, "No position data available");
		return false;
	}

	if(!validateSimulationCell(frame.simulationCell)){
		setError(errorMessage, "Invalid simulation cell");
		return false;
	}

	prepared.positions = createPositionPropertyShared(frame);
	if(!prepared.positions){
		setError(errorMessage, "Failed to create position property");
		return false;
	}

	return true;
}

std::shared_ptr<ParticleProperty> FrameAdapter::createIdentifierProperty(const LammpsParser::Frame& frame){
	std::shared_ptr<ParticleProperty> property(new ParticleProperty(frame.ids.size(), ParticleProperty::IdentifierProperty, 1, false));

	if(!property || property->size() != frame.ids.size()){
		spdlog::error("Failed to allocate ParticleProperty for identifiers with correct size");
		return nullptr;
	}

	for(size_t i = 0; i < frame.ids.size(); i++){
		property->setInt(i, frame.ids[i]);
	}

	return property;
}

std::shared_ptr<ParticleProperty> FrameAdapter::createIntPropertyShared(
	const LammpsParser::Frame& frame,
	const std::string& columnName
){
	const auto* column = requireColumn(frame, columnName, DataType::Int);
	if(!column){
		return nullptr;
	}

	auto property = std::make_shared<ParticleProperty>();
	property->bindExternalData(
		const_cast<int*>(column->ints.data()),
		column->ints.size(),
		DataType::Int,
		1,
		sizeof(int),
		frameOwner(frame)
	);
	return property;
}

std::shared_ptr<ParticleProperty> FrameAdapter::createInt64PropertyShared(
	const LammpsParser::Frame& frame,
	const std::string& columnName
){
	const auto* column = requireColumn(frame, columnName, DataType::Int64);
	if(!column){
		return nullptr;
	}

	auto property = std::make_shared<ParticleProperty>();
	property->bindExternalData(
		const_cast<std::int64_t*>(column->int64s.data()),
		column->int64s.size(),
		DataType::Int64,
		1,
		sizeof(std::int64_t),
		frameOwner(frame)
	);
	return property;
}

std::shared_ptr<ParticleProperty> FrameAdapter::createQuaternionPropertyShared(
	const LammpsParser::Frame& frame,
	const std::string& xColumn,
	const std::string& yColumn,
	const std::string& zColumn,
	const std::string& wColumn
){
	const auto* x = requireColumn(frame, xColumn, DataType::Double);
	const auto* y = requireColumn(frame, yColumn, DataType::Double);
	const auto* z = requireColumn(frame, zColumn, DataType::Double);
	const auto* w = requireColumn(frame, wColumn, DataType::Double);
	if(!x || !y || !z || !w){
		return nullptr;
	}

	auto property = std::make_shared<ParticleProperty>(
		static_cast<std::size_t>(frame.natoms),
		DataType::Double,
		4,
		0,
		false
	);
	if(!property || property->size() != static_cast<std::size_t>(frame.natoms)){
		spdlog::error("Failed to allocate quaternion property for {} atoms", frame.natoms);
		return nullptr;
	}

	for(std::size_t atomIndex = 0; atomIndex < static_cast<std::size_t>(frame.natoms); ++atomIndex){
		property->setDoubleComponent(atomIndex, 0, x->doubles[atomIndex]);
		property->setDoubleComponent(atomIndex, 1, y->doubles[atomIndex]);
		property->setDoubleComponent(atomIndex, 2, z->doubles[atomIndex]);
		property->setDoubleComponent(atomIndex, 3, w->doubles[atomIndex]);
	}

	property->setType(ParticleProperty::OrientationProperty);
	return property;
}

bool FrameAdapter::validateSimulationCell(const SimulationCell& cell){
	const AffineTransformation& matrix = cell.matrix();
	for(int i = 0; i < 3; i++){
		for(int j = 0; j < 3; j++){
			double value = matrix(i, j);
			if(std::isnan(value) || std::isinf(value)){
				spdlog::error("Invalid cell matrix component at ({},{}): {}", i, j, value);
				return false;
			}
		}
	}

	double volume = cell.volume3D();
	if(volume <= 0 || std::isnan(volume) || std::isinf(volume)){
		spdlog::error("Invalid cell volume: {}", volume);
		return false;
	}

	return true;
}

}
