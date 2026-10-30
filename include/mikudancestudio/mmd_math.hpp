// MikuDanceStudio additions: MMD numeric operations with VC9 rounding.
#ifndef MIKUDANCESTUDIO_MMD_MATH_HPP
#define MIKUDANCESTUDIO_MMD_MATH_HPP
#include "LinearMath/btMatrix3x3.h"
#include "LinearMath/btVector3.h"
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
namespace mikudancestudio::mmd_math {
btMatrix3x3 inverseInertiaTensor(const btMatrix3x3& basis, const btVector3& local);
float sinTimes(float angle, float factor);
float cosine(float angle);
float dot3(const float* left, const float* right);
float squaredDistance3(const float* left, const float* right);
void cross3(float* out, const float* left, const float* right);
float edgeComponent(float p, float n, float a);
float pmxEdgeComponent(float position, float normal, float frameScale, float vertexScale,
                       float materialSize);
float edgeDistanceScale(float distance, float fov, float modelScale);
float blendTransformComponent(const float* input, const float* matrix0, const float* matrix1,
                              int component, float weight0, bool position);
float boneVCoordinate(int n, int o, float len, float s);
void boneMarkerBounds(int x, int y, float scale, float* left, float* top, float* right,
                      float* bottom);
void __fastcall rotationBasis(float* basis, const float* quaternion);
void __fastcall transformPoint(float* out, const float* point, const float* matrix);
float lengthXYZ(const btVector3& v);
void normalizeDirection(const btVector3& direction, btVector3& out);
float determinant(const btVector3& a, const btVector3& b, const btVector3& c);
void triangleWeights(const btVector3& a, const btVector3& b, float lengthSquared, float* weights);
void tetrahedronWeights(float detCBD, float detACD, float detBAD, float volume, float* weights);
float lengthYZX(const btVector3& v);
float lengthXZY(const btVector3& v);
void normalize(btVector3& vector, float lengthSquared);
void tailNormalize(btVector3& normal, float lengthSquared, float distanceSquared,
                   float* inverseLengthOut, float* distanceOut);
float negativeDistance(const btVector3& left, const btVector3& right);
float distanceMinusMargin(const btVector3& left, const btVector3& right, float margin);
float capsuleDistance(float lengthSquared, float radiusA, float radiusB);
void capsuleNormal(btVector3& normal, const btVector3& points, float lengthSquared);
void planeSpace(btVector3& normal, float a, float b, bool zBranch);
void normalizeSupport(btVector3& vector);
void normalizeSupportLength(btVector3& vector, float lengthSquared);
void quaternionAngle(const float* quaternion, float* inverseLengthOut, float* angleOut);
void quaternionRootParts(float diagonalI, float diagonalJ, float diagonalK, bool positiveTrace,
                         float* rootOut, float* halfRootOut);
void integrateRotation(const float* angularVelocity, float timeStep, float* axis, float* cosineOut);
void gravityNoise(int rStrength, int rX, int rY, int rZ, int noise, float magnitude,
                  const float* direction, float* gravity);
} // namespace mikudancestudio::mmd_math
#endif
#endif
