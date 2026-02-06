#pragma once

#include <volt/core/volt.h>
#include "vector3.h"
#include "rotation.h"
#include "scaling.h"
#include "affine_transformation.h"

namespace Volt{ 

class AffineDecomposition{
public:
	Vector3 translation;
	Quaternion rotation;
	Scaling scaling;
	double sign;	

	AffineDecomposition(const AffineTransformation& tm);
};

}