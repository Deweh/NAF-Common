#pragma once
#include "PNode.h"
#include "Animation/Ozz.h"
#include "Animation/FileID.h"

namespace Animation::Procedural
{
	class PStaticPoseNode : public PNodeT<PStaticPoseNode>
	{
	public:
		std::vector<ozz::math::SoaTransform> pose;

		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override;
		virtual size_t GetSizeBytes() override;

		inline static Registration _reg{
			"pose",
			{
			},
			{
				{ "file", PEvaluationType<RE::BSFixedString> }
			},
			PEvaluationType<PoseCache::Handle>,
			CreateNodeOfType<PStaticPoseNode>
		};
	};
}