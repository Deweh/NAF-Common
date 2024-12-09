#pragma once
#include "Constraint.h"
#include "DynamicProperty.h"
#include "Spring.h"

namespace Physics
{
	class ModelSpaceSystem;

	struct Transform
	{
		ozz::math::SimdFloat4 position;
		ozz::math::SimdQuaternion rotation;
	};

	class Body
	{
	public:
		struct UpdateContext
		{
			ModelSpaceSystem* system = nullptr;
			LinearConstraint* linearConstraint = nullptr;
			AngularConstraint* angularConstraint = nullptr;
			SpringWithBodyProperties* linearProps = nullptr;
			SpringWithBodyProperties* angularProps = nullptr;
			ozz::math::Float4x4 animatedTransform;
			ozz::math::Float4x4 parentTransform;
		};

		struct SubStepConstants
		{
			struct PropertyConstants
			{
				ozz::math::SimdFloat4 force;
				float massInverse;
			};
			
			PropertyConstants linear;
			PropertyConstants angular;
			ozz::math::SimdFloat4 restPositionMS;
			ozz::math::SimdQuaternion restRotationMS;
			ozz::math::SimdQuaternion parentInverseRotMS;
			ozz::math::Float4x4 parentInverseMS;
		};

		DynamicProperty<ozz::math::SimdFloat4> position;
		DynamicProperty<ozz::math::SimdQuaternion> rotation;

		Transform Update(const UpdateContext& a_context);

	protected:
		void CalculatePropertyConstants(const UpdateContext& a_context, SpringWithBodyProperties& a_props, SubStepConstants::PropertyConstants& a_constantsOut);
		void BeginStepUpdate(const UpdateContext& a_context, SubStepConstants& a_constants);
		void ProcessPhysicsStep(const UpdateContext& a_context, const SubStepConstants& a_constants);

		void ProcessLinearStep(const UpdateContext& a_context, const SubStepConstants& a_constants);
		void ProcessAngularStep(const UpdateContext& a_context, const SubStepConstants& a_constants);

		Transform CalculateInterpolatedTransform(const UpdateContext& a_context, const SubStepConstants& a_constants) const;
	};
}