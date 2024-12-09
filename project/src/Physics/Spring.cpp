#include "Spring.h"

namespace Physics
{
	using namespace ozz::math;

	ozz::math::SimdFloat4 Spring::CalculateLinearForces(const DynamicProperty<ozz::math::SimdFloat4>& a_prop, const ozz::math::SimdFloat4& a_restPos) const
	{
		// Hooke's law.
		const SimdFloat4 displacement = a_prop.current - a_restPos;
		const SimdFloat4 springForce = displacement * simd_float4::Load1(-stiffness);

		// Calculate damping by opposing velocity.
		const SimdFloat4 dampingForce = -a_prop.velocity * simd_float4::Load1(damping);

		return springForce + dampingForce;
	}

	ozz::math::SimdFloat4 Spring::CalculateAngularTorques(const DynamicProperty<ozz::math::SimdQuaternion>& a_prop, const ozz::math::SimdQuaternion& a_restRot) const
	{
		SimdQuaternion fromRot = a_prop.current;
		if (GetX(Dot4(fromRot.xyzw, a_restRot.xyzw)) < 0.00001f) {
			fromRot = -fromRot;
		}

		// Hooke's law.
		const SimdFloat4 displacement = ToAxisAngle(Conjugate(fromRot) * a_restRot);
		const SimdFloat4 dispAxis = SetW(displacement, simd_float4::zero());
		const SimdFloat4 dispAngle = SplatW(displacement);
		const SimdFloat4 springTorque = dispAxis * dispAngle * simd_float4::Load1(stiffness);

		// Calculate damping by opposing velocity.
		const SimdFloat4 dampingTorque = -a_prop.velocity * simd_float4::Load1(damping);

		return springTorque + dampingTorque;
	}
}