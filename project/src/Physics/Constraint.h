#pragma once
#include "DynamicProperty.h"
#include "Spring.h"

namespace Physics
{
	template <typename T>
	struct Constraint
	{
		struct Data
		{
			DynamicProperty<T>* property;
			T constrainedTo;
			float massInverse;
		};

		virtual void Apply(const Data& a_data) = 0;
	};

	using AngularConstraint = Constraint<ozz::math::SimdQuaternion>;
	using LinearConstraint = Constraint<ozz::math::SimdFloat4>;

	struct LinearBoxConstraint : public LinearConstraint
	{
		ozz::math::SimdFloat4 min;
		ozz::math::SimdFloat4 max;
		float bounce;
		Spring* softSpring;

		virtual void Apply(const Data& a_data) override;
	};

	struct LinearSphereConstraint : public LinearConstraint
	{
		float radius;
		float bounce;
		Spring* softSpring;

		virtual void Apply(const Data& a_data) override;
	};

	struct AngularConeConstraint : public AngularConstraint
	{
		float halfAngle;
		float bounce;
		Spring* softSpring;

		virtual void Apply(const Data& a_data) override;
	};
}