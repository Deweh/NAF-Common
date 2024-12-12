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
		const float bounce = GetOptionalInput<float>(1, 0.0f, a_evalContext);

		Physics::Spring* softSpring = nullptr;
		if (auto data = GetOptionalInput<PDataObject*>(2, nullptr, a_evalContext); data) {
			if (auto props = data->IsSpringProperties(); props) {
				softSpring = &props->spring;
			}
		}

		inst->halfAngle = halfAngle;
		inst->bounce = bounce;
		inst->softSpring = softSpring;
		return static_cast<PDataObject*>(inst);
	}
}