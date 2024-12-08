#pragma once
#include "Physics/Constraint.h"
#include "Physics/Body.h"

namespace Animation::Procedural
{
	struct PDataObject
	{
		virtual ~PDataObject() = default;
		virtual Physics::LinearConstraint* IsLinearConstraint();
		virtual Physics::AngularConstraint* IsAngularConstraint();
		virtual Physics::SpringProperties* IsSpringProperties();
	};
}