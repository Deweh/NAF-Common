#include "PAngularConeConstrNode.h"
#include "Util/Math.h"

namespace Animation::Procedural
{
	using namespace ozz::math;

	Physics::AngularConstraint* PAngularConeConstrNode::InstanceData::IsAngularConstraint()
	{
		return this;
	}

	std::unique_ptr<PNodeInstanceData> PAngularConeConstrNode::CreateInstanceData()
	{
		return std::make_unique<InstanceData>();
	}

	PEvaluationResult PAngularConeConstrNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext)
	{
		auto inst = static_cast<InstanceData*>(a_instanceData);
		const float halfAngle = GetRequiredInput<float>(0, a_evalContext) * Util::DEGREE_TO_RADIAN;
		const float bounce = GetRequiredInput<float>(1, a_evalContext);

		inst->halfAngle = halfAngle;
		inst->bounce = bounce;
		return static_cast<PDataObject*>(inst);
	}
}