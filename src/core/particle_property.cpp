#include <volt/core/particle_property.h>
#include <stdexcept>
#include <cassert>

namespace Volt { namespace Particles {

ParticleProperty::ParticleProperty()
    : PropertyBase()
    , _type(UserProperty)
{}

ParticleProperty::ParticleProperty(size_t particleCount,
                                   Type   type,
                                   size_t componentCount,
                                   bool   initializeMemory)
    : PropertyBase(
        particleCount,
        (type == ParticleTypeProperty || type == StructureTypeProperty || type == SelectionProperty || type == ClusterProperty || type == CoordinationProperty || type == IdentifierProperty || type == MoleculeProperty || type == MoleculeTypeProperty || type == PeriodicImageProperty) ? DataType::Int :
        (type == UserProperty) ? DataType::Void : DataType::Double,
        (componentCount > 0) ? componentCount :
        (type == PositionProperty || type == DisplacementProperty || type == VelocityProperty || type == ForceProperty || type == DipoleOrientationProperty || type == AngularVelocityProperty || type == AngularMomentumProperty || type == TorqueProperty || type == AsphericalShapeProperty || type == ColorProperty || type == VectorColorProperty || type == PeriodicImageProperty) ? 3 :
        (type == StressTensorProperty || type == StrainTensorProperty || type == ElasticStrainTensorProperty || type == StretchTensorProperty) ? 6 :
        (type == DeformationGradientProperty || type == ElasticDeformationGradientProperty) ? 9 :
        (type == OrientationProperty || type == RotationProperty) ? 4 : 1,
        (componentCount > 0) ? ((type == ParticleTypeProperty || type == StructureTypeProperty || type == SelectionProperty || type == ClusterProperty || type == CoordinationProperty || type == IdentifierProperty || type == MoleculeProperty || type == MoleculeTypeProperty || type == PeriodicImageProperty) ? componentCount * sizeof(int) : componentCount * sizeof(double)) :
        (type == PositionProperty || type == DisplacementProperty || type == VelocityProperty || type == ForceProperty || type == DipoleOrientationProperty || type == AngularVelocityProperty || type == AngularMomentumProperty || type == TorqueProperty || type == AsphericalShapeProperty) ? sizeof(Vector3) :
        (type == ColorProperty || type == VectorColorProperty) ? 3 * sizeof(double) :
        (type == StressTensorProperty || type == StrainTensorProperty || type == ElasticStrainTensorProperty || type == StretchTensorProperty) ? sizeof(SymmetricTensor2) :
        (type == DeformationGradientProperty || type == ElasticDeformationGradientProperty) ? 9 * sizeof(double) :
        (type == OrientationProperty || type == RotationProperty) ? sizeof(Quaternion) :
        (type == PeriodicImageProperty) ? 3 * sizeof(int) :
        (type == ParticleTypeProperty || type == StructureTypeProperty || type == SelectionProperty || type == ClusterProperty || type == CoordinationProperty || type == IdentifierProperty || type == MoleculeProperty || type == MoleculeTypeProperty) ? sizeof(int) : sizeof(double),
        initializeMemory)
    , _type(type)
{
    if (_dataType == DataType::Void && _componentCount > 0) {
        throw std::runtime_error("ParticleProperty: No se puede crear una propiedad de datos con DATATYPE_VOID y componentCount > 0");
    }
}

ParticleProperty::ParticleProperty(size_t              particleCount,
                                   DataType                 dataType,
                                   size_t              componentCount,
                                   size_t              stride,
                                   bool                initializeMemory)
    : PropertyBase(particleCount, dataType, componentCount, stride, initializeMemory)
    , _type(UserProperty)
{
}

ParticleProperty::ParticleProperty(const ParticleProperty& other)
    : PropertyBase(other)
    , _type(other._type)
{}

void ParticleProperty::bindExternalData(
    void* data,
    std::size_t count,
    DataType dataType,
    std::size_t componentCount,
    std::size_t stride,
    std::shared_ptr<void> owner
){
    PropertyBase::bindExternalData(data, count, dataType, componentCount, stride, std::move(owner));
}

}}
