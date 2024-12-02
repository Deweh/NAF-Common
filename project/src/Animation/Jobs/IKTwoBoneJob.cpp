#include "IKTwoBoneJob.h"
#include "Util/Ozz.h"

namespace Animation
{
	IKTwoBoneJob::IKTwoBoneJob()
	{
	}

	void IKTwoBoneJob::TransitionIn(float a_duration)
	{
		transition.mode = IKTwoBoneJob::TransitionMode::kIn;
		transition.duration = std::max(a_duration, 0.0001f);
		transition.currentTime = 0.0f;
	}

	void IKTwoBoneJob::TransitionOut(float a_duration, bool a_delete)
	{
		transition.mode = IKTwoBoneJob::TransitionMode::kOut;
		transition.duration = std::max(a_duration, 0.0001f);
		transition.currentTime = 0.0f;

		if (a_delete) {
			flags.set(FLAG::kDeleteOnTransitionFinish);
		}
	}

	void IKTwoBoneJob::CalculateJobWeight(float a_deltaTime, ozz::animation::IKTwoBoneJob& a_job)
	{
		if (transition.mode != TransitionMode::kNone) {
			transition.currentTime += a_deltaTime;
			if (transition.currentTime >= transition.duration) {
				a_job.weight = transition.mode == TransitionMode::kIn ? 1.0f : 0.0f;
				transition.mode = TransitionMode::kNone;
				if (flags.any(FLAG::kDeleteOnTransitionFinish)) {
					flags.reset(FLAG::kDeleteOnTransitionFinish);
					flags.set(FLAG::kPendingDelete);
				}
			} else {
				float normalTime = transition.ease(transition.currentTime / transition.duration);
				a_job.weight = (transition.mode == TransitionMode::kIn ? normalTime : (1.0f - normalTime));
			}
		} else {
			a_job.weight = 1.0f;
		}
	}

	bool IKTwoBoneJob::Validate(const ozz::animation::Skeleton* a_skeleton)
	{
		int num_joints = a_skeleton->num_joints();
		return start_node < num_joints && mid_node < num_joints && end_node < num_joints;
	}

	bool IKTwoBoneJob::Update(float a_deltaTime,
		const std::span<ozz::math::SoaTransform>& a_localOutput,
		const std::span<ozz::math::Float4x4>& a_modelSpace,
		const ozz::math::Float4x4& a_invertedRoot,
		const ozz::animation::Skeleton* a_skeleton)
	{
		ozz::animation::IKTwoBoneJob ikJob;
		CalculateJobWeight(a_deltaTime, ikJob);

		ozz::math::SimdQuaternion endWorldRotation;
		if (flags.any(FLAG::kKeepEndNodeMSRotation)) {
			endWorldRotation = Util::Ozz::ToNormalizedQuaternion(a_modelSpace[end_node]);
		}

		ikJob.target = ozz::math::TransformPoint(a_invertedRoot, ozz::math::simd_float4::Load3PtrU(&target.x));
		ikJob.pole_vector = ozz::math::simd_float4::Load3PtrU(&poleDir.x);
		ikJob.mid_axis = ozz::math::simd_float4::z_axis();

		ikJob.soften = 0.9f;
		ikJob.twist_angle = 0.0f;

		ikJob.start_joint = &a_modelSpace[start_node];
		ikJob.mid_joint = &a_modelSpace[mid_node];
		ikJob.end_joint = &a_modelSpace[end_node];

		ozz::math::SimdQuaternion corrections[2];
		ikJob.start_joint_correction = &corrections[0];
		ikJob.mid_joint_correction = &corrections[1];
		ikJob.reached = &targetWithinRange;

		if (!ikJob.Run())
			return false;

		Util::Ozz::MultiplySoATransformQuaternion(start_node, corrections[0], a_localOutput);
		Util::Ozz::MultiplySoATransformQuaternion(mid_node, corrections[1], a_localOutput);

		if (flags.any(FLAG::kKeepEndNodeMSRotation)) {
			ozz::animation::LocalToModelJob l2mJob;
			l2mJob.skeleton = a_skeleton;
			l2mJob.from = start_node;
			l2mJob.to = end_node;
			l2mJob.input = ozz::make_span(a_localOutput);
			l2mJob.output = { &a_modelSpace.front(), a_modelSpace.size() };
			l2mJob.Run();

			Util::Ozz::ApplyMSRotationToLocal(endWorldRotation, end_node, a_localOutput, a_modelSpace, a_skeleton);
		}

		return true;
	}

	uint64_t IKTwoBoneJob::ChainIDToGUID(uint8_t a_chainId)
	{
		return START_GUID + static_cast<uint64_t>(a_chainId);
	}

	uint8_t IKTwoBoneJob::GUIDToChainID(uint64_t a_guid)
	{
		return static_cast<uint8_t>(a_guid - START_GUID);
	}

	bool IKTwoBoneJob::Run(const Context& a_context)
	{
		Update(a_context.deltaTime,
			std::span(a_context.localTransforms, a_context.localCount),
			std::span(a_context.modelSpaceMatrices, a_context.modelSpaceCount),
			a_context.invertedRootMatrix,
			a_context.skeleton);
		
		return flags.none(FLAG::kPendingDelete);
	}

	uint64_t IKTwoBoneJob::GetGUID()
	{
		return ChainIDToGUID(chainId);
	}

	void IKTwoBoneJob::Destroy()
	{
		delete this;
	}
}