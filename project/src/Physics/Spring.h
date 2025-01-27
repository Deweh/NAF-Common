#pragma once
#include "DynamicProperty.h"

namespace Physics
{
	class Spring
	{
	public:
		float stiffness;
		float damping;

		ozz::math::SimdFloat4 CalculateLinearForces(const DynamicProperty<ozz::math::SimdFloat4>& a_prop, const ozz::math::SimdFloat4& a_restPos) const;
		ozz::math::SimdFloat4 CalculateAngularTorques(const DynamicProperty<ozz::math::SimdQuaternion>& a_prop, const ozz::math::SimdQuaternion& a_restRot) const;
	};

	struct SpringWithBodyProperties
	{
		ozz::math::SimdFloat4 gravity;
		ozz::math::SimdFloat4 upAxis;
		Spring spring;
		float mass;
		float linearToAngularScale{ 1.0f };
	};
}