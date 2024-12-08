#pragma once
#include "PNode.h"
#include "Physics/Body.h"

namespace Animation::Procedural
{
	class PSpringBoneNode : public PNodeT<PSpringBoneNode>
	{
	public:
		struct InstanceData : public PNodeInstanceDataT<InstanceData>
		{
			Physics::Body body;
			bool initialized = false;
		};

		uint16_t boneIdx;
		uint16_t parentIdx;

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		virtual void AdvanceTime(PNodeInstanceData* a_instanceData, float a_deltaTime) override;
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override;

		inline static Registration _reg{
			"spring_bone",
			{
				{ "pose", PEvaluationType<PoseCache::Handle> },
				{ "linearProps", PEvaluationType<PDataObject*>, true },
				{ "angularProps", PEvaluationType<PDataObject*>, true },
				{ "linearConstr", PEvaluationType<PDataObject*>, true },
				{ "angularConstr", PEvaluationType<PDataObject*>, true },
			},
			{
				{ "bone", PEvaluationType<RE::BSFixedString> }
			},
			PEvaluationType<PoseCache::Handle>,
			CreateNodeOfType<PSpringBoneNode>
		};
	};
}