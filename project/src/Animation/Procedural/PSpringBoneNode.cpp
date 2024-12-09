#include "PSpringBoneNode.h"
#include "Settings/Settings.h"
#include "Util/Ozz.h"
#include "Util/Math.h"
#include "ozz/base/maths/simd_quaternion.h"
#include "PDataObject.h"

namespace Animation::Procedural
{
	std::unique_ptr<PNodeInstanceData> PSpringBoneNode::CreateInstanceData()
	{
		return std::make_unique<InstanceData>();
	}

	PEvaluationResult PSpringBoneNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext)
	{
		using namespace ozz::math;
		auto inst = static_cast<InstanceData*>(a_instanceData);

		// Get node input data.
		PoseCache::Handle& input = GetRequiredInput<PoseCache::Handle>(0, a_evalContext);

		Physics::SpringWithBodyProperties* linearProps = nullptr;
		if (auto data = GetOptionalInput<PDataObject*>(1, nullptr, a_evalContext); data) {
			linearProps = data->IsSpringProperties();
		}

		Physics::SpringWithBodyProperties* angularProps = nullptr;
		if (auto data = GetOptionalInput<PDataObject*>(2, nullptr, a_evalContext); data) {
			angularProps = data->IsSpringProperties();
		}

		Physics::LinearConstraint* linearConstraint = nullptr;
		if (auto data = GetOptionalInput<PDataObject*>(3, nullptr, a_evalContext); data) {
			linearConstraint = data->IsLinearConstraint();
		}

		Physics::AngularConstraint* angularConstraint = nullptr;
		if (auto data = GetOptionalInput<PDataObject*>(4, nullptr, a_evalContext); data) {
			angularConstraint = data->IsAngularConstraint();
		}

		// Acquire a pose handle for this node's output and copy the input pose to the output pose - we only need to make 1 correction to the pose.
		PoseCache::Handle output = a_poseCache.acquire_handle();
		auto inputSpan = input.get();
		auto outputSpan = output.get();
		std::copy(inputSpan.begin(), inputSpan.end(), outputSpan.begin());

		// Update model-space cache.
		a_evalContext.UpdateModelSpaceCache(outputSpan, ozz::animation::Skeleton::kNoParent, boneIdx);

		// Setup spring params & run.
		Physics::Body::UpdateContext ctxt{
			.system = a_evalContext.physSystem,
			.linearConstraint = linearConstraint,
			.angularConstraint = angularConstraint,
			.linearProps = linearProps,
			.angularProps = angularProps,
			.animatedTransform = a_evalContext.modelSpaceCache[boneIdx],
			.parentTransform = a_evalContext.modelSpaceCache[parentIdx]
		};

		if (!inst->initialized) {
			const SimdFloat4 initPos = ctxt.animatedTransform.cols[3];
			const SimdQuaternion initRot = Util::Ozz::ToNormalizedQuaternion(ctxt.animatedTransform);
			inst->body.position.current = initPos;
			inst->body.position.previous = initPos;
			inst->body.rotation.current = initRot;
			inst->body.rotation.previous = initRot;
			inst->initialized = true;
		}

		Physics::Transform result = inst->body.Update(ctxt);

		if (linearProps) {
			Util::Ozz::ApplySoATransformTranslation(boneIdx, result.position, outputSpan);
		}

		if (angularProps) {
			Util::Ozz::ApplySoATransformQuaternion(boneIdx, result.rotation, outputSpan);
		}
		
		return output;
	}

	bool PSpringBoneNode::SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir)
	{
		const RE::BSFixedString& boneName = std::get<RE::BSFixedString>(a_values[0]);
		const auto idxs = Util::Ozz::GetJointIndexes(a_skeleton->data.get(), boneName.c_str());

		if (!idxs.has_value()) {
			return false;
		}

		boneIdx = idxs->at(0);
		int sParent = a_skeleton->data->joint_parents()[boneIdx];
		if (sParent == ozz::animation::Skeleton::kNoParent) {
			return false;
		}
		parentIdx = sParent;

		return true;
	}
}