#pragma once
#include "PNode.h"
#include "PDataObject.h"

namespace Animation::Procedural
{
	class PSpringPropsNode : public PNodeT<PSpringPropsNode>
	{
	public:
		struct InstanceData :
			public PNodeInstanceDataT<InstanceData>,
			public PDataObject,
			public Physics::SpringWithBodyProperties
		{
			virtual Physics::SpringWithBodyProperties* IsSpringProperties() override;
		};

		ozz::math::Float3 upAxis;

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override;

		inline static Registration _reg{
			"spring_props",
			{
				{ "stiff", PEvaluationType<float> },
				{ "damp", PEvaluationType<float> },
				{ "mass", PEvaluationType<float> },
				{ "gravity", PEvaluationType<ozz::math::Float4>, true },
			},
			{
				{ "up_x", PEvaluationType<float> },
				{ "up_y", PEvaluationType<float> },
				{ "up_z", PEvaluationType<float> }
			},
			PEvaluationType<PDataObject*>,
			CreateNodeOfType<PSpringPropsNode>
		};
	};
}