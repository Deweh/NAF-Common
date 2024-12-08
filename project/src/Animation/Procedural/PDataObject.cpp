#include "PDataObject.h"

namespace Animation::Procedural
{
	Physics::LinearConstraint* PDataObject::IsLinearConstraint()
	{
		return nullptr;
	}

	Physics::AngularConstraint* PDataObject::IsAngularConstraint()
	{
		return nullptr;
	}

	Physics::SpringProperties* PDataObject::IsSpringProperties()
	{
		return nullptr;
	}
}