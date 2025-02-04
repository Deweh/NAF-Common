#pragma once
#include "PNode.h"

namespace Animation::Procedural
{
	class PBlend2DNode : public PNodeT<PBlend2DNode>
	{
	public:
		struct BlendTriangle
		{
			static constexpr size_t NO_ADJACENT_TRI = std::numeric_limits<size_t>::max();

			ozz::math::Float2 a, b, c;
			size_t inputA, inputB, inputC;
			std::array<size_t, 3> walkAdjacencies;
		};

		using tri_vector_t = std::vector<BlendTriangle>;

		struct InstanceData : public PNodeInstanceDataT<InstanceData>
		{
			tri_vector_t::iterator lastTri;
		};

		tri_vector_t triangles;

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override;

		inline static Registration _reg{
			"blend_2d",
			{
				{ "x", PEvaluationType<float> },
				{ "y", PEvaluationType<float> }
			},
			{
				{ "placeholder", PEvaluationType<float> }
			},
			PEvaluationType<PoseCache::Handle>,
			CreateNodeOfType<PBlend2DNode>
		};
	};
}