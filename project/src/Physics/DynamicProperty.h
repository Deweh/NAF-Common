#pragma once
#include "ModelSpaceSystem.h"

namespace Physics
{
	template <typename T>
	struct DynamicProperty
	{
		T current;
		T previous;
		ozz::math::SimdFloat4 velocity;
	};

	inline void IntegrateLinearStep(DynamicProperty<ozz::math::SimdFloat4>& a_prop, const ozz::math::SimdFloat4& a_acceleration)
	{
		constexpr float deltaTime = ModelSpaceSystem::FIXED_TIMESTEP;
		using namespace ozz::math;

		// Euler integration.
		const SimdFloat4 dtSimd = simd_float4::Load1(deltaTime);
		const ozz::math::SimdFloat4 currentPos = a_prop.current;
		a_prop.velocity = a_prop.velocity + (a_acceleration * dtSimd);
		a_prop.current = currentPos + (a_prop.velocity * dtSimd);
		a_prop.previous = currentPos;
	}

	inline void IntegrateAngularStep(DynamicProperty<ozz::math::SimdQuaternion>& a_prop, const ozz::math::SimdFloat4& a_acceleration)
	{
		constexpr float deltaTime = ModelSpaceSystem::FIXED_TIMESTEP;
		using namespace ozz::math;

		// Quaternion integration.
		const ozz::math::SimdQuaternion currentRot = a_prop.current;
		const SimdFloat4 dtSimd = simd_float4::Load1(deltaTime);
		const SimdFloat4 newVel = a_prop.velocity + (a_acceleration * dtSimd);
		a_prop.velocity = newVel;
		const SimdFloat4 deltaAngle = SplatX(Length3(newVel)) * dtSimd;
		if (AreAllTrue1(CmpGt(deltaAngle, simd_float4::zero()))) {
			a_prop.current = currentRot * SimdQuaternion::FromAxisAngle(Normalize3(newVel), deltaAngle);
		}
		a_prop.previous = currentRot;
	}
}