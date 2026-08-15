#ifndef CUTOFF_NEIGHBOR_FINDER_H
#define CUTOFF_NEIGHBOR_FINDER_H

#include <volt/core/volt.h>
#include <volt/core/simulation_cell.h>
#include <volt/core/particle_property.h>
#include <volt/math/lin_alg.h>

namespace Volt{

class CutoffNeighborFinder{
private:
    struct NeighborListParticle{
        Point3 pos;
        Vector_3<int8_t> pbcShift;
    };

public:
    CutoffNeighborFinder(): _cutoffRadius(0), _cutoffRadiusSquared(0){}

    bool prepare(double cutoffRadius, ParticleProperty* positions, const SimulationCell& simCell);

    double cutoffRadius() const{
        return _cutoffRadius;
    }

    double cutoffRadiusSquared() const{
        return _cutoffRadiusSquared;
    }

    class Query{
    public:
        Query(const CutoffNeighborFinder& finder, size_t particleIndex);

        bool atEnd() const{
            return _atEnd;
        }

        void next();

        size_t current(){
            return _neighborIndex;
        }

        const Vector3& delta() const{
            return _delta;
        }

        double distanceSquared() const{
            return _distSq;
        }

		const Vector_3<int8_t>& pbcShift() const{
            return _pbcShift;
        }

        Vector_3<int8_t> unwrappedPbcShift() const {
			const auto& s1 = _builder.particles[_centerIndex].pbcShift;
			const auto& s2 = _builder.particles[_neighborIndex].pbcShift;
			return Vector_3<int8_t>(
					_pbcShift.x() - s1.x() + s2.x(),
					_pbcShift.y() - s1.y() + s2.y(),
					_pbcShift.z() - s1.z() + s2.z());
		}
    
    private:
        const CutoffNeighborFinder& _builder;
        bool _atEnd;
        Point3 _center, _shiftedCenter;
        size_t _centerIndex;
        std::vector<Vector3I>::const_iterator _stencilIter;
        Point3I _centerBin;
        Point3I _currentBin;
        uint32_t _binCursor;
        uint32_t _binEnd;
        size_t _neighborIndex;
        Vector_3<int8_t> _pbcShift;
        Vector3 _delta;
        double _distSq;
    };

private:
    double _cutoffRadius;
    double _cutoffRadiusSquared;
    
    SimulationCell simCell;

    int binDim[3];

    AffineTransformation reciprocalBinCell;

    std::vector<NeighborListParticle> particles;

    std::vector<uint32_t> binStart;
    std::vector<uint32_t> binnedIndices;

	std::vector<Vector3I> stencil;
};

}

#endif