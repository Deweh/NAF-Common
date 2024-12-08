#include "PAngularConeConstrNode.h"

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
		const Float4 axis = GetRequiredInput<ozz::math::Float4>(0, a_evalContext);
		const float halfAngle = GetRequiredInput<float>(1, a_evalContext);
		const float soft = GetRequiredInput<float>(2, a_evalContext);

		inst->axis = simd_float4::Load3PtrU(&axis.x);
		inst->halfAngle = halfAngle;
		inst->softness = soft;
		return static_cast<PDataObject*>(inst);
	}
}