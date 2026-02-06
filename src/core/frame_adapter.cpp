#include <volt/core/frame_adapter.h>
#include <spdlog/spdlog.h>
#include <cmath>

namespace Volt{

using namespace Volt::Particles;

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
