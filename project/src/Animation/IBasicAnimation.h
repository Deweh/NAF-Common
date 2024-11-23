#pragma once
#include "IAnimationFile.h"
#include "IAnimEventHandler.h"

namespace Animation
{
	class IBasicAnimationContext
	{
	public:
		virtual ~IBasicAnimationContext() = default;
	};

	class IBasicAnimation : public IAnimationFile
	{
	public:
		virtual std::unique_ptr<Generator> CreateGenerator() override;
		virtual void SampleBoneAnimation(float a_time, const ozz::span<ozz::math::SoaTransform>& a_transformsOut, IBasicAnimationContext* a_context) = 0;
		virtual void SampleFaceAnimation(float a_time, const std::span<float>& a_morphsOut) = 0;
		virtual void SampleEventTrack(float a_time, float a_timeStep, IAnimEventHandler* a_eventHandler) = 0;
		virtual bool HasBoneAnimation() = 0;
		virtual bool HasFaceAnimation() = 0;
		virtual float GetDuration() = 0;
		virtual std::unique_ptr<IBasicAnimationContext> CreateContext() = 0;
	};
}