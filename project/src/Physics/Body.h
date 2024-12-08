#pragma once
#include "Constraint.h"

namespace Physics
{
	class ModelSpaceSystem;

	struct Transform
	{
		ozz::math::SimdFloat4 position;
		ozz::math::SimdQuaternion rotation;
	};

	template <typename T>
	struct DynamicProperty
	{
		T current;
		T previous;
		ozz::math::SimdFloat4 velocity;
	};

	struct SpringProperties
	{
		float stiffness;
		float damping;
		float mass;
		ozz::math::SimdFloat4 gravity;
		ozz::math::SimdFloat4 upAxis;
	};

	class Body
	{
	public:
		struct UpdateContext
		{
			ModelSpaceSystem* system = nullptr;
			LinearConstraint* linearConstraint = nullptr;
			AngularConstraint* angularConstraint = nullptr;
			SpringProperties* linearSpring = nullptr;
			SpringProperties* angularSpring = nullptr;
			ozz::math::Float4x4 animatedTransform;
			ozz::math::Float4x4 parentTransform;
		};

		struct SubStepConstants
		{
			struct SpringConstants
			{
				ozz::math::SimdFloat4 force;
				ozz::math::SimdFloat4 dampingCoeff;
				float massInverse;
			};
			
			SpringConstants linearSpring;
			SpringConstants angularSpring;
			ozz::math::SimdFloat4 restPositionMS;
			ozz::math::SimdQuaternion restRotationMS;
			ozz::math::SimdQuaternion parentInverseRotMS;
			ozz::math::Float4x4 parentInverseMS;
		};

		DynamicProperty<ozz::math::SimdFloat4> position;
		DynamicProperty<ozz::math::SimdQuaternion> rotation;

		Transform Update(const UpdateContext& a_context);

	protected:
		void CalculateSpringConstants(const UpdateContext& a_context, SpringProperties& a_props, SubStepConstants::SpringConstants& a_constantsOut);
		void BeginStepUpdate(const UpdateContext& a_context, SubStepConstants& a_constants);
		void ProcessPhysicsStep(const UpdateContext& a_context, const SubStepConstants& a_constants);

		void ProcessLinearSpringStep(const UpdateContext& a_context, const SubStepConstants& a_constants);
		void ProcessAngularSpringStep(const UpdateContext& a_context, const SubStepConstants& a_constants);

		Transform CalculateInterpolatedTransform(const UpdateContext& a_context, const SubStepConstants& a_constants) const;
	};
}