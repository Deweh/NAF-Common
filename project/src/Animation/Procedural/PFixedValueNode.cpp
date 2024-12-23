#include "PFixedValueNode.h"

namespace Animation::Procedural
{
	PEvaluationResult PFixedValueNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext)
	{
		return value;
	}

	bool PFixedValueNode::SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir)
	{
		value = std::get<float>(a_values[0]);
		return true;
	}
}