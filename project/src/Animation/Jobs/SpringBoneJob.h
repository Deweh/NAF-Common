#pragma once
#include "IPostGenJob.h"
#include "Animation/Procedural/PSpringBoneNode.h"

namespace Animation
{
	class SpringBoneJob : public IPostGenJob
	{
		//Procedural::SpringPhysicsJob::Context springContext;
		ozz::math::SimdFloat4 prevRootPos;
		ozz::math::SimdFloat4 gravity;
		float stiffness;
		float damping;
		float mass;
		uint32_t springId;
		uint16_t boneIdx;
		uint16_t parentIdx;

		virtual bool Run(const Context& a_context) override;
		virtual GUID GetGUID() override;
		virtual void Destroy() override;
	};
}