#pragma once
#include "PNode.h"
#include "Animation/Ozz.h"
#include "Animation/FileID.h"

namespace Animation::Procedural
{
	class PFullAnimationNode : public PNodeT<PFullAnimationNode>
	{
	public:
		struct InstanceData : public PNodeInstanceDataT<InstanceData>
		{
			bool looped{ false };
			float localTime{ 0.0f };
			float speedMod{ 0.0f };
			std::unique_ptr<IBasicAnimationContext> context;

			virtual size_t GetSizeBytes() override;
		};

		std::shared_ptr<IBasicAnimation> anim;
		float durationInv;

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		virtual void AdvanceTime(PNodeInstanceData* a_instanceData, float a_deltaTime) override;
		virtual void Synchronize(PNodeInstanceData* a_instanceData, PNodeInstanceData* a_ownerInstance, float a_correctionDelta) override;
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override;

		inline static Registration _reg{
			"anim",
			{
				{ "speedMod", PEvaluationType<float>, true }
			},
			{
				{ "file", PEvaluationType<RE::BSFixedString> },
				{ "syncId", PEvaluationType<uint64_t> }
			},
			PEvaluationType<PoseCache::Handle>,
			CreateNodeOfType<PFullAnimationNode>
		};
	};
}