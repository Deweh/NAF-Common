#pragma once

namespace Physics
{
	template <typename T>
	struct Constraint
	{
		virtual T Apply(const T& a_current) = 0;
	};

	using AngularConstraint = Constraint<ozz::math::SimdQuaternion>;
	using LinearConstraint = Constraint<ozz::math::SimdFloat4>;

	struct LinearBoxConstraint : public LinearConstraint
	{
		ozz::math::SimdFloat4 min;
		ozz::math::SimdFloat4 max;
		float softness;

		virtual ozz::math::SimdFloat4 Apply(const ozz::math::SimdFloat4& a_position) override;
	};

	struct LinearSphereConstraint : public LinearConstraint
	{
		float radius;
		float softness;

		virtual ozz::math::SimdFloat4 Apply(const ozz::math::SimdFloat4& a_position) override;
	};

	struct AngularConeConstraint : public AngularConstraint
	{
		ozz::math::SimdFloat4 axis;
		float halfAngle;
		float softness;

		virtual ozz::math::SimdQuaternion Apply(const ozz::math::SimdQuaternion& a_rotation) override;
	};
}