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
		inst->bounce = GetOptionalInput<float>(1, 0.0f, a_evalContext);

		Physics::Spring* softSpring = nullptr;
		if (auto data = GetOptionalInput<PDataObject*>(2, nullptr, a_evalContext); data) {
			if (auto props = data->IsSpringProperties(); props) {
				softSpring = &props->spring;
			}
		}

		inst->softSpring = softSpring;
		return static_cast<PDataObject*>(inst);
	}
}