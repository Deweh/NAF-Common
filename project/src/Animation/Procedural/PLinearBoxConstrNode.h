#pragma once
#include "PDataObject.h"
#include "PNode.h"

namespace Animation::Procedural
{
	class PLinearBoxConstrNode : public PNodeT<PLinearBoxConstrNode>
	{
	public:
		struct InstanceData :
			public PNodeInstanceDataT<InstanceData>,
			public PDataObject,
			public Physics::LinearBoxConstraint
		{
			virtual Physics::LinearConstraint* IsLinearConstraint() override;
		};

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;

		inline static Registration _reg{
			"linear_box_constr",
			{
				{ "min", PEvaluationType<ozz::math::Float4> },
				{ "max", PEvaluationType<ozz::math::Float4> },
				{ "soft", PEvaluationType<float> } },
			{
			},
			PEvaluationType<PDataObject*>,
			CreateNodeOfType<PLinearBoxConstrNode>
		};
	};
}