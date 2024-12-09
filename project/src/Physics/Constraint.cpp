#include "Constraint.h"
#include "ModelSpaceSystem.h"

namespace Physics
{
	using namespace ozz::math;
	constexpr float deltaTime = ModelSpaceSystem::FIXED_TIMESTEP;
	constexpr float deltaInv = { 1.0f / deltaTime };

	namespace
	{
		inline SimdFloat4 LinearFullToRelative(const SimdFloat4& a_current, const SimdFloat4& a_constrainedTo)
		{
			return a_current - a_constrainedTo;
		}

		inline SimdFloat4 LinearRelativeToFull(const SimdFloat4& a_relative, const SimdFloat4& a_constrainedTo)
		{
			return a_constrainedTo + a_relative;
		}

		inline SimdFloat4 IntegrateHardCollisionVelocity(const SimdFloat4& a_boundaryNormal, const SimdFloat4& a_velocity, float bounce = 0.1f)
		{
			const float velAlongNormal = GetX(Dot3(a_boundaryNormal, a_velocity));
			const float magnitude = (1.0f + bounce) * velAlongNormal;
			return a_velocity - simd_float4::Load1(magnitude) * a_boundaryNormal;
		}

		inline SimdQuaternion AngularFullToRelative(const SimdQuaternion& a_current, const SimdQuaternion& a_constrainedTo, bool* a_flippedSign)
		{
			SimdQuaternion fromRot = a_constrainedTo;
			if (GetX(Dot4(fromRot.xyzw, a_current.xyzw)) < 0.00001f) {
				fromRot = -fromRot;
				*a_flippedSign = true;
			}

			return Conjugate(fromRot) * a_current;
		}

		inline SimdQuaternion AngularRelativeToFull(const SimdQuaternion& a_relative, const SimdQuaternion& a_constrainedTo, bool a_flippedSign)
		{
			if (a_flippedSign) {
				return (-a_constrainedTo) * a_relative;
			} else {
				return a_constrainedTo * a_relative;
			}
		}
	}

	void LinearBoxConstraint::Apply(const Data& a_data)
	{
		const SimdFloat4 currentPos = a_data.property->current;
		const SimdFloat4 relativePos = LinearFullToRelative(currentPos, a_data.constrainedTo);
		const SimdFloat4 correctedPosition = Clamp(min, relativePos, max);

		const SimdInt4 minExceededMask = CmpLt(relativePos, min);
		const SimdInt4 maxExceededMask = CmpGt(relativePos, max);
		const SimdFloat4 boundaryNormal = (-simd_float4::FromInt(minExceededMask)) + simd_float4::FromInt(maxExceededMask);

		a_data.property->current = LinearRelativeToFull(correctedPosition, a_data.constrainedTo);
		a_data.property->velocity = IntegrateHardCollisionVelocity(boundaryNormal, a_data.property->velocity, bounce);
	}

	void LinearSphereConstraint::Apply(const Data& a_data)
	{
		const SimdFloat4 relativePos = LinearFullToRelative(a_data.property->current, a_data.constrainedTo);
		const float currentDistance = GetX(Length3(relativePos));

		if (currentDistance > radius) {
			const float correctionFactor = (radius / currentDistance);
			const SimdFloat4 correctedPosition = relativePos * simd_float4::Load1(correctionFactor);
			a_data.property->current = LinearRelativeToFull(correctedPosition, a_data.constrainedTo);
			a_data.property->velocity = IntegrateHardCollisionVelocity(Normalize3(correctedPosition), a_data.property->velocity, bounce);
		}
	}

	void AngularConeConstraint::Apply(const Data& a_data)
	{
		bool flippedSign = false;
		const SimdQuaternion relativeRot = AngularFullToRelative(a_data.property->current, a_data.constrainedTo, &flippedSign);
		const SimdFloat4 relativeAxisAngle = ToAxisAngle(relativeRot);
		const float angle = GetW(relativeAxisAngle);

		if (angle > halfAngle) {
			const SimdFloat4 boundaryNormal = SetW(relativeAxisAngle, simd_float4::zero());
			const SimdQuaternion correctedRot = SimdQuaternion::FromAxisAngle(boundaryNormal, simd_float4::Load1(halfAngle));
			a_data.property->current = AngularRelativeToFull(Normalize(correctedRot), a_data.constrainedTo, flippedSign);
			a_data.property->velocity = IntegrateHardCollisionVelocity(boundaryNormal, a_data.property->velocity, bounce);
		}
	}
}