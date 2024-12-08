#include "PSpringPropsNode.h"

namespace Animation::Procedural
{
	Physics::SpringProperties* PSpringPropsNode::InstanceData::IsSpringProperties()
	{
		return this;
	}

	std::unique_ptr<PNodeInstanceData> PSpringPropsNode::CreateInstanceData()
	{
		auto result = std::make_unique<InstanceData>();
		result->upAxis = ozz::math::simd_float4::Load3PtrU(&upAxis.x);
		return result;
	}

	PEvaluationResult PSpringPropsNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext)
	{
		auto inst = static_cast<InstanceData*>(a_instanceData);
		inst->stiffness = std::clamp(GetRequiredInput<float>(0, a_evalContext), 1.0f, 10000.0f);
		inst->damping = std::clamp(GetRequiredInput<float>(1, a_evalContext), 0.001f, 1.0f);
		inst->mass = std::clamp(GetRequiredInput<float>(2, a_evalContext), 0.1f, 2000.0f);

		const ozz::math::Float4 gravity = GetOptionalInput<ozz::math::Float4>(3, ozz::math::Float4::zero(), a_evalContext);
		inst->gravity = ozz::math::simd_float4::Load3PtrU(&gravity.x);
		return static_cast<PDataObject*>(inst);
	}

	bool PSpringPropsNode::SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir)
	{
		upAxis = {
			std::get<float>(a_values[0]),
			std::get<float>(a_values[1]),
			std::get<float>(a_values[2]),
		};
		return true;
	}
}