#include <volt/core/frame_adapter.h>
#include <cmath>
#include <format>
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

template<typename T>
std::shared_ptr<ParticleProperty> createScalarPropertyShared(
	const LammpsParser::Frame& frame,
	const std::vector<T>& buffer,
	DataType dataType
){
	auto property = std::make_shared<ParticleProperty>();
	property->bindExternalData(
		const_cast<T*>(buffer.data()),
		buffer.size(),
		dataType,
		1,
		sizeof(T),
		frameOwner(frame)
	);
	return property;
}

const LammpsParser::AtomColumn* requireColumn(
	const LammpsParser::Frame& frame,
	const std::string& columnName,
	DataType expectedType,
	std::string* errorMessage
){
	const auto* column = frame.findAtomProperty(columnName);
	if(!column){
		setError(errorMessage, std::format("Frame is missing required atom column '{}'", columnName));
		return nullptr;
	}
	if(column->dataType != expectedType){
		setError(errorMessage, std::format(
			"Atom column '{}' has unexpected data type {} (expected {})",
			columnName,
			static_cast<int>(column->dataType),
			static_cast<int>(expectedType)
		));
		return nullptr;
	}
	if(column->size() != static_cast<std::size_t>(frame.natoms)){
		setError(errorMessage, std::format(
			"Atom column '{}' has {} rows but frame reports {} atoms",
			columnName,
			column->size(),
			frame.natoms
		));
		return nullptr;
	}
	return column;
}

std::shared_ptr<ParticleProperty> bindColumnShared(const LammpsParser::Frame& frame, const LammpsParser::AtomColumn& column){
	switch(column.dataType){
		case DataType::Int:
			return createScalarPropertyShared(frame, column.ints, DataType::Int);
		case DataType::Int64:
			return createScalarPropertyShared(frame, column.int64s, DataType::Int64);
		case DataType::Double:
			return createScalarPropertyShared(frame, column.doubles, DataType::Double);
		case DataType::Void:
			break;
	}
	return nullptr;
}

}

using namespace FrameAdapterDetail;

std::shared_ptr<ParticleProperty> FrameAdapter::createPositionProperty(
	const LammpsParser::Frame& frame,
	std::string* errorMessage
){
	std::shared_ptr<ParticleProperty> property(new ParticleProperty(frame.natoms, ParticleProperty::PositionProperty, 0, true));

	if(!property || property->size() != static_cast<size_t>(frame.natoms)){
		setError(errorMessage, "Failed to allocate ParticleProperty for positions with correct size");
		return nullptr;
	}

	Point3* data = property->dataPoint3();
	if(!data){
		setError(errorMessage, "Failed to get position data pointer from ParticleProperty");
		return nullptr;
	}

	for(size_t i = 0; i < frame.positions.size() && i < static_cast<size_t>(frame.natoms); i++){
		data[i] = frame.positions[i];
	}

	return property;
}

std::shared_ptr<ParticleProperty> FrameAdapter::createPositionPropertyShared(
	const LammpsParser::Frame& frame,
	std::string* errorMessage
){
	auto property = std::make_shared<ParticleProperty>();
	if(frame.positions.empty()){
		setError(errorMessage, "Frame positions buffer is empty");
		return nullptr;
	}
	if(frame.positions.size() < static_cast<std::size_t>(frame.natoms)){
		setError(errorMessage, std::format(
			"Frame positions buffer has {} entries but frame reports {} atoms",
			frame.positions.size(),
			frame.natoms
		));
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

	std::string reason;
	if(!validateSimulationCell(frame.simulationCell, &reason)){
		setError(errorMessage, "Invalid simulation cell: " + reason);
		return false;
	}

	prepared.positions = createPositionPropertyShared(frame, &reason);
	if(!prepared.positions){
		setError(errorMessage, "Failed to create position property: " + reason);
		return false;
	}

	return true;
}

std::shared_ptr<ParticleProperty> FrameAdapter::createIdentifierProperty(
	const LammpsParser::Frame& frame,
	std::string* errorMessage
){
	std::shared_ptr<ParticleProperty> property(new ParticleProperty(frame.ids.size(), ParticleProperty::IdentifierProperty, 1, false));

	if(!property || property->size() != frame.ids.size()){
		setError(errorMessage, "Failed to allocate ParticleProperty for identifiers with correct size");
		return nullptr;
	}

	for(size_t i = 0; i < frame.ids.size(); i++){
		property->setInt(i, frame.ids[i]);
	}

	return property;
}

std::shared_ptr<ParticleProperty> FrameAdapter::createIntPropertyShared(
	const LammpsParser::Frame& frame,
	const std::string& columnName,
	std::string* errorMessage
){
	const auto* column = requireColumn(frame, columnName, DataType::Int, errorMessage);
	if(!column){
		return nullptr;
	}
	return createScalarPropertyShared(frame, column->ints, DataType::Int);
}

std::shared_ptr<ParticleProperty> FrameAdapter::createInt64PropertyShared(
	const LammpsParser::Frame& frame,
	const std::string& columnName,
	std::string* errorMessage
){
	const auto* column = requireColumn(frame, columnName, DataType::Int64, errorMessage);
	if(!column){
		return nullptr;
	}
	return createScalarPropertyShared(frame, column->int64s, DataType::Int64);
}

std::shared_ptr<ParticleProperty> FrameAdapter::createQuaternionPropertyShared(
	const LammpsParser::Frame& frame,
	const std::string& xColumn,
	const std::string& yColumn,
	const std::string& zColumn,
	const std::string& wColumn,
	std::string* errorMessage
){
	const auto* x = requireColumn(frame, xColumn, DataType::Double, errorMessage);
	const auto* y = x ? requireColumn(frame, yColumn, DataType::Double, errorMessage) : nullptr;
	const auto* z = y ? requireColumn(frame, zColumn, DataType::Double, errorMessage) : nullptr;
	const auto* w = z ? requireColumn(frame, wColumn, DataType::Double, errorMessage) : nullptr;
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
		setError(errorMessage, std::format("Failed to allocate quaternion property for {} atoms", frame.natoms));
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

std::shared_ptr<ParticleProperty> FrameAdapter::findColumnShared(
	const LammpsParser::Frame& frame,
	std::initializer_list<const char*> columnNames,
	DataType dataType
){
	for(const char* columnName : columnNames){
		const auto* column = frame.findAtomProperty(columnName);
		if(column && column->dataType == dataType && column->size() == static_cast<std::size_t>(frame.natoms)){
			return bindColumnShared(frame, *column);
		}
	}
	return nullptr;
}

bool FrameAdapter::validateSimulationCell(const SimulationCell& cell, std::string* errorMessage){
	const AffineTransformation& matrix = cell.matrix();
	for(int i = 0; i < 3; i++){
		for(int j = 0; j < 3; j++){
			double value = matrix(i, j);
			if(std::isnan(value) || std::isinf(value)){
				setError(errorMessage, std::format("Invalid cell matrix component at ({},{}): {}", i, j, value));
				return false;
			}
		}
	}

	double volume = cell.volume3D();
	if(volume <= 0 || std::isnan(volume) || std::isinf(volume)){
		setError(errorMessage, std::format("Invalid cell volume: {}", volume));
		return false;
	}

	return true;
}

}
