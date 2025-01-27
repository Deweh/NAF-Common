#pragma once
#include "FileID.h"
#include "IBasicAnimation.h"

namespace Animation
{
	struct RawOzzFaceAnimation
	{
		std::array<ozz::animation::offline::RawFloatTrack, FACE_MORPHS_SIZE> tracks;
		float duration;
	};

	struct RawOzzAnimation
	{
		ozz::unique_ptr<ozz::animation::offline::RawAnimation> data = nullptr;
		std::unique_ptr<RawOzzFaceAnimation> faceData = nullptr;
	};

	struct OzzFaceAnimation
	{
		std::array<ozz::animation::FloatTrack, FACE_MORPHS_SIZE> tracks;
		float duration;
	};

	struct OzzAnimation : public IBasicAnimation
	{
		class Context : public IBasicAnimationContext
		{
		public:
			ozz::animation::SamplingJob::Context context;

			virtual size_t GetSizeBytes() override;
		};

		ozz::unique_ptr<ozz::animation::Animation> data = nullptr;
		std::unique_ptr<OzzFaceAnimation> faceData = nullptr;

		virtual size_t GetSizeBytes() override;
		virtual void SampleBoneAnimation(float a_time, const ozz::span<ozz::math::SoaTransform>& a_transformsOut, IBasicAnimationContext* a_context) override;
		virtual void SampleFaceAnimation(float a_time, const std::span<float>& a_morphsOut) override;
		virtual void SampleEventTrack(float a_time, float a_timeStep, IAnimEventHandler* a_eventHandler) override;
		virtual bool HasBoneAnimation() override;
		virtual bool HasFaceAnimation() override;
		virtual float GetDuration() override;
		virtual std::unique_ptr<IBasicAnimationContext> CreateContext() override;
	};

	struct OzzSkeleton
	{
		ozz::unique_ptr<ozz::animation::Skeleton> data = nullptr;
		std::vector<bool> defaultBoneMask;
		std::vector<bool> controlledByGameMask;
		std::string name;
#ifdef TARGET_GAME_F4
		std::vector<RE::hkQsTransformf> havokRestPose;
		std::vector<int32_t> havokToOzzIdxs;
#endif
	};
}