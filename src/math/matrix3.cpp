#include <volt/math/matrix3.h>

namespace Volt{

template<typename T>
bool Matrix_3<T>::orthonormalized(Matrix_3& output) const noexcept{
	Vector_3<T> c0 = column(0);
	Vector_3<T> c1 = column(1);
	Vector_3<T> c2 = column(2);

	const T l0 = c0.length();
	if(l0 <= T(EPSILON)){
		return false;
	}
	c0 /= l0;

	c1 -= c0 * c0.dot(c1);
	const T l1 = c1.length();
	if(l1 <= T(EPSILON)){
		return false;
	}
	c1 /= l1;

	c2 -= c0 * c0.dot(c2);
	c2 -= c1 * c1.dot(c2);
	const T l2 = c2.length();
	if(l2 <= T(EPSILON)){
		return false;
	}
	c2 /= l2;

	output = Matrix_3(c0, c1, c2);
	if(output.determinant() < T(0)){
		output.column(2) = -output.column(2);
	}
	return true;
}

template bool Matrix_3<double>::orthonormalized(Matrix_3<double>& output) const noexcept;

}
