#pragma once
#include "PDataObject.h"
#include "PNode.h"

namespace Animation::Procedural
{
	class PAngularConeConstrNode : public PNodeT<PAngularConeConstrNode>
	{
	public:
		struct InstanceData :
			public PNodeInstanceDataT<InstanceData>,
			public PDataObject,
			public Physics::AngularConeConstraint
		{
			virtual Physics::AngularConstraint* IsAngularConstraint() override;
		};

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;

		inline static Registration _reg{
			"angle_cone_constr",
			{ 
				{ "halfAngle", PEvaluationType<float> },
				{ "bounce", PEvaluationType<float> } },
			{
			},
			PEvaluationType<PDataObject*>,
			CreateNodeOfType<PAngularConeConstrNode>
		};
	};
}