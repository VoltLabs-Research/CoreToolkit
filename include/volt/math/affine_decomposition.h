#pragma once

#include <volt/core/volt.h>
#include "vector3.h"
#include "rotation.h"
#include "scaling.h"
#include "affine_transformation.h"
#include "matrix4.h"

namespace Volt{ 

class AffineDecomposition{
public:
	Vector3 translation;
	Quaternion rotation;
	Scaling scaling;
	double sign;	

	AffineDecomposition(const AffineTransformation& tm);
};

// The polar decomposition of Graphics Gems IV ("Polar Matrix Decomposition", Ken Shoemake) the constructor runs.
namespace AffineDecompositionDetail{

void adjoint_transpose(Matrix4& M, Matrix4& MadjT);
Quaternion Qt_FromMatrix(const Matrix4& mat);
int find_max_col(const Matrix4& M);
void make_reflector(const Vector3& v, Vector3& u);
void reflect_cols(Matrix4& M, const Vector3& u);
void reflect_rows(Matrix4& M, const Vector3& u);
void do_rank1(Matrix4& M, Matrix4& Q);
void do_rank2(Matrix4& M, Matrix4& MadjT, Matrix4& Q);
double polar_decomp(Matrix4& M, Matrix4& Q, Matrix4& S);
Vector3 spect_decomp(Matrix4& S, Matrix4& U);
Quaternion snuggle(Quaternion q, Vector3& k);
void decomp_affine(Matrix4& A, AffineDecomposition* parts);

}

}