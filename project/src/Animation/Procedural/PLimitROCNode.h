#pragma once
#include "PNode.h"

namespace Animation::Procedural
{
	class PLimitROCNode : public PNodeT<PLimitROCNode>
	{
	public:
		struct InstanceData : public PNodeInstanceDataT<InstanceData>
		{
			bool initialized{ false };
			float previousValue;
			float timeStep;
		};

		float rateOfChange;

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		virtual void AdvanceTime(PNodeInstanceData* a_instanceData, float a_deltaTime) override;
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override;

		inline static Registration _reg{
			"limit_roc",
			{
				{ "input", PEvaluationType<float> }
			},
			{
				{ "roc", PEvaluationType<float> }
			},
			PEvaluationType<float>,
			CreateNodeOfType<PLimitROCNode>
		};
	};
}