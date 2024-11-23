#pragma once
#include "Easing.h"
#include "IPostGenJob.h"

namespace Animation
{
	struct IKTwoBoneData : public IPostGenJob
	{
		static constexpr size_t START_GUID{ 0 };

		enum FLAG : uint8_t
		{
			kNoFlags = 0,
			kDeleteOnTransitionFinish = 1u << 0,
			kPendingDelete = 1u << 1,

			// Preserves the model-space rotation of the end node.
			// i.e. if the foot was pointing towards the ground before the IK solve, it will stay pointing towards the ground.
			kKeepEndNodeMSRotation = 1u << 2
		};

		enum TransitionMode : uint8_t
		{
			kNone = 0,
			kIn = 1,
			kOut = 2
		};

		struct TransitionData
		{
			float duration = 0.0f;
			float currentTime = 0.0f;
			TransitionMode mode = TransitionMode::kNone;
			CubicInOutEase<float> ease;
		};

		RE::NiPoint3 target;
		RE::NiPoint3 poleDir;
		TransitionData transition;
		uint16_t start_node;
		uint16_t mid_node;
		uint16_t end_node;
		xSE::stl::enumeration<FLAG, uint8_t> flags = FLAG::kKeepEndNodeMSRotation;
		uint8_t chainId;
		bool targetWithinRange = false;

		IKTwoBoneData();

		void TransitionIn(float a_duration);
		void TransitionOut(float a_duration, bool a_delete);
		void CalculateJobWeight(float a_deltaTime, ozz::animation::IKTwoBoneJob& a_job);
		bool Validate(const ozz::animation::Skeleton* a_skeleton);
		bool Update(float a_deltaTime,
			const std::span<ozz::math::SoaTransform>& a_localOutput,
			const std::span<ozz::math::Float4x4>& a_modelSpace,
			const ozz::math::Float4x4& a_invertedRoot,
			const ozz::animation::Skeleton* a_skeleton);

		static uint64_t ChainIDToGUID(uint8_t a_chainId);
		static uint8_t GUIDToChainID(uint64_t a_guid);

		virtual bool Run(const Context& a_context) override;
		virtual uint64_t GetGUID() override;
		virtual void Destroy() override;
	};
}