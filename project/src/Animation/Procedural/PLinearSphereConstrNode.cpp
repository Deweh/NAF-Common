#include "PLinearSphereConstrNode.h"

namespace Animation::Procedural
{
	Physics::LinearConstraint* PLinearSphereConstrNode::InstanceData::IsLinearConstraint()
	{
		return this;
	}

	std::unique_ptr<PNodeInstanceData> PLinearSphereConstrNode::CreateInstanceData()
	{
		return std::make_unique<InstanceData>();
	}

	PEvaluationResult PLinearSphereConstrNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext)
	{
		auto inst = static_cast<InstanceData*>(a_instanceData);
		inst->radius = GetRequiredInput<float>(0, a_evalContext);
		inst->softness = GetRequiredInput<float>(1, a_evalContext);
		return static_cast<PDataObject*>(inst);
	}
}