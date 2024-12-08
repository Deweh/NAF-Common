#pragma once
#include "PNode.h"
#include "PDataObject.h"

namespace Animation::Procedural
{
	class PLinearSphereConstrNode : public PNodeT<PLinearSphereConstrNode>
	{
	public:
		struct InstanceData :
			public PNodeInstanceDataT<InstanceData>,
			public PDataObject,
			public Physics::LinearSphereConstraint
		{
			virtual Physics::LinearConstraint* IsLinearConstraint() override;
		};

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;

		inline static Registration _reg{
			"linear_sphere_constr",
			{
				{ "radius", PEvaluationType<float> },
				{ "soft", PEvaluationType<float> }
			},
			{
			},
			PEvaluationType<PDataObject*>,
			CreateNodeOfType<PLinearSphereConstrNode>
		};
	};
}