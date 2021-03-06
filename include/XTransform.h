#ifndef XTRANSFORM_H
#define XTRANSFORM_H

#include "X3DGlobal.h"
#undef min
#undef max
#include "Math/Eigen/Geometry"
#include "Math/XMathVector.h"
#include "Math/XMathMatrix.h"

namespace Eks
{

typedef Eigen::Affine3f Transform;
typedef Eigen::Projective3f ComplexTransform;

namespace TransformUtilities
{
Transform EKS3D_EXPORT lookAt(const Vector3D &eye, const Vector3D &aim, const Vector3D &up);
ComplexTransform EKS3D_EXPORT perspective(Real angle, Real aspect, Real nearPlane, Real farPlane);
}
}

namespace Eigen
{
template <typename A, int B, int C, int D> std::ostream &operator<<(std::ostream &str, const Eigen::Transform <A, B, C, D> &data)
  {
  return str << data.matrix();
  }

template <typename A, int B, int C, int D> std::istream &operator>>(std::istream &str, Eigen::Transform <A, B, C, D> &data)
  {
  return str >> data.matrix();
  }

template <typename Derived, int Dim, int Mode, int Opt> bool operator!=(const Eigen::Transform<Derived, Dim, Mode, Opt> &a, const Eigen::Transform<Derived, Dim, Mode, Opt> &b)
  {
  return a != b;
  }
}

#if X_QT_INTEROP

#include "QDebug"

template <typename A, int B, int C, int D> QDebug operator <<(QDebug str, const Eigen::Transform <A, B, C, D> &data)
  {
  return str << data.matrix();
  }

template <typename A, int B, int C, int D, typename E, int F, int G, int H>
    bool operator ==(const Eigen::Transform <A, B, C, D> &a, const Eigen::Transform <E, F, G, H> &b)
  {
  return a.matrix() == b.matrix();
  }


template <typename A, int B, int C, int D, typename E, int F, int G, int H>
    bool operator !=(const Eigen::Transform <A, B, C, D> &a, const Eigen::Transform <E, F, G, H> &b)
  {
  return a.matrix() != b.matrix();
  }

#endif

#endif // XTRANSFORM_H
