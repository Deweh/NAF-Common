#pragma once
#include "IPostGenJob.h"
#include "Animation/Procedural/PSpringBoneNode.h"

namespace Animation
{
	class SpringBoneJob : public IPostGenJob
	{
		static constexpr size_t START_GUID{ 256 };

		//Procedural::SpringPhysicsJob::Context springContext;
		ozz::math::SimdFloat4 prevRootPos;
		ozz::math::SimdFloat4 gravity;
		float stiffness;
		float damping;
		float mass;
		uint16_t boneIdx;
		uint16_t parentIdx;
		uint8_t springId;

		virtual bool Run(const Context& a_context) override;
		virtual uint64_t GetGUID() override;
		virtual void Destroy() override;
	};
}