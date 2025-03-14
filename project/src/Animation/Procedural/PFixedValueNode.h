#pragma once
#include "PNode.h"

namespace Animation::Procedural
{
	class PFixedValueNode : public PNodeT<PFixedValueNode>
	{
	public:
		float value;

		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override;

		inline static Registration _reg{
			"fixed_val",
			{},
			{
				{ "val", PEvaluationType<float> }
			},
			PEvaluationType<float>,
			CreateNodeOfType<PFixedValueNode>
		};
	};
}