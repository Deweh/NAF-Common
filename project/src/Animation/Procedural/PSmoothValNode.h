#pragma once
#include "PNode.h"

namespace Animation::Procedural
{
	class PSmoothValNode : public PNodeT<PSmoothValNode>
	{
	public:
		struct InstanceData : public PNodeInstanceDataT<InstanceData>
		{
			bool initialized{ false };
			float previousValue;
			float timeStep;
		};

		float percentPerSec;

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		virtual void AdvanceTime(PNodeInstanceData* a_instanceData, float a_deltaTime) override;
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override;

		inline static Registration _reg{
			"smooth_val",
			{
				{ "input", PEvaluationType<float> }
			},
			{
				{ "percent", PEvaluationType<float> }
			},
			PEvaluationType<float>,
			CreateNodeOfType<PSmoothValNode>
		};
	};
}