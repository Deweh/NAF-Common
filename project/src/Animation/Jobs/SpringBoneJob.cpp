#include "SpringBoneJob.h"
#include "Util/Ozz.h"

namespace Animation
{
	bool SpringBoneJob::Run(const Context& a_context)
	{
		/*
		springContext.deltaTime = a_context.deltaTime;
		Procedural::SpringPhysicsJob springJob;
		springJob.stiffness = stiffness;
		springJob.damping = damping;
		springJob.mass = mass;
		springJob.gravity = gravity;
		springJob.boneTransform = &a_context.modelSpaceMatrices[boneIdx];
		springJob.parentTransform = &a_context.modelSpaceMatrices[boneIdx];
		springJob.rootTransform = &a_context.rootMatrix;
		springJob.prevRootTransform = &a_context.prevRootMatrix;
		springJob.context = &springContext;

		ozz::math::SimdFloat4 output;
		springJob.positionOutput = &output;

		if (!springJob.Run())
			return true;

		Util::Ozz::ApplySoATransformTranslation(boneIdx, output, std::span(a_context.localTransforms, a_context.localCount));
		*/
		return true;
	}

	uint64_t SpringBoneJob::GetGUID()
	{
		return START_GUID + springId;
	}

	void SpringBoneJob::Destroy()
	{
		delete this;
	}
}