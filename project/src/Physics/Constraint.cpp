#include "Constraint.h"

namespace Physics
{
	using namespace ozz::math;

	ozz::math::SimdFloat4 LinearBoxConstraint::Apply(const ozz::math::SimdFloat4& a_position)
	{
		const SimdFloat4 constrainedPos = Clamp(min, a_position, max);
		return Lerp(constrainedPos, a_position, simd_float4::Load1(softness));
	}

	ozz::math::SimdFloat4 LinearSphereConstraint::Apply(const ozz::math::SimdFloat4& a_position)
	{
		const float currentDistance = GetX(Length3(a_position));

		if (currentDistance > radius)
		{
			const float correctionFactor = (radius / currentDistance);
			const SimdFloat4 correctedPosition = a_position * simd_float4::Load1(correctionFactor);
			return Lerp(correctedPosition, a_position, simd_float4::Load1(softness));
		} else {
			return a_position;
		}
	}

	ozz::math::SimdQuaternion AngularConeConstraint::Apply(const ozz::math::SimdQuaternion& a_rotation)
	{
		const SimdFloat4 currentDir = TransformVector(a_rotation, axis);
		const float deltaAngle = std::acos(GetX(Dot3(currentDir, axis)));

		if (deltaAngle > halfAngle) {
			const float excessAngle = (deltaAngle - halfAngle) * (1.0f - softness);
			const SimdFloat4 correctionAxis = SetW(NormalizeEst3(Cross3(axis, currentDir)), simd_float4::zero());
			const SimdQuaternion correctionRot = SimdQuaternion::FromAxisAngle(correctionAxis, simd_float4::Load1(-excessAngle));
			return Normalize(a_rotation * correctionRot);
		}
	}
}