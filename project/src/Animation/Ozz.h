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
		// Overrides of the skeleton's default bone mask, by name: bones it leaves out that this animation writes anyway,
		// and bones it includes that this animation leaves to the game.
		std::vector<std::string> unmaskedBones;
		std::vector<std::string> maskedBones;
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
		std::vector<bool> boneMask;  // Per joint, or empty for the skeleton's default.

		virtual size_t GetSizeBytes() override;
		virtual const std::vector<bool>* GetBoneMask() override;
		virtual void SampleBoneAnimation(float a_time, const ozz::span<ozz::math::SoaTransform>& a_transformsOut, IBasicAnimationContext* a_context) override;
		virtual void SampleFaceAnimation(float a_time, const std::span<float>& a_morphsOut) override;
		virtual void SampleEventTrack(float a_time, float a_timeStep, IAnimEventHandler* a_eventHandler) override;
		virtual bool HasBoneAnimation() override;
		virtual bool HasFaceAnimation() override;
		virtual float GetDuration() override;
		virtual std::unique_ptr<IBasicAnimationContext> CreateContext() override;
	};

	struct OzzSkeleton : public std::enable_shared_from_this<OzzSkeleton>
	{
		ozz::unique_ptr<ozz::animation::Skeleton> data = nullptr;
		std::vector<bool> defaultBoneMask;
		std::vector<bool> controlledByGameMask;
		std::string name;
		// A hash of the bones the game animates (names and parents), which comes from the game's animation skeleton: the
		// .rig on Starfield, the Havok skeleton on Fallout 4. Skeletons of different races with the same hash animate the
		// same bones, so content made for one fits the other. Computed at runtime and never saved, since installed mods can
		// extend a game skeleton. Empty when no bone is game-controlled.
		std::optional<std::uint64_t> gameSkeletonHash;
#ifdef TARGET_GAME_F4
		std::vector<int32_t> ozzToHavokIdxs;
#endif
	};
}