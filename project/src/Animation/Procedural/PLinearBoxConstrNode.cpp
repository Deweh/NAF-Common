#include "PLinearBoxConstrNode.h"

namespace Animation::Procedural
{
	using namespace ozz::math;

	Physics::LinearConstraint* PLinearBoxConstrNode::InstanceData::IsLinearConstraint()
	{
		return this;
	}

	std::unique_ptr<PNodeInstanceData> PLinearBoxConstrNode::CreateInstanceData()
	{
		return std::make_unique<InstanceData>();
	}

	PEvaluationResult PLinearBoxConstrNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext)
	{
		auto inst = static_cast<InstanceData*>(a_instanceData);
		const Float4 min = GetRequiredInput<ozz::math::Float4>(0, a_evalContext);
		const Float4 max = GetRequiredInput<ozz::math::Float4>(1, a_evalContext);
		const float bounce = GetOptionalInput<float>(2, 0.0f, a_evalContext);

		Physics::Spring* softSpring = nullptr;
		if (auto data = GetOptionalInput<PDataObject*>(3, nullptr, a_evalContext); data) {
			if (auto props = data->IsSpringProperties(); props) {
				softSpring = &props->spring;
			}
		}

		inst->min = simd_float4::Load3PtrU(&min.x);
		inst->max = simd_float4::Load3PtrU(&max.x);
		inst->bounce = bounce;
		inst->softSpring = softSpring;
		return static_cast<PDataObject*>(inst);
	}
}